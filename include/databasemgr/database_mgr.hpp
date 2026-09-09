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

#include "SQLiteCpp/SQLiteCpp.h"
#include <filesystem>
#include <unordered_map>

namespace database_mgr {
// 获取全局唯一的能力框架数据库实例, 如果数据库未初始化,则抛出异常
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
// 在程序结束时关闭数据库
void close_database();

} // namespace database_mgr
// 放置一些命名空间辅助函数,它们可以被ADL查找
namespace SQLite {

/// @brief 执行stmt ,将结果转换为字符串字典
/// @param stmt 须有两列, 且都为字符串格式, 否则行为未定义
std::unordered_map<std::string, std::string> execute_to_kvmap_unordered(SQLite::Statement& stmt);
} // namespace SQLite
