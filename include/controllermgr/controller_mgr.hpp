// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

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
    // container key
    std::unordered_map<std::string, Entry> heartbeats;

    // periodictask
    std::atomic<bool> running;
    std::thread timer_thread;

    // check whether the controller process is running
    bool is_process_running(const std::string& controller_id);

    // check and start the controller program
    // @param abilityId a certain ability's class id
    void check_and_start_controllers(
        const std::string& abilityPackageName,
        const std::string& abilityVersion,
        const std::string& abilityName
    );

    // scheduled task function
    void fetch_and_check_abilities();

public:
    ControllerManager();
    ~ControllerManager();

    std::string module_name() const override { return "ControllerMgr"; }

    void on_exit() override { running = false; }

    void on_receive_heartbeat(const ControllerHeartbeat& controllerheartbeat);
    friend void build_api(std::shared_ptr<ControllerManager> mgr, httplib::Server&);
};
