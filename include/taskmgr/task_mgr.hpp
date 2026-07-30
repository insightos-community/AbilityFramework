// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "messagebus/messagebus.hpp"
#include "prelude.hpp"
#include "task.hpp"
#include "util/jthread.hpp"
#include <httplib.h>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <uvw.hpp>

// task manager
class TaskManager
    : public message_bus::Module
    , std::enable_shared_from_this<TaskManager> {
    mutable std::recursive_mutex m;
    using Clock = std::chrono::system_clock; // clock
    using TimePoint = Clock::time_point; // time point
    std::map<uuids::uuid, TaskPtr> tasks; // task
    std::map<uuids::uuid, TimePoint> last_updated; // last-updated time for each task
    // each task owns an async_handle in the event loop,
    // this handle is invoked multiple times; each invocation resumes the task once until it completes or fails
    std::map<uuids::uuid, std::shared_ptr<uvw::async_handle>> active_handles;

    // this idle executor advances each task by one step each time,
    std::shared_ptr<uvw::timer_handle> timer_handler;
    // checks the status of each task; if idle and ready, executes it once
    void update();

public:
    TaskManager(std::shared_ptr<uvw::loop> loop);
    // add task
    void add(TaskPtr task);
    // find whether a certain task exists; if not, return nullptr
    TaskPtr at(uuids::uuid id) const;
    // construct api
    friend void build_api(std::shared_ptr<TaskManager>, httplib::Server&);

    expected<void, ErrorMsg> add_task_factory(const std::string& task_type, TaskFactory factory);

private:
    // module name
    [[nodiscard]] std::string module_name() const override { return "TaskMgr"; };
    // receive message
    expected<void, std::string> on_receive(const message_bus::Message& message) override;

    std::unordered_map<std::string, TaskFactory> task_factories;
};

void send_msg(
    const std::string& from,
    const std::string& to,
    const std::string& operation,
    nlohmann::json& data
);

void send_extra_msg(
    const std::string& from, const std::string& to, const std::string& operation, TaskPtr task
);
