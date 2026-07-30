// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "util/make_uuid.hpp"
#include <memory>
#include <nlohmann/json.hpp>
#include <utility>
#include <uuid.h>
#include <uvw/idle.h>

enum class TaskState {
    unstarted, // not started yet
    running, // running
    pending, // waiting for sub-ability or external operation to complete
    fine, // adjustment is in optimal state(though it may deviate later)
    finished, // execution completed successfully,this task can be safely deleted
    error, // encountered an error, cannot execute
    cancelled // cancelled; not a fault, but execution is terminated
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

inline bool is_end(TaskState x) { // check whether the task is complete
    using S = TaskState;
    return x == S::finished || x == S::error || x == S::cancelled;
}

// these tasks are one-shot; they represent single executions
struct TaskInterface {
    [[nodiscard]] virtual uuids::uuid id() const = 0;
    [[nodiscard]] virtual TaskState state() const = 0;
    [[nodiscard]] virtual std::string name() const { return "unknown"; }

    // task kind
    [[nodiscard]] virtual std::string kind() const { return "unknown"; }
    // on success, returns the task result
    // while executing, this return value is meaningless
    [[nodiscard]] virtual nlohmann::json result() const { return {}; }
    // returns detailed task status
    [[nodiscard]] virtual nlohmann::json status() const { return {}; }
    [[nodiscard]] virtual std::exception_ptr error() const { return nullptr; };
    // advance the task by one step until success, failure, or waiting for a condition
    // returns the task status after advancing one step
    virtual TaskState resume() { return TaskState::finished; };
    virtual ~TaskInterface() = default;
};

struct TaskBase : virtual public TaskInterface {
    uuids::uuid id() const override { return uuid_; }
    TaskState state() const override { return state_; }
    std::string name() const override { return "unknown"; }

    // task kind
    std::string kind() const override { return "unknown"; }
    // on success, returns the task result
    // while executing, this return value is meaningless
    nlohmann::json result() const override { return {}; }
    // returns detailed task status
    nlohmann::json status() const override { return {}; }
    std::exception_ptr error() const override { return exception_; }
    // advance the task by one step until success, failure, or waiting for a condition
    // returns the task status after advancing one step
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

// task pointer
using TaskPtr = std::shared_ptr<TaskInterface>;

/// parse input from json content and create a task
/// if task parameters are invalid, throws std::invalid_argument;
using TaskFactory = std::function<TaskPtr(const nlohmann::json&)>;
namespace task_mgr {
void add_task_factory(const std::string& task_type, TaskFactory task_factory);
}
