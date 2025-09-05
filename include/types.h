#ifndef TYPES_H
#define TYPES_H

#include <string>
#include <sstream>
#include <vector>
#include <unordered_map>

// Per-connection state
struct ConnState {
    bool inData = false;
    std::string inbuf;                 
    std::ostringstream dataBuffer;
    std::string sender;
    std::vector<std::string> recipients;
    std::string ip;
};

// Worker struct holding epoll fd and connection states
struct Worker {
    int epfd = -1;
    std::unordered_map<int, ConnState> conns;
};

#endif // TYPES_H
