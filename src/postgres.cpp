#include "postgres.h"
#include <iostream>
#include <config.h>

PostgresDB::PostgresDB(const std::string &connectionStr)
    : connStr(connectionStr), conn(nullptr) {}

PostgresDB::~PostgresDB() {
    disconnect();
}

bool PostgresDB::connect() {
 std::cout << "DB string: [" << g_config.db_conn_str << "]\n";
    try {
        conn = new pqxx::connection(connStr);
        if (conn->is_open()) {
            std::cout << "Connected to database successfully." << std::endl;
            return true;
        } else {
            std::cerr << "Failed to open database connection." << std::endl;
            return false;
        }
    } catch (const std::exception &e) {
        std::cerr << "Connection error: " << e.what() << std::endl;
        return false;
    }
}

void PostgresDB::disconnect() {
    if (conn) {
        delete conn;  // This will close the connection
        conn = nullptr;
    }
}


bool PostgresDB::isConnected() const {
    return conn && conn->is_open();
}

bool PostgresDB::execute(const std::string &query) {
    if (!isConnected()) return false;
    try {
        pqxx::work txn(*conn);
        txn.exec(query);
        txn.commit();
        return true;
    } catch (const std::exception &e) {
        std::cerr << "Query execution error: " << e.what() << std::endl;
        return false;
    }
}

std::vector<std::unordered_map<std::string, std::string>> PostgresDB::query(const std::string &query) {
    std::vector<std::unordered_map<std::string, std::string>> results;
    if (!isConnected()) return results;

    try {
        pqxx::nontransaction txn(*conn);
        pqxx::result r = txn.exec(query);

        for (const auto &row : r) {
            std::unordered_map<std::string, std::string> rowMap;
            for (const auto &field : row) {
                rowMap[field.name()] = field.c_str() ? field.c_str() : "";
            }
            results.push_back(rowMap);
        }
    } catch (const std::exception &e) {
        std::cerr << "Query fetch error: " << e.what() << std::endl;
    }

    return results;
}

std::string PostgresDB::escape(const std::string &input) {
    if (!isConnected()) return "";
    return conn->esc(input);
}
