#include "string_manipulation.h"
#include <regex>
#include <cctype>

std::string getEmailDomain(const std::string& email) {
    // Basic regex for email validation
    static const std::regex pattern(
        R"(^[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}$)"
    );

    if (std::regex_match(email, pattern)) {
        // Extract the domain part after '@'
        size_t atPos = email.find('@');
        if (atPos != std::string::npos && atPos + 1 < email.size()) {
            return email.substr(atPos + 1);
        }
    }
    return ""; // invalid email
}

std::string extract_sender(const std::string& line) {
    // Find start of "<"
    size_t start = line.find('<');
    // Find end of ">"
    size_t end = line.find('>', start);

    if (start != std::string::npos && end != std::string::npos && end > start) {
        return line.substr(start + 1, end - start - 1); // sender inside <>
    }

    // fallback: try after "MAIL FROM:"
    if (line.size() > 10) {
        std::string fallback = line.substr(10);
        // trim leading/trailing spaces
        fallback.erase(0, fallback.find_first_not_of(" \t\r\n"));
        fallback.erase(fallback.find_last_not_of(" \t\r\n") + 1);
        return fallback;
    }

    return "";
}

    void rstrip_crlf(std::string& s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back();
}