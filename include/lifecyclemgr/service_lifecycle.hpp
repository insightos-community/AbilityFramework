// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "lifecyclemgr/service_state.hpp"
#include "resourcemgr/service_cr.hpp"
#include "util/uuid_json_convert.hpp"
#include <chrono>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <uuid.h>

// service lifecycle manager
// manages start/stop, restart policy, and health checks for Service resources
class ServiceLifecycleManager {
public:
    using StartProcessFn = std::function<uuids::uuid(
        const std::string& executable_path,
        const std::vector<std::string>& args,
        std::function<void(int exit_status)> on_exit
    )>;

    explicit ServiceLifecycleManager(StartProcessFn start_fn);

    // start service
    void start_service(const ServiceCR& cr, const std::string& executable_path);

    // stop service
    void stop_service(uuids::uuid service_id);

    // process exit callback (triggered by SubprocessManager)
    void on_service_exit(uuids::uuid service_id, int exit_code);

    // periodic health check (called periodically by the main loop)
    void run_health_checks();

    // get service status
    ServiceState get_state(uuids::uuid service_id) const;

    // get all service runtime info
    struct ServiceRunInfo {
        uuids::uuid id;
        std::string serviceName;
        ServiceState state;
        int restartCount;
        int pid; // 0 if not running
    };
    std::vector<ServiceRunInfo> get_all_services() const;

private:
    struct ServiceEntry {
        ServiceCR cr;
        std::string executable_path;
        uuids::uuid process_handle_id; // from SubprocessManager
        ServiceState state = ServiceState::Stopped;
        int restartCount = 0;
        int consecutiveHealthFailures = 0;
        std::chrono::steady_clock::time_point lastRestart;
    };

    void do_restart(ServiceEntry& entry);
    bool should_restart(const ServiceEntry& entry, int exit_code) const;

    mutable std::mutex m;
    std::unordered_map<uuids::uuid, ServiceEntry> services;
    StartProcessFn start_process_fn;
};
