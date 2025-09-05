#ifndef SMTP_LOGIC_H
#define SMTP_LOGIC_H

#include <string>
#include "string_manipulation.h"
#include "postgres.h"
#include <mutex>
#include <sstream>
#include <vector>

struct ConnState; // forward declaration

extern std::mutex g_fileMutex;
extern PostgresDB* g_db;

void send_line(int fd, const std::string& s);
void process_smtp_line(ConnState& st, int fd, const std::string& raw);

#endif // SMTP_LOGIC_H
