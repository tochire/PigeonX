#pragma once
#include <pqxx/pqxx>
#include <string>
#include <vector>
#include <unordered_map>

class PostgresDB {
public:
    // Constructor & Destructor
    PostgresDB(const std::string &connectionStr);
    ~PostgresDB();

    // Connection
    bool connect();
    void disconnect();
    bool isConnected() const;

    // Query execution
    bool execute(const std::string &query);
    
    // Fetch all rows (returns vector of maps: column name -> value)
    std::vector<std::unordered_map<std::string, std::string>> query(const std::string &query);

    // Escape strings
    std::string escape(const std::string &input);

private:
    std::string connStr;
    pqxx::connection* conn;
};
