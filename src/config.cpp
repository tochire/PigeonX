#include "config.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <unordered_map>
#include <algorithm>

// Default config values
Config g_config = {
    2525,   // port
    10,     // backlog
    64,     // max_events
    4,      // workers
    4096,   // buf_sz
    "postgresql://user:password@localhost:5432/mydb" // db_conn_str
};

static inline std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

bool load_config(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Config file not found, using defaults.\n";
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue; // Skip comments

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));

        if (key == "port")        g_config.port        = std::stoi(value);
        else if (key == "backlog")     g_config.backlog     = std::stoi(value);
        else if (key == "max_events")  g_config.max_events  = std::stoi(value);
        else if (key == "workers")     g_config.workers     = std::stoi(value);
        else if (key == "buf_sz")      g_config.buf_sz      = std::stoi(value);
        else if (key == "db_conn_str") g_config.db_conn_str = value;
    }
    g_config.db_conn_str.erase(
    g_config.db_conn_str.find_last_not_of(" \r\n\t") + 1
);

    return true;
}
