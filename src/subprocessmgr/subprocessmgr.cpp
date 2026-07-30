// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#include "param/process_option.hpp"
#include "param/task_id_wrapper.hpp"
#include "prelude.hpp"
#include "subprocessmgr/subprocess_mgr.hpp"
#include "util/container_fp.hpp"
#include "util/make_uuid.hpp"
#include <csignal>
#include <glog/logging.h>
#include <span>

namespace {
// a process handle without a data item is considered deleted
bool is_closed(const uvw::process_handle& p) { // check whether the process is closed
    return !p.data();
}
} // namespace
void SubprocessManager::cleanup() { // cleanup
    std::lock_guard _lk(m);
    std::erase_if(handles, [](const auto& p) { return is_closed(*p.second); });
}

namespace {
auto get_c_str = [](const auto& x) { return (char*)x.c_str(); }; // get the C string

std::vector<char*> make_cli_ptrs(std::span<const std::string> params) { // generate command-line arguments
    auto ptrs = transform_to_vector(params, get_c_str);
    ptrs.push_back(nullptr);
    return ptrs;
}

class EnvParams {
    std::unordered_map<std::string, std::string> m;
    mutable bool finished = false;
    mutable std::vector<std::string> strs;
    mutable std::vector<char*> ptrs;

public:
    EnvParams() = default;
    EnvParams(const std::unordered_map<std::string, std::string>& m1)
        : m(m1) {}

    void add(std::string_view key, std::string value) { m.emplace(key, std::move(value)); }

    void finish() const {
        if (finished) { throw std::invalid_argument("can't finish EnvParams twice"); }
        auto make_env_entry = [](const auto& p) { return strjoin(p.first, '=', p.second); };
        strs = transform_to_vector(m, make_env_entry);
        ptrs = transform_to_vector(strs, get_c_str);
        ptrs.push_back(nullptr);
        finished = true;
    }
    /// @return if user did not specify env vars, then use the framework process's env vars
    /// this function returns nullptr at this point
    char** raw() const {
        if (!finished) { finish(); }
        if (m.empty()) { return nullptr; }
        return (char**)ptrs.data();
    }
}; // class EnvParams
std::vector<gid_t> get_user_groups() {
    std::vector<gid_t> res;
    res.resize(8);
    int num_groups = getgroups(0, nullptr);
    if (num_groups < 0) {
        int e = errno;
        throw std::system_error(e, std::system_category());
    }
    res.resize(num_groups);
    res.resize(num_groups);
    num_groups = getgroups(num_groups, res.data());

    if (num_groups < 0) {
        int e = errno;
        throw std::system_error(e, std::system_category());
    }
    return res;
}

int can_execute(const char* filename) {
    struct stat file_stat;

    // get file status
    if (stat(filename, &file_stat) == -1) {
        int e = errno;
        LOG(ERROR) << "get file stat error: " << strerror(e);
        return 0; // error
    }
    // check other user permissions
    if (file_stat.st_mode & S_IXOTH) {
        return 1; // has execute permission
    }
    // check file owner permissions
    uid_t current_uid = getuid();
    if (file_stat.st_uid == current_uid) {
        if (file_stat.st_mode & S_IXUSR) {
            return 1; // has execute permission
        }
    }
    // check user group permissions
    auto group_ids = get_user_groups();
    if (std::find(group_ids.begin(), group_ids.end(), file_stat.st_gid) != group_ids.end()) {
        if (file_stat.st_mode & S_IXGRP) {
            return 1; // has execute permission
        }
    }
    return 0; // no execute permission
}
} // namespace
using IOFlag = uvw::process_handle::stdio_flags;
using Path = std::filesystem::path;
uuids::uuid SubprocessManager::start_process(
    const std::string& path,
    std::span<const std::string> args,
    const std::unordered_map<std::string, std::string>& envs,
    ProcessExitCallback on_exit,
    const LabelMap& labels
) {
    Path fspath = path;
    if (!exists(fspath)) { throw std::invalid_argument("executable file do not exist: " + path); }
    if (!can_execute(path.c_str())) {
        throw std::invalid_argument("file do not have executable permission: " + path);
    }

    // prepare command-line arguments
    std::vector<std::string> cli_args{path};
    copy(args.begin(), args.end(), std::back_inserter(cli_args));
    EnvParams envp(envs);
    auto id = make_uuid();
    auto process = loop->resource<uvw::process_handle>();
    CHECK_NOTNULL(process);
    process->init();
    process->data(std::make_shared<LabelMap>(labels));
    process->on<uvw::exit_event>([path,
                                  exit_cb = std::move(on_exit),
                                  this](const uvw::exit_event& ev, uvw::process_handle& self) {
        LOG(ERROR) << "process " << path << ", exited, status=" << ev.status;
        if (exit_cb) { exit_cb(ev); }
        self.close();
        self.data(nullptr);
        cleanup();
    });
    // set stdio attributes of the ability
    process->stdio(uvw::std_in, IOFlag::IGNORE_STREAM);
    process->stdio(uvw::std_out, IOFlag::INHERIT_FD);
    process->stdio(uvw::std_err, IOFlag::INHERIT_FD);
    process->flags(uvw::process_handle::process_flags{});

    auto args_for_uv = make_cli_ptrs(cli_args);
    int ret = process->spawn(path.c_str(), args_for_uv.data(), envp.raw());
    if (ret < 0) { throw make_error("spawn subprocess failed:", uv_strerror(ret)); }
    LOG(INFO) << "start subprocess success";

    // record the subprocess handle
    add(id, process);
    return id;
}

expected<void, std::string> SubprocessManager::on_receive(const message_bus::Message& message) {
    if (message.operation() == "start_process") {
        auto options = message.parse_as<msg_params::ProcessOption>();
        const auto* on_exit_p = message.get_extra<ProcessExitCallback>();
        ProcessExitCallback on_exit = on_exit_p ? (*on_exit_p) : nullptr;
        try {
            auto uuid
                = start_process(options.path, options.args, options.envs, on_exit, options.labels);
            message.respond(msg_params::ProcessIdWrapper{uuid});
            return {};
        }
        catch (std::exception& e) {
            message.respond_error(std::string_view{e.what()});
            return {};
        }
    }
    else if (message.operation() == "start_controller") {

        // parse Message data
        std::string content_str(message.content.begin(), message.content.end());
        nlohmann::json json_data = nlohmann::json::parse(content_str);
        LOG(WARNING) << "receive start_controller data: " << json_data.dump(2);

        std::string controller_path = json_data["controllerPath"].get<std::string>();
        std::string controller_id = json_data["controllerId"].get<std::string>();
        nlohmann::json nested_json = json_data["nested"];

        std::vector<std::string> args = {controller_id, nested_json.dump()};
        // start_process(controller_path, args);
        try {
            uuids::uuid process_id = start_process(controller_path, args);
            LOG(INFO) << "Started process with ID: " << process_id;
        }
        catch (const std::exception& e) {
            LOG(ERROR) << "Failed to start process: " << e.what() << std::endl;
        }

        // message.respond("ok");

        return {};
    }
    return err_invalid_operation(message);
}

void SubprocessManager::on_exit() {
    std::lock_guard _lk(m);
    LOG(WARNING) << "SubprocessMgr exit";
    for (auto& [id, handle] : handles) {
        if (!handle || is_closed(*handle)) { continue; }
        LOG(WARNING) << "killing child process of id: " << to_string(id);
        // must explicitly SIGTERM the subprocess, otherwise close() only closes the libuv handle,
        // sub-ability processes become orphans and keep sending heartbeats to the new framework, breaking the singleton constraint.
        handle->kill(SIGTERM);
        handle->close();
    }
}
