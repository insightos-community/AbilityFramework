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
#include "task.hpp"

// '%Y-%m-%d %H:%M:%S.%3N' 格式的日期字符串
// 可以通过命令 date +'%Y-%m-%d %H:%M:%S.%3N' 查看样例
// 如 2025-03-29 16:18:06.952
// 在内存中可以存储为unix格式的时间戳
struct TaskDateTime {
    std::chrono::milliseconds stamp; // 毫秒级时间戳
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
    uuids::uuid id;          // 任务id
    uuids::uuid executor_id; // 执行该任务的实例id
    // 执行者的类型
    //(ability 表示能力, controller 表示控制器, framework表示框架)
    ExecutorType executor_type;
    TaskState state;            // 任务执行状态
    TaskDateTimeStr start_time; // 开始时间(若尚未开始,该时间为空)
    TaskDateTimeStr end_time;   // 结束时间(若尚未结束.该时间为空)
    std::chrono::seconds timeout; // 任务最大执行时长(比如1min),如果超过了当前时长,可以判定为失败
    // 如果成功,存储任务结果,如果失败,存储错误细节
    // 这一项供程序读
    nlohmann::json payload;
    std::string message; // 如果失败或取消,存储人类可读的错误原因
    friend void from_json(const nlohmann::json&, TaskStatus&);
    friend void to_json(nlohmann::json&, const TaskStatus&);
};
