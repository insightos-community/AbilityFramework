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
#include "messagebus/messagebus.hpp"
#include "prelude.hpp"
#include "task.hpp"
#include "util/jthread.hpp"
#include <httplib.h>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <uvw.hpp>

// 任务管理器
class TaskManager
    : public message_bus::Module
    , std::enable_shared_from_this<TaskManager> {
    mutable std::recursive_mutex m;
    using Clock = std::chrono::system_clock;       // 时钟
    using TimePoint = Clock::time_point;           // 时间点
    std::map<uuids::uuid, TaskPtr> tasks;          // 任务
    std::map<uuids::uuid, TimePoint> last_updated; // 每个任务最近一次更新的时间
    // 每个任务在事件循环种拥有一个async_handle,
    // 这个handle会被多次调用,每调用一次则会给任务resume一次,直到完成或失败
    std::map<uuids::uuid, std::shared_ptr<uvw::async_handle>> active_handles;

    // 这个空闲执行器每次将任务前进一轮,
    std::shared_ptr<uvw::timer_handle> timer_handler;
    // 检查每个任务的状况,如果空闲且就绪,就使其执行一次
    void update();

public:
    TaskManager(std::shared_ptr<uvw::loop> loop);
    // 添加任务
    void add(TaskPtr task);
    // 查找是否有某一任务,如果不存在,返回 nullptr
    TaskPtr at(uuids::uuid id) const;
    // 构造api
    friend void build_api(std::shared_ptr<TaskManager>, httplib::Server&);

    expected<void, ErrorMsg> add_task_factory(const std::string& task_type, TaskFactory factory);

private:
    // 模块名
    [[nodiscard]] std::string module_name() const override { return "TaskMgr"; };
    // 接收消息
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
