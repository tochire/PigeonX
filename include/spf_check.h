#ifndef SPF_CHECKER_H
#define SPF_CHECKER_H

#include <string>
#include <vector>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>

namespace spf {
/**
 * @brief Checks if an IP address is allowed to send email for a given domain using SPF.
 *
 * This function performs the following:
 *  - Looks up the SPF record (TXT) of the domain.
 *  - Parses mechanisms: ip4, ip6, include, a, mx, all.
 *  - Recursively resolves "include:" and evaluates.
 *  - Returns true if the given IP is authorized, false otherwise.
 *
 * @param domain The domain name (e.g. "example.com").
 * @param ip The IP address to check (IPv4 or IPv6 as string).
 * @return true if allowed, false otherwise.
 */
bool spf_allows(const std::string &domain, const std::string &ip);
}
#endif // SPF_CHECKER_H
