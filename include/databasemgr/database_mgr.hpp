// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "SQLiteCpp/SQLiteCpp.h"
#include <filesystem>
#include <unordered_map>

namespace database_mgr {
// get the globally unique ability framework database instance; throws if database is not initialized
SQLite::Database& get_database();
SQLite::Transaction transaction();
SQLite::Statement statement(const std::string& s);
SQLite::Statement statement(const char* s);
namespace _impl {

template <typename Tuple, size_t... Is>
inline void bind_args(SQLite::Statement& stmt, Tuple&& ref_tuple, std::index_sequence<Is...>) {
    ((stmt.bind(Is + 1, std::get<Is>(ref_tuple))), ...);
}
} // namespace _impl
template <typename... Ts>
inline void exec(const char* stmt_str, Ts&&... args) {
    auto stmt = statement(stmt_str);
    auto ref_tuple = std::forward_as_tuple(std::forward<Ts>(args)...);
    constexpr int total_args = sizeof...(Ts);
    if (total_args == 0) {
        stmt.exec();
        return;
    }
    _impl::bind_args(stmt, ref_tuple, std::make_index_sequence<total_args>{});

    stmt.exec();
}
template <typename... Ts>
inline void exec(const std::string& stmt_str, Ts&&... args) {
    exec(stmt_str.c_str(), std::forward<Ts>(args)...);
}

void init_database(std::filesystem::path filename);
// close the database when the program exits
void close_database();

} // namespace database_mgr
// place namespace helper functions that can be found via ADL
namespace SQLite {

/// @brief execute stmt and convert the result to a string dictionary
/// @param stmt must have two columns, both in string format, otherwise behavior is undefined
std::unordered_map<std::string, std::string> execute_to_kvmap_unordered(SQLite::Statement& stmt);
} // namespace SQLite
