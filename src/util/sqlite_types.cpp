// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#include "util/sqlite_types.hpp"
#include <glog/logging.h>
void SqliteDbCloser::operator()(sqlite3* db) {
    if (!db) { return; }
    int rc = sqlite3_close(db);
    LOG_IF(ERROR, rc != SQLITE_OK) << "sqlite3 close db failed: " << sqlite3_errmsg(db);
}

SqlitePtr open_db(const std::filesystem::path& db_path) {
    auto db_path_str = db_path.string();

    sqlite3* db;
    int rc = sqlite3_open(db_path_str.c_str(), &db);
    CHECK_EQ(rc, SQLITE_OK) << " open db failed: " << sqlite3_errmsg(db);
    return SqlitePtr(db);
}
