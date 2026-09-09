// Copyright 2026 InsightOS
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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

// 放置一些命名空间辅助函数,它们可以被ADL查找
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
