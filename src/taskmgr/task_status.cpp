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

#include "taskmgr/task_status.hpp"
#include "util/uuid_json_convert.hpp"
#include <glog/logging.h>

#define read_with_default(Name, DefaultValue)         \
    {                                                 \
        auto it = j.find(#Name);                      \
        if (it == j.end()) { x.Name = DefaultValue; } \
        else { it->get_to(x.Name); }                  \
    }

#define read_required(Name) j.at(#Name).get_to(x.Name)
#define read_optional(Name)                        \
    {                                              \
        auto it = j.find(#Name);                   \
        if (it != j.end()) { it->get_to(x.Name); } \
    }
#define read_optional_unless_empty(Name)                             \
    {                                                                \
        auto it = j.find(#Name);                                     \
        if (it != j.end() && (!it->empty())) { it->get_to(x.Name); } \
    }

void from_json(const nlohmann::json& j, TaskStatus& x) {
    read_required(id);
    read_required(executor_id);
    read_with_default(executor_type, ExecutorType::unknown);
    read_required(state);
    read_optional_unless_empty(start_time);
    read_optional_unless_empty(end_time);
    x.timeout = std::chrono::seconds{j.at("timeout").get<int64_t>()};
    read_optional(payload);
    read_optional_unless_empty(message);
}

#define output(Name) j[#Name] = x.Name;

#define output_not_null_if(Name, Cond) \
    if ((Cond)) { j[#Name] = x.Name; } \
    else { j[#Name] = nlohmann::json(); }

void to_json(nlohmann::json& j, const TaskStatus& x) {
    output(id);
    output(executor_id);
    output(executor_type);
    output(state);
    output(state);
    output_not_null_if(start_time, !x.start_time.empty());
    output_not_null_if(end_time, !x.end_time.empty());
    j["timeout"] = x.timeout.count();
    output(payload);
    output_not_null_if(message, !x.message.empty());
}

#define _field(Name) \
    case (ExecutorType::Name): return #Name;
std::string to_string(ExecutorType et) {
    switch (et) {
        _field(ability);
        _field(controller);
        _field(framework);
        _field(unknown);
    default: return "<invalid executor type>";
    }
}
#undef _field

#define _field(Name) \
    if (sv == #Name) { return ExecutorType::Name; }

ExecutorType executor_type_from_string(std::string_view sv) {
    _field(ability);
    _field(controller);
    _field(framework);

    return ExecutorType::unknown;
}

std::string format_as_task_datetime(std::chrono::system_clock::time_point t) {
    if (t == std::chrono::system_clock::time_point{}) { return ""; }

    // Convert to time_t
    std::time_t now_time_t = std::chrono::system_clock::to_time_t(t);

    // Convert to local time
    std::tm now_tm = *std::localtime(&now_time_t);

    // Get milliseconds
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t.time_since_epoch()) % 1000;

    // Create a string stream to format the time
    std::ostringstream oss;
    oss << std::put_time(&now_tm, "%Y-%m-%d %H:%M:%S") << '.' << std::setw(3) << std::setfill('0')
        << ms.count();

    return oss.str();
}

TaskDateTimeStr task_datetime_now() {
    // Get the current time point
    auto now = std::chrono::system_clock::now();
    return format_as_task_datetime(now);
}
