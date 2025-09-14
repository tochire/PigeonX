#ifndef POSTGRES_H
#define POSTGRES_H

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <pqxx/pqxx>

class PostgresDB {
public:
    PostgresDB(const std::string &connectionStr);
    ~PostgresDB();

    bool connect();
    void disconnect();
    bool isConnected() const;

    // --- Transaction Methods ---
    void begin();
    void commit();
    void rollback();

    // --- Query Methods ---
    // Executes a query within a transaction.
    pqxx::result execute(const std::string &query);

    // Executes a query with a binary parameter.
    pqxx::result execute(const std::string &query_name, const pqxx::binarystring& binary_content);

      template <typename... Args>
    pqxx::result execute_prepared(const std::string &query_name, const Args&... args);

    // Prepares a statement with a specific name.
    void prepare(const std::string& query_name, const std::string& query_text);
    void init_prepared_statements(); // Add this line
    // Helper to get the ID from a result.
    int getInsertedId(const pqxx::result& result);

    std::vector<std::unordered_map<std::string, std::string>> query(const std::string &query);
    std::string escape(const std::string &input); 

private:
    std::string connStr;
    std::unique_ptr<pqxx::connection> conn;
    std::unique_ptr<pqxx::work> tx;
};
template <typename... Args>
pqxx::result PostgresDB::execute_prepared(const std::string &query_name, const Args&... args) {
    if (!tx) {
        throw std::runtime_error("Cannot execute prepared statement without an active transaction.");
    }
    return tx->exec_prepared(query_name, args...);
}
#endif // POSTGRES_H