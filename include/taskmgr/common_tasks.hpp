// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "taskmgr/task.hpp"
#include "util/expected.hpp"
#include <functional>

namespace tasks {
TaskPtr atomic(std::string_view name, std::function<void()> f); // atomic task
TaskPtr atomic(std::string_view name, std::function<nlohmann::json()> f); // atomic task with return value
TaskPtr atomic_with_return_value(std::string_view name, std::function<nlohmann::json()> f);
TaskPtr sequence(std::string_view name, std::vector<TaskPtr> tasks); // sequential task
// execute all tasks in parallel; the overall task completes only when all sub-tasks finish; if any sub-task fails, the overall task fails.
TaskPtr parallel(std::string_view name, std::vector<TaskPtr> tasks);
template <typename... Ts>
inline TaskPtr sequence(std::string_view name = "", Ts&&... subtasks) {
    std::vector<TaskPtr> tasks;
    ((void)tasks.push_back(std::forward<Ts>(subtasks)), ...);
    return sequence(name, std::move(tasks));
}
TaskPtr wait_once(
    std::string_view _name,
    std::function<bool()> condition, // condition
    std::chrono::steady_clock::duration max_timeout, // max wait time
    std::function<std::string()> _on_timeout = {} // timeout handling
);
TaskPtr on_thread_queue(std::string_view name, std::function<void()> f);
TaskPtr delay(std::chrono::steady_clock::duration duration);
}; // namespace tasks

// submit task to TaskMgr
expected<uuids::uuid, std::string> submit_task(TaskPtr task, std::string_view who);

template <typename T>
struct submit_task_as {
    T self_module_name;
    expected<uuids::uuid, std::string> operator()(TaskPtr tptr) const {
        return submit_task(tptr, std::string_view(self_module_name));
    }
};
