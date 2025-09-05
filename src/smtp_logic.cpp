#include "smtp_logic.h"
#include "string_manipulation.h"
#include <iostream>
#include <sstream>
#include <regex>
#include <sys/socket.h>
#include <unistd.h>
#include "spf_check.h"

std::mutex g_fileMutex;


struct ConnState {
    bool inData = false;
    std::string inbuf;                 // accumulate bytes; parse by CRLF
    std::ostringstream dataBuffer;
    std::string sender;
    std::vector<std::string> recipients;
    std::string ip;
};

void send_line(int fd, const std::string& s) {
    std::cout << "S: " << s << std::endl;
    std::string out = s + "\r\n";
    ssize_t n = send(fd, out.data(), out.size(), 0);
    (void)n;
}

void process_smtp_line(ConnState& st, int fd, const std::string& raw) {
    std::string line = raw;
    rstrip_crlf(line);

    // DATA state
    if (st.inData) {
        if (line == ".") {
            st.inData = false;

            std::string escSender = g_db->escape(st.sender);
            std::ostringstream recArray;
            recArray << "{";
            for (size_t i = 0; i < st.recipients.size(); ++i) {
                if (i > 0) recArray << ",";
                recArray << "\"" << g_db->escape(st.recipients[i]) << "\"";
            }
            recArray << "}";
            std::string escBody = g_db->escape(st.dataBuffer.str());
            std::ostringstream q;
            q << "INSERT INTO emails (sender, recipients, body) "
              << "VALUES ('" << escSender << "', '" << recArray.str() << "', '" << escBody << "');";

            if (!g_db->execute(q.str())) {
                std::cerr << "Failed to insert email into database\n";
            }

            st.dataBuffer.str("");
            st.dataBuffer.clear();
            st.recipients.clear();
            st.sender.clear();

            send_line(fd, "250 2.0.0 OK: Message accepted");
        } else {
            st.dataBuffer << line << "\n";
        }
        return;
    }

    // Normal SMTP commands
    if (line.rfind("HELO", 0) == 0 || line.rfind("EHLO", 0) == 0) {
        std::string client_name = "unknown";
        size_t space_pos = line.find(' ');
        if (space_pos != std::string::npos) client_name = line.substr(space_pos + 1);

        send_line(fd, "250-mx.benamor.pro Hello " + client_name);
        send_line(fd, "250-SIZE 35882577");
        send_line(fd, "250-8BITMIME");
        send_line(fd, "250-PIPELINING");
        send_line(fd, "250 HELP");

    } else if (line.rfind("MAIL FROM:", 0) == 0) {
        std::string sender = extract_sender(line);
        std::string domain = getEmailDomain(sender);
        if (domain.empty()) { send_line(fd,"501 Incorrect email format"); return; }
        bool spf_allowed = spf::spf_allows(domain, st.ip);
        if (!spf_allowed) { send_line(fd,"550 5.7.1 Access denied: invalid sender"); return; }
        st.sender = sender;
        send_line(fd,"250 OK");

    } else if (line.rfind("RCPT TO:", 0) == 0) {
        st.recipients.push_back(line.substr(8));
        send_line(fd, "250 OK");

    } else if (line == "DATA") {
        if (st.sender.empty() || st.recipients.empty()) send_line(fd, "503 Bad sequence of commands");
        else { send_line(fd, "354 End data with <CR><LF>.<CR><LF>"); st.inData = true; }

    } else if (line == "RSET") {
        st.sender.clear(); st.recipients.clear(); st.dataBuffer.str(""); st.dataBuffer.clear(); st.inData = false;
        send_line(fd, "250 OK");

    } else if (line == "NOOP") send_line(fd, "250 OK");
    else if (line == "VRFY") send_line(fd, "252 Cannot VRFY user, but will accept message");
    else if (line == "HELP") { send_line(fd, "214-Commands supported:"); send_line(fd, "214 HELO EHLO MAIL RCPT DATA RSET NOOP QUIT HELP VRFY"); }
    else if (line == "QUIT") { send_line(fd, "221 Bye"); shutdown(fd, SHUT_RDWR); }
    else if (!line.empty()) send_line(fd, "502 Command not implemented");
}
