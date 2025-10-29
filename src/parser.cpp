// parser.cpp
#include "parser.h"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <vector>
#include <iostream>

namespace mail {
void Parser::debugPrint(const std::string& message, const std::string& content) {
    std::cerr << "DEBUG: " << message;
    if (!content.empty()) {
        std::cerr << " Content: " << content.substr(0, 100) << (content.length() > 100 ? "..." : "");
    }
    std::cerr << std::endl;
}

// --- member helpers --------------------------------------------------------
std::string Parser::trim(const std::string &s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    size_t end = s.find_last_not_of(" \t\r\n");
    return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
}

std::string Parser::toLower(const std::string &s) {
    std::string res = s;
    std::transform(res.begin(), res.end(), res.begin(), [](unsigned char c){ return std::tolower(c); });
    return res;
}

// Normalize newlines to `\n`
void Parser::normalizeNewlines(std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\r') {
            if (i+1 < s.size() && s[i+1] == '\n') { out.push_back('\n'); ++i; }
            else out.push_back('\n');
        } else out.push_back(s[i]);
    }
    s.swap(out);
}

// Split a block into headers and body using the first blank line (\\n\\n)
void Parser::splitHeadersBody(const std::string &raw, std::string &headers, std::string &body) {
    size_t pos = raw.find("\n\n");
    if (pos == std::string::npos) {
        // no blank line: treat all as headers (or all as body in some contexts)
        headers = raw;
        body = "";
    } else {
        headers = raw.substr(0, pos);
        body = raw.substr(pos + 2);
    }
    // trim possible leading newline from headers (happens when part starts with \\n)
    if (!headers.empty() && (headers[0] == '\n' || headers[0] == '\r')) {
        headers = trim(headers);
    }
}

// Parse headers block into a lower-cased map (handles folded headers)
std::map<std::string,std::string> Parser::parseHeaders(const std::string &headerBlock) {
    std::map<std::string,std::string> hdrs;
    std::istringstream ss(headerBlock);
    std::string line;
    std::string lastKey;
    while (std::getline(ss, line)) {
        // remove trailing CR (shouldn't exist after normalizeNewlines, but safe)
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && (line[0] == ' ' || line[0] == '\t')) {
            // folded header continuation
            if (!lastKey.empty()) {
                hdrs[lastKey] += ' ';
                hdrs[lastKey] += trim(line);
            }
        } else {
            size_t colon = line.find(':');
            if (colon != std::string::npos) {
                std::string key = toLower(trim(line.substr(0, colon)));
                std::string value = trim(line.substr(colon + 1));
                
                // Handle encoded words in header values
                value = decodeHeaderValue(value);
                
                hdrs[key] = value;
                lastKey = key;
            } else {
                // ignore malformed lines
                lastKey.clear();
            }
        }
    }
    return hdrs;
}

// Decode encoded words in header values (e.g., =?utf-8?B?...?=)
std::string Parser::decodeHeaderValue(const std::string &value) {
    std::string result;
    size_t start = 0;
    size_t pos = value.find("=?", start);
    
    if (pos == std::string::npos) {
        // No encoded words found
        return value;
    }
    
    result = value.substr(0, pos);
    
    while (pos != std::string::npos) {
        size_t charset_end = value.find('?', pos + 2);
        if (charset_end == std::string::npos) break;
        
        size_t encoding_end = value.find('?', charset_end + 1);
        if (encoding_end == std::string::npos) break;
        
        size_t end_marker = value.find("?=", encoding_end + 1);
        if (end_marker == std::string::npos) break;
        
        std::string charset = value.substr(pos + 2, charset_end - pos - 2);
        std::string encoding = value.substr(charset_end + 1, encoding_end - charset_end - 1);
        std::string encoded_text = value.substr(encoding_end + 1, end_marker - encoding_end - 1);
        
        std::string decoded_text;
        if (toLower(encoding) == "b") {
            // Base64 encoding
            decoded_text = decodeBase64(encoded_text);
        } else if (toLower(encoding) == "q") {
            // Quoted-printable encoding
            decoded_text = decodeQuotedPrintable(encoded_text);
            // Replace underscores with spaces as per Q encoding
            std::replace(decoded_text.begin(), decoded_text.end(), '_', ' ');
        } else {
            // Unknown encoding, use as-is
            decoded_text = encoded_text;
        }
        
        result += decoded_text;
        start = end_marker + 2;
        pos = value.find("=?", start);
        
        if (pos != std::string::npos) {
            // Add any text between encoded words
            result += value.substr(start, pos - start);
        } else {
            // Add remaining text after the last encoded word
            result += value.substr(start);
        }
    }
    
    return result;
}

// Extract parameter value from header field
std::string Parser::extractParameter(const std::string &headerValue, const std::string &paramName) {
    std::string lcValue = toLower(headerValue);
    std::string lcParamName = toLower(paramName) + "=";
    
    size_t paramPos = lcValue.find(lcParamName);
    if (paramPos == std::string::npos) {
        return "";
    }
    
    paramPos += lcParamName.length();
    while (paramPos < headerValue.size() && std::isspace(headerValue[paramPos])) {
        paramPos++;
    }
    
    if (paramPos >= headerValue.size()) {
        return "";
    }
    
    char quoteChar = 0;
    if (headerValue[paramPos] == '"' || headerValue[paramPos] == '\'') {
        quoteChar = headerValue[paramPos];
        paramPos++;
    }
    
    std::string result;
    while (paramPos < headerValue.size()) {
        if (quoteChar) {
            if (headerValue[paramPos] == quoteChar) {
                break;
            }
        } else {
            if (headerValue[paramPos] == ';' || headerValue[paramPos] == ' ' || headerValue[paramPos] == '\t') {
                break;
            }
        }
        
        // Handle escaped quotes
        if (headerValue[paramPos] == '\\' && paramPos + 1 < headerValue.size()) {
            result += headerValue[paramPos + 1];
            paramPos += 2;
            continue;
        }
        
        result += headerValue[paramPos];
        paramPos++;
    }
    
    return result;
}

// --- base64 & quoted-printable decoders -----------------------------------
// tolerant base64 value map
static inline unsigned char b64val(char c) {
    if ('A' <= c && c <= 'Z') return c - 'A';
    if ('a' <= c && c <= 'z') return c - 'a' + 26;
    if ('0' <= c && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return 255;
}

std::string Parser::decodeBase64(const std::string &in) {
    std::string out;
    out.reserve((in.size()*3)/4);
    int val = 0, valb = -8;
    for (unsigned char c : in) {
        if (c == '=' || c == '\r' || c == '\n' || c == ' ' || c == '\t') {
            if (c == '=') {
                // padding — stop consuming meaningful bytes; but continue to allow following whitespace
                // (we simply skip here; existing buffered bits produce correct bytes)
            }
            continue;
        }
        unsigned char v = b64val(c);
        if (v == 255) continue; // ignore non-base64 (be permissive)
        val = (val << 6) + v;
        valb += 6;
        if (valb >= 0) {
            out.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

std::string Parser::decodeQuotedPrintable(const std::string &in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        char c = in[i];
        if (c == '=' ) {
            // soft line break: '=' followed by newline -> remove both and continue
            if (i + 1 < in.size() && in[i+1] == '\n') { i += 1; continue; }
            // =\r\n is normalized to =\n so above covers it.
            // hex form =XX
            if (i + 2 < in.size()) {
                std::string hx = in.substr(i+1, 2);
                // validate hex
                bool ok = true;
                for (char h : hx) if (!isxdigit((unsigned char)h)) { ok = false; break; }
                if (ok) {
                    char v = static_cast<char>(std::stoi(hx, nullptr, 16));
                    out.push_back(v);
                    i += 2;
                    continue;
                }
            }
            // fallback: append '=' literally
            out.push_back('=');
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::string Parser::decodeContent(const std::string &data, const std::string &encoding) {
    std::string enc = toLower(trim(encoding));
    if (enc == "base64") return decodeBase64(data);
    if (enc == "quoted-printable") return decodeQuotedPrintable(data);
    // 7bit/8bit/binary/unknown => return as-is (data should already be bytes/utf8)
    return data;
}

// --- multipart splitting utility ------------------------------------------
// returns parts found between boundaries (excludes preamble and epilogue)
// Updated splitMultipart function
static std::vector<std::string> splitMultipart(const std::string &body, const std::string &boundary) {
    std::vector<std::string> parts;
    if (boundary.empty()) return parts;
    
    std::string boundaryLine = "--" + boundary;
    std::string endBoundaryLine = boundaryLine + "--";
    
    size_t startPos = 0;
    size_t boundaryPos = body.find(boundaryLine, startPos);
    
    // Skip the preamble. The loop should start after the first boundary.
    if (boundaryPos == std::string::npos) {
        return parts;
    }
    
    // Move to the end of the first boundary
    startPos = boundaryPos + boundaryLine.length();
    
    // Skip CRLF after the boundary
    if (startPos < body.size() && body[startPos] == '\r') startPos++;
    if (startPos < body.size() && body[startPos] == '\n') startPos++;
    
    while (true) {
        // First, check for the end boundary
        size_t endBoundaryPos = body.find(endBoundaryLine, startPos);
        
        // Then, check for the next regular boundary
        size_t nextBoundaryPos = body.find(boundaryLine, startPos);
        
        // Determine which boundary is found first
        size_t effectiveBoundaryPos = std::string::npos;
        if (endBoundaryPos != std::string::npos && nextBoundaryPos != std::string::npos) {
            effectiveBoundaryPos = std::min(endBoundaryPos, nextBoundaryPos);
        } else if (endBoundaryPos != std::string::npos) {
            effectiveBoundaryPos = endBoundaryPos;
        } else {
            effectiveBoundaryPos = nextBoundaryPos;
        }
        
        if (effectiveBoundaryPos == std::string::npos) {
            // No more boundaries found
            break;
        }
        
        // Extract the part between startPos and the found boundary
        std::string part = body.substr(startPos, effectiveBoundaryPos - startPos);
        
        // Trim trailing CRLF
        size_t end = part.size();
        while (end > 0 && (part[end-1] == '\r' || part[end-1] == '\n')) {
            end--;
        }
        
        parts.push_back(part.substr(0, end));
        
        // Check if this was the end boundary
        if (effectiveBoundaryPos == endBoundaryPos) {
            break;
        }
        
        // Move to the next boundary
        startPos = effectiveBoundaryPos + boundaryLine.length();
        
        // Skip CRLF after the boundary
        if (startPos < body.size() && body[startPos] == '\r') startPos++;
        if (startPos < body.size() && body[startPos] == '\n') startPos++;
    }
    
    return parts;
}

// Parse an entity: either text/plain, text/html or attachment; recursively handle simple multipart
void Parser::parseTopLevelBody(const std::map<std::string,std::string>& headers,
                               const std::string &body,
                               EmailMessage &out) {
     auto itCT = headers.find("content-type");
    std::string ctype = (itCT != headers.end()) ? toLower(itCT->second) : "text/plain";
    ctype = trim(ctype);

    debugPrint("Processing part with content-type: " + ctype);

    // detect multipart
    if (ctype.find("multipart/") != std::string::npos) {
        debugPrint("Found multipart content");
        
        // Extract boundary using more robust parameter extraction
        std::string boundary = extractParameter(itCT->second, "boundary");
        
        debugPrint("Extracted boundary: " + boundary);
        
        if (boundary.empty()) {
            debugPrint("No boundary found, treating as plain text");
            std::string decoded = decodeContent(body,
                headers.count("content-transfer-encoding") ? headers.at("content-transfer-encoding") : "7bit");
            if (!out.plainTextBody.has_value()) out.plainTextBody = decoded;
            return;
        }
        
        // Remove quotes from boundary if present
        if (boundary.size() >= 2 && boundary.front() == '"' && boundary.back() == '"') {
            boundary = boundary.substr(1, boundary.size() - 2);
        }
        
        debugPrint("Using boundary: " + boundary);
        
        // split into parts
        auto parts = splitMultipart(body, boundary);
        debugPrint("Found " + std::to_string(parts.size()) + " parts in multipart");
        
        for (size_t i = 0; i < parts.size(); ++i) {
            debugPrint("Processing part " + std::to_string(i));
            
            // each part composed of headers + body
            std::string ph, pb;
            splitHeadersBody(parts[i], ph, pb);
            
            // trim leading whitespace of headers (if any)
            ph = trim(ph);
            
            if (ph.empty()) {
                debugPrint("Part " + std::to_string(i) + " has no headers, skipping");
                continue;
            }
            
            auto phdrs = parseHeaders(ph);
            
            // Check if this part has its own content-type
            auto partCTit = phdrs.find("content-type");
            std::string partCT = (partCTit != phdrs.end()) ? toLower(partCTit->second) : "";
            
            debugPrint("Part " + std::to_string(i) + " content-type: " + partCT);
            
            parseTopLevelBody(phdrs, pb, out);
        }
        return;
    }

    // single part: trim leading/trailing newlines and whitespace
    size_t s = 0;
    while (s < body.size() && (body[s] == '\r' || body[s] == '\n')) ++s;
    size_t e = body.size();
    while (e > s && (body[e-1] == '\r' || body[e-1] == '\n')) --e;
    std::string partBody = (s < e) ? body.substr(s, e - s) : std::string();

    // decode per encoding header (if present)
    std::string cte = "7bit";
    auto itCTE = headers.find("content-transfer-encoding");
    if (itCTE != headers.end()) cte = toLower(trim(itCTE->second));
    std::string decoded = decodeContent(partBody, cte);

    debugPrint("Processing single part: " + ctype);

    // decide if text/plain, text/html or attachment
    if (ctype.find("text/plain") != std::string::npos) {
        debugPrint("Found text/plain part");
        if (!out.plainTextBody.has_value()) out.plainTextBody = decoded;
    } else if (ctype.find("text/html") != std::string::npos) {
        debugPrint("Found text/html part");
        if (!out.htmlBody.has_value()) out.htmlBody = decoded;
    } else {
        debugPrint("Found attachment part");
        // attachment (or unknown part) - try to get filename from content-disposition or content-type name param
        BodyPart att;
        att.content = decoded;
        att.contentType = ctype;
        
        // Extract filename from Content-Disposition
        auto itDisp = headers.find("content-disposition");
        if (itDisp != headers.end()) {
            std::string filename = extractParameter(itDisp->second, "filename");
            if (!filename.empty()) {
                att.filename = filename;
            }
        }
        
        // Fallback: try to extract filename from Content-Type
        if (att.filename.empty()) {
            std::string filename = extractParameter(ctype, "name");
            if (!filename.empty()) {
                att.filename = filename;
            }
        }
        
        // If we still don't have a filename, generate a default one
        if (att.filename.empty()) {
            att.filename = "attachment";
            // Try to add extension based on content type
            size_t slashPos = ctype.find('/');
            if (slashPos != std::string::npos) {
                std::string subtype = ctype.substr(slashPos + 1);
                size_t semicolonPos = subtype.find(';');
                if (semicolonPos != std::string::npos) {
                    subtype = subtype.substr(0, semicolonPos);
                }
                att.filename += "." + subtype;
            }
        }
        
        out.attachments.push_back(std::move(att));
    }
}
 std::string Parser::extractSenderName(const std::string& fromHeader) {
    size_t ltPos = fromHeader.find('<');
    if (ltPos == std::string::npos) {
        // No angle brackets found, assume the entire string is the name or email
        // In the format "email@example.com"
        size_t atPos = fromHeader.find('@');
        if (atPos != std::string::npos) {
            return ""; // No name, just an email address
        }
        return fromHeader; // Assume it's a name without an email
    }

    // A name and email were found in the format "Name <email>"
    std::string namePart = fromHeader.substr(0, ltPos);
    return trim(namePart);
}

// --- public parse entry ---------------------------------------------------
EmailMessage Parser::parse(const std::string& rawMessage) {
    EmailMessage out;
    std::string working = rawMessage;
    normalizeNewlines(working);

    std::string headerBlock, bodyBlock;
    splitHeadersBody(working, headerBlock, bodyBlock);
    auto hdrs = parseHeaders(headerBlock);

    auto geth = [&](const std::string &k)->std::string {
        auto it = hdrs.find(toLower(k));
        return it == hdrs.end() ? std::string() : it->second;
    };
    out.from = geth("From");
     out.senderName = extractSenderName(out.from); 
    out.to = geth("To");
    out.cc = geth("Cc");
    out.subject = geth("Subject");
    out.date = geth("Date");
    out.messageId = geth("Message-ID");

    // parse top-level entity (multipart or single)
    parseTopLevelBody(hdrs, bodyBlock, out);
    return out;
}

} // namespace mail