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
