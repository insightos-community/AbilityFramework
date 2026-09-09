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

#include "taskmgr/task.hpp"
#include "util/expected.hpp"
#include "messagebus/message_client.hpp"

#define _field(Name) \
    case (TaskState::Name): return #Name;
std::string to_string(TaskState s) {
    switch (s) {
        _field(unstarted);
        _field(running);
        _field(pending);
        _field(fine);
        _field(finished);
        _field(error);
        _field(cancelled);

    default: return "<invalid state>";
    }
}
#undef _field

#define _field(Name) \
    if (sv == #Name) { return TaskState::Name; }

TaskState task_state_from_string(std::string_view sv) {
    _field(unstarted);
    _field(running);
    _field(pending);
    _field(fine);
    _field(finished);
    _field(error);
    _field(cancelled);
    throw std::invalid_argument("invalid task state str: " + std::string(sv));
}

// 提交任务到TaskMgr
expected<uuids::uuid, std::string> submit_task(TaskPtr task,std::string_view who) {
    auto msg = make_message(std::string{who}, "TaskMgr", "add_task/raw");
    msg.set_extra(task);

    if (auto res = send_sync(std::move(msg)); !res) {
        return unexpected{"send message failed" + res.error()};
    }
    return task->id();
}