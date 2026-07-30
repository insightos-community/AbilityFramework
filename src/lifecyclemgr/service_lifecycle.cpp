// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#include "lifecyclemgr/service_lifecycle.hpp"
#include <glog/logging.h>
#include <httplib.h>

ServiceLifecycleManager::ServiceLifecycleManager(StartProcessFn start_fn)
    : start_process_fn(std::move(start_fn)) {}

void ServiceLifecycleManager::start_service(
    const ServiceCR& cr, const std::string& executable_path
) {
    std::lock_guard lock(m);
    auto id = cr.id;

    ServiceEntry entry;
    entry.cr = cr;
    entry.executable_path = executable_path;
    entry.state = ServiceState::Starting;
    entry.restartCount = 0;
    entry.consecutiveHealthFailures = 0;
    entry.lastRestart = std::chrono::steady_clock::now();

    LOG(INFO) << "starting service " << cr.metadata.name << " (id=" << to_string(id) << ")";

    auto on_exit = [this, id](int exit_status) { on_service_exit(id, exit_status); };

    try {
        entry.process_handle_id = start_process_fn(executable_path, {}, on_exit);
        entry.state = ServiceState::Running;
    }
    catch (const std::exception& e) {
        LOG(ERROR) << "failed to start service " << cr.metadata.name << ": " << e.what();
        entry.state = ServiceState::Failed;
    }

    services[id] = std::move(entry);
}

void ServiceLifecycleManager::stop_service(uuids::uuid service_id) {
    std::lock_guard lock(m);
    auto it = services.find(service_id);
    if (it == services.end()) { return; }

    LOG(INFO) << "stopping service " << it->second.cr.metadata.name;
    // mark as Stopped to block the restart logic in on_service_exit
    it->second.state = ServiceState::Stopped;
    // actual process termination is the caller's responsibility (via SubprocessManager kill)
}

void ServiceLifecycleManager::on_service_exit(uuids::uuid service_id, int exit_code) {
    std::lock_guard lock(m);
    auto it = services.find(service_id);
    if (it == services.end()) { return; }

    auto& entry = it->second;

    // if already marked Stopped by stop_service, do not restart
    if (entry.state == ServiceState::Stopped) {
        LOG(INFO) << "service " << entry.cr.metadata.name << " exited (stopped by user)";
        return;
    }

    LOG(WARNING) << "service " << entry.cr.metadata.name << " exited with code " << exit_code;

    if (should_restart(entry, exit_code)) {
        do_restart(entry);
    }
    else {
        entry.state = ServiceState::Failed;
        LOG(ERROR) << "service " << entry.cr.metadata.name << " marked as Failed"
                   << " (restartCount=" << entry.restartCount << ")";
    }
}

bool ServiceLifecycleManager::should_restart(const ServiceEntry& entry, int exit_code) const {
    const auto& policy = entry.cr.spec->restartPolicy;
    int maxRestarts = entry.cr.spec->maxRestarts;

    if (policy == "never") { return false; }
    if (policy == "on-failure" && exit_code == 0) { return false; }
    // policy == "always" or (policy == "on-failure" && exit_code != 0)
    return entry.restartCount < maxRestarts;
}

void ServiceLifecycleManager::do_restart(ServiceEntry& entry) {
    entry.restartCount++;
    entry.state = ServiceState::Restarting;

    // exponential backoff: backoff * 2^(restartCount-1), max 300s
    int backoff = entry.cr.spec->restartBackoffSeconds;
    int delay = std::min(backoff * (1 << (entry.restartCount - 1)), 300);

    LOG(WARNING) << "restarting service " << entry.cr.metadata.name
                 << " in " << delay << "s (attempt " << entry.restartCount << ")";

    // note: actual delayed restart needs to be implemented via libuv timer
    // currently simplified to direct restart (should use uvw timer in real deployment)
    auto on_exit = [this, id = entry.cr.id](int exit_status) { on_service_exit(id, exit_status); };

    try {
        entry.process_handle_id = start_process_fn(entry.executable_path, {}, on_exit);
        entry.state = ServiceState::Running;
        entry.consecutiveHealthFailures = 0;
        entry.lastRestart = std::chrono::steady_clock::now();
    }
    catch (const std::exception& e) {
        LOG(ERROR) << "restart failed for " << entry.cr.metadata.name << ": " << e.what();
        entry.state = ServiceState::Failed;
    }
}

void ServiceLifecycleManager::run_health_checks() {
    std::lock_guard lock(m);
    for (auto& [id, entry] : services) {
        if (entry.state != ServiceState::Running) { continue; }
        if (!entry.cr.spec->healthCheck) { continue; }
        const auto& hc = *entry.cr.spec->healthCheck;
        if (!hc.liveness) { continue; }
        const auto& probe = *hc.liveness;

        if (probe.type == "http" && probe.port > 0 && !probe.path.empty()) {
            httplib::Client cli("localhost", probe.port);
            cli.set_connection_timeout(2);
            cli.set_read_timeout(2);
            auto res = cli.Get(probe.path);
            if (res && res->status >= 200 && res->status < 400) {
                entry.consecutiveHealthFailures = 0;
            }
            else {
                entry.consecutiveHealthFailures++;
                LOG(WARNING) << "service " << entry.cr.metadata.name
                             << " health check failed (" << entry.consecutiveHealthFailures
                             << "/" << probe.failureThreshold << ")";
                if (entry.consecutiveHealthFailures >= probe.failureThreshold) {
                    LOG(ERROR) << "service " << entry.cr.metadata.name
                               << " health check threshold exceeded, triggering restart";
                    entry.consecutiveHealthFailures = 0;
                    if (should_restart(entry, 1)) {
                        do_restart(entry);
                    }
                    else {
                        entry.state = ServiceState::Failed;
                    }
                }
            }
        }
    }
}

ServiceState ServiceLifecycleManager::get_state(uuids::uuid service_id) const {
    std::lock_guard lock(m);
    auto it = services.find(service_id);
    if (it == services.end()) { return ServiceState::Stopped; }
    return it->second.state;
}

std::vector<ServiceLifecycleManager::ServiceRunInfo>
ServiceLifecycleManager::get_all_services() const {
    std::lock_guard lock(m);
    std::vector<ServiceRunInfo> result;
    result.reserve(services.size());
    for (const auto& [id, entry] : services) {
        result.push_back({
            .id = id,
            .serviceName = entry.cr.spec->serviceName,
            .state = entry.state,
            .restartCount = entry.restartCount,
            .pid = 0, // TODO: get actual PID from SubprocessManager
        });
    }
    return result;
}
