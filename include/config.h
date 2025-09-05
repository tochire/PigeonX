#ifndef CONFIG_H
#define CONFIG_H

#include <string>

// Struct holding all config values
struct Config {
    int port;
    int backlog;
    int max_events;
    int workers;
    int buf_sz;
    std::string db_conn_str;
};

// Global instance accessible everywhere
extern Config g_config;

// Loads config file, returns true if successful
bool load_config(const std::string& filename);

#endif // CONFIG_H
