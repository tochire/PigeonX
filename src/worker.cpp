#include "worker.h"
#include "string_manipulation.h"
#include <sys/epoll.h>
#include <unistd.h>
#include <iostream>
#include <cerrno>
#include <netinet/in.h>
#include <types.h>
#include <config.h>
#include <fcntl.h>

void handle_readable(Worker& w, int fd) {
    char buf[g_config.buf_sz];
    while (true) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n > 0) {
            auto& st = w.conns[fd];
            st.inbuf.append(buf, buf + n);
            size_t pos = 0;
            while (true) {
                size_t eol = st.inbuf.find('\n', pos);
                if (eol == std::string::npos) { st.inbuf.erase(0, pos); break; }
                std::string line = w.conns[fd].inbuf.substr(pos, eol - pos + 1);
                pos = eol + 1;
                std::string log_line = line;
                rstrip_crlf(log_line);
                std::cout << "C: " << log_line << std::endl;
                process_smtp_line(st, fd, line);
            }
        } else if (n == 0) {
            epoll_ctl(w.epfd, EPOLL_CTL_DEL, fd, nullptr);
            close(fd);
            w.conns.erase(fd);
            break;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            else { epoll_ctl(w.epfd, EPOLL_CTL_DEL, fd, nullptr); close(fd); w.conns.erase(fd); break; }
        }
    }
}

void worker_loop(Worker* wptr, int id) {
    Worker& w = *wptr;
    std::vector<epoll_event> events(g_config.max_events);
    while (true) {
        int n = epoll_wait(w.epfd, events.data(), g_config.max_events, -1);
        if (n < 0) { if (errno == EINTR) continue; perror("epoll_wait"); break; }
        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            uint32_t ev = events[i].events;
            if (ev & (EPOLLHUP | EPOLLERR)) { epoll_ctl(w.epfd, EPOLL_CTL_DEL, fd, nullptr); close(fd); w.conns.erase(fd); continue; }
            if (ev & EPOLLIN) handle_readable(w, fd);
        }
    }
}

// Make fd non-blocking
 int make_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
