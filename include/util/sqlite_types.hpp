// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

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
