// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#include "databasemgr/database_mgr.hpp"
#include "glog/logging.h"
#include <optional>
namespace {
std::optional<SQLite::Database> DATABASE;
}

namespace database_mgr {
void init_database(std::filesystem::path filename) {
    if (DATABASE.has_value()) { throw std::runtime_error("database already initialized"); }
    auto filename_str = filename.string();
    LOG(INFO) << "init database at " << filename_str;
    DATABASE = SQLite::Database(filename_str, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    // WAL gives us concurrent reader + single writer instead of one-at-a-time
    // with whole-DB locks. Without this, an HTTP read (e.g. launcher TUI
    // probing /api/skill once a second) racing with the periodic update()
    // writer surfaces as "database is locked" SQLITE_BUSY errors back to
    // the HTTP client.
    DATABASE->exec("PRAGMA journal_mode = WAL");
    // If a writer does still grab an exclusive lock briefly (schema change,
    // checkpoint, etc.), wait up to 5 s for it to release before failing.
    // 5 s is well under any HTTP client timeout but plenty for normal
    // contention.
    DATABASE->exec("PRAGMA busy_timeout = 5000");
    // synchronous=NORMAL is the recommended default with WAL: durable
    // across crashes, much faster than FULL, and avoids extra fsyncs
    // that can stall reads.
    DATABASE->exec("PRAGMA synchronous = NORMAL");
}

void close_database() {
    if (!DATABASE) { return; }
    LOG(INFO) << "close database";
    DATABASE.reset();
    LOG(INFO) << "database closed";
}

SQLite::Database& get_database() {
    if (!DATABASE.has_value()) { throw std::runtime_error("database used before initialized"); }
    return *DATABASE;
}

SQLite::Statement statement(const std::string& s) {
    if (!DATABASE.has_value()) { throw std::runtime_error("database used before initialized"); }
    return SQLite::Statement(*DATABASE, s);
}

SQLite::Statement statement(const char* s) {
    if (!DATABASE.has_value()) { throw std::runtime_error("database used before initialized"); }
    return SQLite::Statement(*DATABASE, s);
}
SQLite::Transaction transaction() {
    if (!DATABASE.has_value()) { throw std::runtime_error("database used before initialized"); }
    return SQLite::Transaction(*DATABASE);
}

}; // namespace database_mgr

// place namespace helper functions that can be found via ADL
namespace SQLite {

std::unordered_map<std::string, std::string> execute_to_kvmap_unordered(SQLite::Statement& stmt) {
    std::unordered_map<std::string, std::string> res;
    while (stmt.executeStep()) {
        std::string key = stmt.getColumn(0);
        std::string value = stmt.getColumn(1);
        res[key] = std::move(value);
    }
    return res;
}

} // namespace SQLite
