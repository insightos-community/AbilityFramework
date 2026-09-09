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
#include "lifecyclemgr/service_state.hpp"
#include "resourcemgr/service_cr.hpp"
#include "util/uuid_json_convert.hpp"
#include <chrono>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <uuid.h>

// 服务生命周期管理器
// 管理 Service 类型资源的启停、重启策略和健康检查
class ServiceLifecycleManager {
public:
    using StartProcessFn = std::function<uuids::uuid(
        const std::string& executable_path,
        const std::vector<std::string>& args,
        std::function<void(int exit_status)> on_exit
    )>;

    explicit ServiceLifecycleManager(StartProcessFn start_fn);

    // 启动服务
    void start_service(const ServiceCR& cr, const std::string& executable_path);

    // 停止服务
    void stop_service(uuids::uuid service_id);

    // 进程退出回调 (由 SubprocessManager 触发)
    void on_service_exit(uuids::uuid service_id, int exit_code);

    // 定期执行健康检查 (由主循环定时调用)
    void run_health_checks();

    // 获取服务状态
    ServiceState get_state(uuids::uuid service_id) const;

    // 获取所有服务运行信息
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
