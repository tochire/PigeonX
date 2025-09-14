#ifndef PARSER_H
#define PARSER_H

#include <string>
#include <vector>
#include <optional>
#include <map>

namespace mail {

struct BodyPart {
    std::string filename;
    std::string content; // decoded content
    std::string contentType; // added content type field
};

struct EmailMessage {
    std::string from;
    std::string to;
    std::string cc;
    std::string subject;
    std::string date;
    std::string messageId;
    std::optional<std::string> plainTextBody;
    std::optional<std::string> htmlBody;
    std::vector<BodyPart> attachments;
};

class Parser {
public:
    static EmailMessage parse(const std::string& rawMessage);

private:
    static void normalizeNewlines(std::string &s);
    static void splitHeadersBody(const std::string &raw, std::string &headers, std::string &body);
    static std::map<std::string,std::string> parseHeaders(const std::string &headerBlock);
    static void parseTopLevelBody(const std::map<std::string,std::string>& headers,
                                  const std::string &body,
                                  EmailMessage &out);
    static std::string decodeContent(const std::string &data, const std::string &encoding);
    static std::string decodeBase64(const std::string &in);
    static std::string decodeQuotedPrintable(const std::string &in);
    static std::string trim(const std::string &s);
    static std::string toLower(const std::string &s);
    
    // New helper functions
    static std::string decodeHeaderValue(const std::string &value);
    static std::string extractParameter(const std::string &headerValue, const std::string &paramName);
    
    // Debug function to help identify parsing issues
    static void debugPrint(const std::string& message, const std::string& content = "");
};

} // namespace mail

#endif