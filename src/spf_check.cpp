#include "spf_check.h"
#include <resolv.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <cstring>
#include <iostream>
#include <vector>
#include <sstream>
#include <regex>
#include <unordered_set>
#include <algorithm>

// Helper: Check if a string is a valid IP address
static bool is_valid_ip(const std::string& ip) {
    struct sockaddr_in sa4;
    struct sockaddr_in6 sa6;
    return inet_pton(AF_INET, ip.c_str(), &sa4) == 1 || 
           inet_pton(AF_INET6, ip.c_str(), &sa6) == 1;
}

// Helper: Extract all TXT records for a domain
static std::vector<std::string> get_txt_records(const std::string& domain) {
    std::vector<std::string> records;
    unsigned char answer[4096];
    
    int len = res_query(domain.c_str(), C_IN, ns_t_txt, answer, sizeof(answer));
    if (len < 0) return records;

    ns_msg handle;
    if (ns_initparse(answer, len, &handle) < 0) return records;

    int count = ns_msg_count(handle, ns_s_an);
    for (int i = 0; i < count; i++) {
        ns_rr rr;
        if (ns_parserr(&handle, ns_s_an, i, &rr) == 0 && ns_rr_type(rr) == ns_t_txt) {
            const unsigned char* rdata = ns_rr_rdata(rr);
            int rdlen = ns_rr_rdlen(rr);
            std::string txt;
            
            // TXT records can have multiple character strings
            int pos = 0;
            while (pos < rdlen) {
                int segment_len = rdata[pos++];
                if (pos + segment_len > rdlen) break;
                txt.append(reinterpret_cast<const char*>(rdata + pos), segment_len);
                pos += segment_len;
            }
            
            records.push_back(txt);
        }
    }
    
    return records;
}

// Helper: Get SPF record from TXT records
static std::string get_spf_record(const std::string& domain) {
    auto records = get_txt_records(domain);
    for (const auto& record : records) {
        if (record.find("v=spf1") == 0) {
            return record;
        }
    }
    return "";
}

// Helper: Check if IP matches CIDR range
static bool match_cidr(const std::string& ip, const std::string& cidr) {
    size_t slash_pos = cidr.find('/');
    std::string network = cidr.substr(0, slash_pos);
    int prefix_len = 32; // Default for IPv4
    
    if (slash_pos != std::string::npos) {
        prefix_len = std::stoi(cidr.substr(slash_pos + 1));
    }
    
    // Check if IP is IPv4 or IPv6
    if (ip.find(':') != std::string::npos) {
        // IPv6
        struct in6_addr ip_addr, network_addr;
        if (inet_pton(AF_INET6, ip.c_str(), &ip_addr) != 1 ||
            inet_pton(AF_INET6, network.c_str(), &network_addr) != 1) {
            return false;
        }
        
        // Compare the network portion
        int bytes = prefix_len / 8;
        int bits = prefix_len % 8;
        
        for (int i = 0; i < bytes; i++) {
            if (ip_addr.s6_addr[i] != network_addr.s6_addr[i]) {
                return false;
            }
        }
        
        if (bits > 0) {
            uint8_t mask = 0xFF << (8 - bits);
            if ((ip_addr.s6_addr[bytes] & mask) != (network_addr.s6_addr[bytes] & mask)) {
                return false;
            }
        }
        
        return true;
    } else {
        // IPv4
        struct in_addr ip_addr, network_addr;
        if (inet_pton(AF_INET, ip.c_str(), &ip_addr) != 1 ||
            inet_pton(AF_INET, network.c_str(), &network_addr) != 1) {
            return false;
        }
        
        uint32_t mask = prefix_len == 0 ? 0 : htonl(~((1 << (32 - prefix_len)) - 1));
        return (ip_addr.s_addr & mask) == (network_addr.s_addr & mask);
    }
}

// Helper: Resolve domain to IP addresses
static std::vector<std::string> resolve_domain(const std::string& domain) {
    std::vector<std::string> ips;
    struct addrinfo hints, *res, *p;
    
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; // IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;
    
    if (getaddrinfo(domain.c_str(), NULL, &hints, &res) != 0) {
        return ips;
    }
    
    for (p = res; p != NULL; p = p->ai_next) {
        char ipstr[INET6_ADDRSTRLEN];
        void *addr;
        
        if (p->ai_family == AF_INET) {
            struct sockaddr_in *ipv4 = (struct sockaddr_in *)p->ai_addr;
            addr = &(ipv4->sin_addr);
        } else {
            struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)p->ai_addr;
            addr = &(ipv6->sin6_addr);
        }
        
        inet_ntop(p->ai_family, addr, ipstr, sizeof ipstr);
        ips.push_back(ipstr);
    }
    
    freeaddrinfo(res);
    return ips;
}

// Helper: Get MX records for a domain
static std::vector<std::string> get_mx_records(const std::string& domain) {
    std::vector<std::string> mx_hosts;
    unsigned char answer[4096];
    
    int len = res_query(domain.c_str(), C_IN, ns_t_mx, answer, sizeof(answer));
    if (len < 0) return mx_hosts;
    
    ns_msg handle;
    if (ns_initparse(answer, len, &handle) < 0) return mx_hosts;
    
    int count = ns_msg_count(handle, ns_s_an);
    for (int i = 0; i < count; i++) {
        ns_rr rr;
        if (ns_parserr(&handle, ns_s_an, i, &rr) == 0 && ns_rr_type(rr) == ns_t_mx) {
            char mxname[NS_MAXDNAME];
            const unsigned char* rdata = ns_rr_rdata(rr);
            // Skip preference value (2 bytes)
            dn_expand(ns_msg_base(handle), ns_msg_end(handle), rdata + 2, mxname, sizeof(mxname));
            mx_hosts.push_back(mxname);
        }
    }
    
    return mx_hosts;
}

// Helper: Parse mechanism and qualifier
static void parse_mechanism(const std::string& token, char& qualifier, std::string& mechanism) {
    if (token.empty()) {
        qualifier = '+';
        mechanism = "";
        return;
    }
    
    // Check for qualifier
    if (token[0] == '+' || token[0] == '-' || token[0] == '~' || token[0] == '?') {
        qualifier = token[0];
        mechanism = token.substr(1);
    } else {
        qualifier = '+'; // Default qualifier is pass
        mechanism = token;
    }
}

// Recursive SPF check with depth limiting and cycle detection
static bool eval_spf(const std::string& domain, const std::string& ip, 
                    int depth, std::unordered_set<std::string>& visited) {
    if (depth > 10) return false; // Avoid infinite recursion
    if (visited.find(domain) != visited.end()) return false; // Avoid cycles
    visited.insert(domain);
    
    std::string spf_record = get_spf_record(domain);
    if (spf_record.empty()) return false;
    
    // Check for redirect modifier first
    std::smatch match;
    if (std::regex_search(spf_record, match, std::regex("redirect=([^\\s]+)"))) {
        std::string redirect_domain = match[1].str();
        return eval_spf(redirect_domain, ip, depth + 1, visited);
    }
    
    std::istringstream iss(spf_record);
    std::string token;
    
    // Skip version part
    iss >> token; // v=spf1
    
    bool has_all = false;
    char all_qualifier = '+';
    
    while (iss >> token) {
        // Skip modifiers (other than redirect which we handled above)
        if (token.find("=") != std::string::npos) {
            continue;
        }
        
        char qualifier;
        std::string mechanism;
        parse_mechanism(token, qualifier, mechanism);
        
        if (mechanism == "all") {
            has_all = true;
            all_qualifier = qualifier;
            continue; // Evaluate all at the end
        }
        
        bool match_found = false;
        
        if (mechanism.find("ip4:") == 0) {
            std::string cidr = mechanism.substr(4);
            match_found = match_cidr(ip, cidr);
        } else if (mechanism.find("ip6:") == 0) {
            std::string cidr = mechanism.substr(4);
            match_found = match_cidr(ip, cidr);
        } else if (mechanism.find("a:") == 0) {
            std::string target_domain = mechanism.substr(2);
            size_t slash_pos = target_domain.find('/');
            if (slash_pos != std::string::npos) {
                target_domain = target_domain.substr(0, slash_pos);
            }
            
            auto ips = resolve_domain(target_domain);
            for (const auto& resolved_ip : ips) {
                if (resolved_ip == ip) {
                    match_found = true;
                    break;
                }
            }
        } else if (mechanism == "a") {
            auto ips = resolve_domain(domain);
            for (const auto& resolved_ip : ips) {
                if (resolved_ip == ip) {
                    match_found = true;
                    break;
                }
            }
        } else if (mechanism.find("mx:") == 0) {
            std::string target_domain = mechanism.substr(3);
            auto mx_hosts = get_mx_records(target_domain);
            for (const auto& host : mx_hosts) {
                auto ips = resolve_domain(host);
                for (const auto& resolved_ip : ips) {
                    if (resolved_ip == ip) {
                        match_found = true;
                        break;
                    }
                }
                if (match_found) break;
            }
        } else if (mechanism == "mx") {
            auto mx_hosts = get_mx_records(domain);
            for (const auto& host : mx_hosts) {
                auto ips = resolve_domain(host);
                for (const auto& resolved_ip : ips) {
                    if (resolved_ip == ip) {
                        match_found = true;
                        break;
                    }
                }
                if (match_found) break;
            }
        } else if (mechanism.find("include:") == 0) {
            std::string include_domain = mechanism.substr(8);
            match_found = eval_spf(include_domain, ip, depth + 1, visited);
        } else if (mechanism.find("exists:") == 0) {
            std::string test_domain = mechanism.substr(7);
            // For exists, we just check if the domain resolves
            match_found = !resolve_domain(test_domain).empty();
        }
        // Note: ptr mechanism is not recommended and often disabled
        
        if (match_found) {
            // Apply qualifier
            switch (qualifier) {
                case '+': return true;  // Pass
                case '-': return false; // Fail
                case '~': return false; // Softfail (treated as false for strict checking)
                case '?': return false; // Neutral (treated as false)
            }
        }
    }
    
    // If we get here, no mechanism matched, so use the all mechanism if present
    if (has_all) {
        switch (all_qualifier) {
            case '+': return true;  // Pass
            case '-': return false; // Fail
            case '~': return false; // Softfail
            case '?': return false; // Neutral
        }
    }
    
    return false; // Default to fail if no mechanism matches and no all mechanism
}

namespace spf {
    bool spf_allows(const std::string& domain, const std::string& ip) {
        if (!is_valid_ip(ip)) {
            return false;
        }
        
        std::unordered_set<std::string> visited;
        return eval_spf(domain, ip, 0, visited);
    }
}