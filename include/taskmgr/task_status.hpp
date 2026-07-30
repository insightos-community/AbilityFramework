// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "task.hpp"

// '%Y-%m-%d %H:%M:%S.%3N' formatdate string
// canviacommand date +'%Y-%m-%d %H:%M:%S.%3N' see example
// e.g. 2025-03-29 16:18:06.952
// in memory can storeas a unix timestamp in format
struct TaskDateTime {
    std::chrono::milliseconds stamp; // millisecond timestamp
    std::string time_str();
};

using TaskDateTimeStr = std::string;
TaskDateTimeStr task_datetime_str_now();
std::string format_as_task_datetime(std::chrono::system_clock::time_point t);

enum class ExecutorType { unknown, ability, controller, framework };

std::string to_string(ExecutorType);
ExecutorType executor_type_from_string(std::string_view);

NLOHMANN_JSON_SERIALIZE_ENUM(
    ExecutorType,
    {{ExecutorType::unknown, "unknown"},
     {ExecutorType::ability, "ability"},
     {ExecutorType::controller, "controller"},
     {ExecutorType::framework, "framework"}}
)

struct TaskStatus {
    uuids::uuid id; // task id
    uuids::uuid executor_id; // instance id of the executor of this task
    // executor type
    //(ability = ability, controller = controller, framework = framework)
    ExecutorType executor_type;
    TaskState state; // task execution status
    TaskDateTimeStr start_time; // start time (empty if not started yet)
    TaskDateTimeStr end_time; // end time (empty if not ended yet)
    std::chrono::seconds timeout; // max task duration (e.g. 1min); if exceeded, the task can be judged as failed
    // on success stores task result; on failure stores error details
    // this item is for program reading
    nlohmann::json payload;
    std::string message; // on failure or cancellation, stores a human-readable error reason
    friend void from_json(const nlohmann::json&, TaskStatus&);
    friend void to_json(nlohmann::json&, const TaskStatus&);
};
