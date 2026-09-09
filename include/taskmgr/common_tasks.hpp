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

#include "taskmgr/task.hpp"
#include "util/expected.hpp"
#include <functional>

namespace tasks {
TaskPtr atomic(std::string_view name, std::function<void()> f);           // 原子任务
TaskPtr atomic(std::string_view name, std::function<nlohmann::json()> f); // 带返回值的原子任务
TaskPtr atomic_with_return_value(std::string_view name, std::function<nlohmann::json()> f);
TaskPtr sequence(std::string_view name, std::vector<TaskPtr> tasks); // 顺序任务
// 并行执行所有任务,只有所有任务完成,这个总任务才算完成,一旦有一个任务失败,总体就失败.
TaskPtr parallel(std::string_view name, std::vector<TaskPtr> tasks);
template <typename... Ts>
inline TaskPtr sequence(std::string_view name = "", Ts&&... subtasks) {
    std::vector<TaskPtr> tasks;
    ((void)tasks.push_back(std::forward<Ts>(subtasks)), ...);
    return sequence(name, std::move(tasks));
}
TaskPtr wait_once(
    std::string_view _name,
    std::function<bool()> condition,                 // 条件
    std::chrono::steady_clock::duration max_timeout, // 最大等待时间
    std::function<std::string()> _on_timeout = {}    // 超时处理
);
TaskPtr on_thread_queue(std::string_view name, std::function<void()> f);
TaskPtr delay(std::chrono::steady_clock::duration duration);
}; // namespace tasks

// 提交任务到TaskMgr
expected<uuids::uuid, std::string> submit_task(TaskPtr task, std::string_view who);

template <typename T>
struct submit_task_as {
    T self_module_name;
    expected<uuids::uuid, std::string> operator()(TaskPtr tptr) const {
        return submit_task(tptr, std::string_view(self_module_name));
    }
};
