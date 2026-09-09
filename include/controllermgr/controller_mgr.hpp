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
#include "controller_heartbeat.hpp"
#include <mutex>
#include <unordered_map>

#include "messagebus/messagebus.hpp"
#include <atomic>
#include <filesystem>
#include <string>
#include <thread>

namespace httplib {
class Server;
}

class ControllerManager : public message_bus::Module {
    using Clock = std::chrono::system_clock;
    using Timepoint = Clock::time_point;

    struct Entry {
        ControllerHeartbeat controllerheartbeat;
        Timepoint last_update;
        std::optional<Timepoint> last_connect;
    };

    std::recursive_mutex m;
    // 容器的键
    std::unordered_map<std::string, Entry> heartbeats;

    // 定时任务
    std::atomic<bool> running;
    std::thread timer_thread;

    // 检查控制器进程是否在运行
    bool is_process_running(const std::string& controller_id);

    // 检查并启动控制器程序
    // @param abilityId 某一能力的类id
    void check_and_start_controllers(
        const std::string& abilityPackageName,
        const std::string& abilityVersion,
        const std::string& abilityName
    );

    // 定时任务函数
    void fetch_and_check_abilities();

public:
    ControllerManager();
    ~ControllerManager();

    std::string module_name() const override { return "ControllerMgr"; }

    void on_exit() override { running = false; }

    void on_receive_heartbeat(const ControllerHeartbeat& controllerheartbeat);
    friend void build_api(std::shared_ptr<ControllerManager> mgr, httplib::Server&);
};
