// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "messagebus/messagebus.hpp"
#include "process_exit_callback.hpp"
#include <httplib.h>
#include <mutex>
#include <span>
#include <unordered_map>
#include <utility>
#include <uuid.h>
#include <uvw/process.h>

class SubprocessManager : public message_bus::Module {
    using HandlePtr = std::shared_ptr<uvw::process_handle>; // processhandle
    mutable std::recursive_mutex m;
    std::unordered_map<uuids::uuid, HandlePtr> handles;
    std::shared_ptr<uvw::loop> loop;
    void cleanup();

public:
    using LabelMap = std::unordered_map<std::string, std::string>;
    SubprocessManager(std::shared_ptr<uvw::loop> l)
        : loop(std::move(l)) {}

    /**
     * @param path path where the program resides
     * @param args command-line arguments
     * @param envs environment variables (if setting)
     * @param on_exit callback called when the program exits
     * @param labels extra labels attached to the process,used to determine the process type
     * @return auuid,indicates this process
     */
    uuids::uuid start_process(
        const std::string& path,
        std::span<const std::string> args,
        const std::unordered_map<std::string, std::string>& envs = {},
        ProcessExitCallback on_exit = nullptr,
        const LabelMap& labels = {}
    );
    // register own api to the http server
    friend void build_api(std::shared_ptr<SubprocessManager>, httplib::Server& server);

    void on_exit() override;

private:
    // add a ready-made process handle; if it has a data item, it must be a LabelMap, otherwise behavior is undefined
    void add(const uuids::uuid& name, std::shared_ptr<uvw::process_handle> h) {
        std::lock_guard _lk(m);
        handles.emplace(name, h);
    }
    // implementation module interface
    std::string module_name() const override { return "SubprocessMgr"; }
    expected<void, std::string> on_receive(const message_bus::Message&) override;
};
