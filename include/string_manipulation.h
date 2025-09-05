#ifndef STRING_MANIPULATION_H
#define STRING_MANIPULATION_H

#include <string>

// Extracts the domain from an email if valid, otherwise returns ""
std::string getEmailDomain(const std::string& email);

// Extracts the sender email from an SMTP line
std::string extract_sender(const std::string& line);

//remove crlf from lines
void rstrip_crlf(std::string& s);

#endif // EMAIL_MANIPULATION_H
