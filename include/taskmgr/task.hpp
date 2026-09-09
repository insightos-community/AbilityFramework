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

#include "util/make_uuid.hpp"
#include <memory>
#include <nlohmann/json.hpp>
#include <utility>
#include <uuid.h>
#include <uvw/idle.h>

enum class TaskState {
    unstarted, // 尚未运行
    running,   // 正在运行
    pending,   // 等待子能力或外部操作完成
    fine,      // 调整过程处于最佳状态(不过日后仍有可能偏离)
    finished,  // 执行成功结束,这个Task可以放心删除了
    error,     // 遇到错误,无法执行
    cancelled  // 被取消了,不算一个过错,但是会终止执行
};

std::string to_string(TaskState);
TaskState task_state_from_string(std::string_view);

NLOHMANN_JSON_SERIALIZE_ENUM(
    TaskState,
    {
        {TaskState::unstarted, "unstarted"},
        {TaskState::running, "running"},
        {TaskState::pending, "pending"},
        {TaskState::fine, "fine"},
        {TaskState::finished, "finished"},
        {TaskState::error, "error"},
        {TaskState::cancelled, "cancelled"},

    }
)

std::ostream& operator<<(std::ostream& o, TaskState);

inline bool is_end(TaskState x) { // 检查任务是否完成
    using S = TaskState;
    return x == S::finished || x == S::error || x == S::cancelled;
}

// 这些任务是一次性的,它们表示单次任务
struct TaskInterface {
    [[nodiscard]] virtual uuids::uuid id() const = 0;
    [[nodiscard]] virtual TaskState state() const = 0;
    [[nodiscard]] virtual std::string name() const { return "unknown"; }

    // 任务的种类
    [[nodiscard]] virtual std::string kind() const { return "unknown"; }
    // 如果成功,返回任务结果
    // 如果正在执行,此返回值无意义
    [[nodiscard]] virtual nlohmann::json result() const { return {}; }
    // 返回详细的任务状态
    [[nodiscard]] virtual nlohmann::json status() const { return {}; }
    [[nodiscard]] virtual std::exception_ptr error() const { return nullptr; };
    // 使任务前进一轮,直到成功,失败或者等待某一条件
    // 返回前进一轮后该任务的状态
    virtual TaskState resume() { return TaskState::finished; };
    virtual ~TaskInterface() = default;
};

struct TaskBase : virtual public TaskInterface {
    uuids::uuid id() const override { return uuid_; }
    TaskState state() const override { return state_; }
    std::string name() const override { return "unknown"; }

    // 任务的种类
    std::string kind() const override { return "unknown"; }
    // 如果成功,返回任务结果
    // 如果正在执行,此返回值无意义
    nlohmann::json result() const override { return {}; }
    // 返回详细的任务状态
    nlohmann::json status() const override { return {}; }
    std::exception_ptr error() const override { return exception_; }
    // 使任务前进一轮,直到成功,失败或者等待某一条件
    // 返回前进一轮后该任务的状态
    TaskState resume() override { return TaskState::finished; };

    auto start_time() const { return start_time_; }
    auto end_time() const { return end_time_; }

    std::string message() const {
        if (!exception_) { return ""; }
        try {
            std::rethrow_exception(exception_);
        }
        catch (std::exception& e) {
            return e.what();
        }
    }

protected:
    uuids::uuid uuid_ = make_uuid();
    TaskState set_state(TaskState s) {
        state_ = s;
        if (is_end(s)) { end_time_ = Clock::now(); }
        return s;
    }
    TaskState set_error(std::exception_ptr eptr) {
        exception_ = std::move(eptr);
        state_ = TaskState::error;
        end_time_ = Clock::now();
        return state_;
    }

    auto record_start_time() { start_time_ = Clock::now(); }

private:
    std::exception_ptr exception_;
    TaskState state_ = TaskState::unstarted;
    using Clock = std::chrono::system_clock;
    std::chrono::system_clock::time_point start_time_{};
    std::chrono::system_clock::time_point end_time_{};
};

// 任务指针
using TaskPtr = std::shared_ptr<TaskInterface>;

/// 根据json内容解析输入,并创建任务
/// 如果任务参数不合法, 抛出 std::invalid_argument;
using TaskFactory = std::function<TaskPtr(const nlohmann::json&)>;
namespace task_mgr {
void add_task_factory(const std::string& task_type, TaskFactory task_factory);
}
