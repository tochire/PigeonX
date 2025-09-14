#include "postgres.h"
#include <iostream>
#include <config.h>
#include <stdexcept>

PostgresDB::PostgresDB(const std::string &connectionStr)
    : connStr(connectionStr), conn(nullptr), tx(nullptr) {}

PostgresDB::~PostgresDB() {
    disconnect();
}

bool PostgresDB::connect() {
    if (conn && conn->is_open()) {
        return true;
    }
    std::cout << "DB string: [" << g_config.db_conn_str << "]\n";
    try {
        conn = std::make_unique<pqxx::connection>(connStr);
        if (conn->is_open()) {
            std::cout << "Connected to database successfully." << std::endl;
            return true;
        } else {
            std::cerr << "Failed to open database connection." << std::endl;
            conn.reset();
            return false;
        }
    } catch (const std::exception &e) {
        std::cerr << "Connection error: " << e.what() << std::endl;
        conn.reset();
        return false;
    }
}

void PostgresDB::disconnect() {
    if (conn) {
        conn.reset();
    }
}

bool PostgresDB::isConnected() const {
    return conn && conn->is_open();
}

// --- Transaction Management ---
void PostgresDB::begin() {
    if (!conn) {
        throw std::runtime_error("Cannot begin transaction: not connected to the database.");
    }
    if (tx) {
        throw std::runtime_error("A transaction is already active.");
    }
    tx = std::make_unique<pqxx::work>(*conn);
}

void PostgresDB::commit() {
    if (!tx) {
        throw std::runtime_error("No active transaction to commit.");
    }
    tx->commit();
    tx.reset();
}

void PostgresDB::rollback() {
    if (tx) {
        try {
            tx->abort();
        } catch (const std::exception& e) {
            std::cerr << "Rollback failed: " << e.what() << std::endl;
        }
        tx.reset();
    }
}

// --- Query Execution & Result Handling ---
// New method to prepare a statement
void PostgresDB::prepare(const std::string& query_name, const std::string& query_text) {
    if (!conn) {
        throw std::runtime_error("Cannot prepare statement: not connected to the database.");
    }
    conn->prepare(query_name, query_text);
}

// Executes a standard query.
pqxx::result PostgresDB::execute(const std::string &query) {
    if (!tx) {
        throw std::runtime_error("Cannot execute query without an active transaction.");
    }
    return tx->exec(query);
}

// Executes a query with a binary parameter.
pqxx::result PostgresDB::execute(const std::string &query_name, const pqxx::binarystring& binary_content) {
    if (!tx) {
        throw std::runtime_error("Cannot execute query without an active transaction.");
    }
    return tx->exec_prepared(query_name, binary_content);
}

int PostgresDB::getInsertedId(const pqxx::result& result) {
    if (result.empty() || result[0].empty()) {
        throw std::runtime_error("No result returned for ID.");
    }
    return result[0][0].as<int>();
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
void PostgresDB::init_prepared_statements() {
    if (!conn) {
        throw std::runtime_error("Cannot initialize prepared statements: not connected to the database.");
    }

    // This query is for inserting attachments
    conn->prepare(
        "file_insert",
        "INSERT INTO files (filename, content_type, content) VALUES ($1, $2, $3) RETURNING id;"
    );
}
