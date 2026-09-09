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

#pragma once

#include <filesystem>
#include <memory>
#include <sqlite3.h>
#include <stdexcept>

struct SqliteError : public std::runtime_error {
    SqliteError(sqlite3* db, int _errc)
        : std::runtime_error(sqlite3_errmsg(db))
        , errc(_errc) {}
    int errc;
};

struct SqliteDbCloser {
    void operator()(sqlite3* db);
};

using SqlitePtr = std::unique_ptr<sqlite3, SqliteDbCloser>;

SqlitePtr open_db(const std::filesystem::path& db_path);
