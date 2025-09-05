// g++ -std=c++17 -O2 smtp_epoll_threads.cpp -o smtp_epoll_threads
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fstream>
#include <atomic>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <sstream>
#include <regex>
#include <optional>
#include <spf_check.h>
#include <postgres.h>
#include <string_manipulation.h>
#include <config.h>
#include <types.h>
#include <smtp_logic.h>
#include <worker.h>




PostgresDB* g_db = nullptr;  // definition, global pointer

int main() {
        load_config("config.conf");
   g_db = new PostgresDB(g_config.db_conn_str); // initialize dynamically
    if (!g_db->connect()) {
        std::cerr << "Fatal: could not connect to Postgres.\n";
        return 1;
    }
    std::cout << "Database connection established.\n";
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); return 1; }
     int one = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(g_config.port);

    if (bind(listen_fd, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
    if (listen(listen_fd, g_config.backlog) < 0) { perror("listen"); return 1; }
    if (make_nonblocking(listen_fd) < 0) { perror("nonblock listen"); return 1; }

    std::cout << "SMTP (epoll) listening on " << g_config.port << " with " << g_config.workers << " workers...\n";

    // Create workers (each has its own epoll instance and state)
    std::vector<Worker> workers(g_config.workers);
    for (int i = 0; i < g_config.workers; ++i) {
        workers[i].epfd = epoll_create1(0);
        if (workers[i].epfd < 0) { perror("epoll_create1"); return 1; }
    }

    std::vector<std::thread> threads;
    threads.reserve(g_config.workers);
    for (int i = 0; i < g_config.workers; ++i) {
        threads.emplace_back(worker_loop, &workers[i], i);
    }

    // Accept loop (main thread) — round-robin assign each new client to a worker
    int next = 0;
    while (true) {
        sockaddr_in cli{};
        socklen_t len = sizeof(cli);
        int cfd = accept(listen_fd, (sockaddr*)&cli, &len);
        if (cfd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No pending connections; small sleep prevents busy loop when nonblocking accept
                usleep(2000);
                continue;
            } else {
                perror("accept");
                break;
            }
        }
        make_nonblocking(cfd);
          char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(cli.sin_addr), ip_str, INET_ADDRSTRLEN);
        // Send banner immediately (non-blocking best-effort)
        send_line(cfd, "220 mx.benamor.pro ESMTP SimpleSMTP");

        epoll_event ev{};
        ev.events = EPOLLIN | EPOLLET; // edge-triggered for efficiency
        ev.data.fd = cfd;

        Worker& w = workers[next];
        if (epoll_ctl(w.epfd, EPOLL_CTL_ADD, cfd, &ev) < 0) {
            perror("epoll_ctl ADD");
            close(cfd);
            continue;
        }
        // Initialize per-connection state map entry
           
        w.conns.emplace(cfd, ConnState{ip:ip_str});

        next = (next + 1) % g_config.workers;
    }

    for (auto& t : threads) t.join();
    close(listen_fd);
    return 0;
}
