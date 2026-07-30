// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

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

// submit task to TaskMgr
expected<uuids::uuid, std::string> submit_task(TaskPtr task,std::string_view who) {
    auto msg = make_message(std::string{who}, "TaskMgr", "add_task/raw");
    msg.set_extra(task);

    if (auto res = send_sync(std::move(msg)); !res) {
        return unexpected{"send message failed" + res.error()};
    }
    return task->id();
}