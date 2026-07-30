// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#include "controllermgr/controller_mgr.hpp"
#include "lifecyclemgr/heartbeat.hpp"
#include "messagebus/message_client.hpp"
#include "messagebus/messagebus.hpp"
#include "param/ability_class_info.hpp"
#include "util/global_vars.hpp"
#include "util/make_uuid.hpp"
#include "util/scope.hpp"
#include "util/uuid_json_convert.hpp"
#include <cstdlib>
#include <filesystem>
#include <glog/logging.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

// constructor
ControllerManager::ControllerManager()
    : running(true)
    , timer_thread(&ControllerManager::fetch_and_check_abilities, this) {}

ControllerManager::~ControllerManager() {
    running = false;
    if (timer_thread.joinable()) { timer_thread.join(); }
}

void ControllerManager::on_receive_heartbeat(const ControllerHeartbeat& controllerheartbeat) {
    LOG(INFO) << "Receive Controller Heartbeat:\n" << nlohmann::json(controllerheartbeat).dump(2);
    std::lock_guard _lk(m);
    auto& entry = heartbeats[controllerheartbeat.controllerInstanceId];
    entry.controllerheartbeat = controllerheartbeat;
    entry.last_update = Clock::now();
}

namespace {
nlohmann::json read_json_by_content_type(const std::string& content_type, const std::string& data) {
    if (content_type == "application/json") { return nlohmann::json::parse(data); }
    if (content_type == "application/cbor") { return nlohmann::json::from_cbor(data); }
    throw std::invalid_argument("invalid content-type: " + content_type);
}

std::string get_content_type(const httplib::Request& req) {
    scope_fail complain([]() { LOG(WARNING) << "on get header value:"; });
    if (req.has_header("content-type")) { return req.get_header_value("content-type"); }
    return "application/json";
}
} // namespace

void build_api(std::shared_ptr<ControllerManager> mgr, httplib::Server& server) {
    CHECK_NOTNULL(mgr);
    using httplib::Request, httplib::Response;
    // accept controller heartbeat
    server.Post("/api/controller-heartbeat", [mgr](const Request& req, Response& res) {
        scope_fail _c([]() { LOG(WARNING) << "on handling post /api/controller-heartbeat"; });
        auto content_type = get_content_type(req);
        auto j = read_json_by_content_type(content_type, req.body);
        auto hb = j.get<ControllerHeartbeat>();
        mgr->on_receive_heartbeat(hb);
    });
}
template <typename F>
bool sleep_until_pred(
    std::chrono::milliseconds ms,
    F&& pred,
    std::chrono::milliseconds interval = std::chrono::milliseconds{50}
) {
    int n = ms / interval;
    for (int i = 0; i <= n; ++i) {
        if (pred()) { return true; }
        std::this_thread::sleep_for(interval);
    }
    return false;
}
// scheduled task function
void ControllerManager::fetch_and_check_abilities() {

    int interval = global_vars::get_config<int>("/controller_mgr/fetch_interval", 10);
    while (running) {
        bool to_quit
            = sleep_until_pred(std::chrono::seconds(interval), [this]() { return !running; });
        if (to_quit) { break; }
        // send message to the message bus to get running ability info
        auto res = send_sync(make_message("ControllerMgr", "LifecycleMgr", "running_abilities"));
        if (!res) {
            LOG(ERROR) << "failed to send message: " << res.error();
            continue;
        }
        if (!res->success()) {
            LOG(ERROR) << "error response: " << res->view();
            continue;
        }
        std::vector<Heartbeat> ability_heartbeats = res->parse_as<std::vector<Heartbeat>>();
        // LOG(INFO) << "test scheduled task function";
        for (auto& ability_heartbeat : ability_heartbeats) {
            // iterate ability heartbeat info in the vector
            // currently the controller instance id is the class id of its corresponding ability
            nlohmann::json json_heartbeat = ability_heartbeat;
            // LOG(INFO) << "Heartbeats JSON: " << json_heartbeat.dump(2);
            std::string id = json_heartbeat["id"].get<std::string>();
            // LOG(WARNING) << "abilityInstanceId: " << id;

            // get ability class id, package name and version from resourceMgr
            // nlohmann::json json_data = {{"abilityInstanceId", id}};
            auto res = send_sync(
                make_message("ControllerMgr", "ResourceMgr", "get_ability", nlohmann::json(id))
            );
            if (!res) {
                LOG(ERROR) << "failed to send message: " << res.error();
                continue;
            }
            if (!res->success()) {
                LOG(WARNING) << "error get ability info for " << id << ": " << res->view();
                continue;
            }

            // handle response content
            auto param = res->try_parse<msg_params::AbilityClassInfo>();
            if (!param) {
                LOG(ERROR) << "Failed to find ability info: " << param.error();
                continue;
            }
            auto& [abilityPackageName, abilityVersion] = *param;
            // call check_and_start_controllers
            check_and_start_controllers(
                abilityPackageName, abilityVersion, ability_heartbeat.abilityName
            );
        }
    }
    LOG(INFO) << module_name() << " work loop exit";
}

// check whether the controller process is running
bool ControllerManager::is_process_running(const std::string& controller_id) {
    std::lock_guard _lk(m);
    auto it = heartbeats.find(controller_id);
    if (it == heartbeats.end()) { return false; }
    auto& entry = it->second;
    auto now = Clock::now();
    // the controller sent a heartbeat within the last 30 seconds, indicating it is active
    return now - entry.last_update < std::chrono::seconds(30);
}

// check and start the controller program
void ControllerManager::check_and_start_controllers(
    const std::string& abilityPackageName,
    const std::string& abilityVersion,
    const std::string& abilityName
) {
    // get controller id from ability class id
    std::string controller_id
        = strjoin("controller-", abilityPackageName, '-', abilityName, '-', abilityVersion);
    // check whether the controller process is running
    if (is_process_running(controller_id)) {
        VLOG(1) << "Controller process is running";
        return;
    }
    VLOG(1) << "Controller process is not running";
    // get the controller path
    std::filesystem::path package_path = global_vars::packages_path();
    std::filesystem::path bin_path = package_path / abilityPackageName / abilityVersion / "bin";
    std::filesystem::path controller_path = bin_path / (abilityName + ".controller");
    // check whether the controller path exists
    if (!std::filesystem::exists(controller_path)) {
        VLOG(1) << "Controller path " << controller_path << " does not exist";
        return;
    }
    LOG(INFO) << "Controller path: " << controller_path;
    // config parameters
    // the framework puts a uuid at ARGV[1], which is the controller instance id.
    // puts a json at ARGV[2] to hint the framework's contact info, same content as the json given to the ability.
    nlohmann::json nested_json = {{"protocol", "http"}, {"port", 8080}};
    nlohmann::json arg_data
        = {{"controllerPath", controller_path},
           {"controllerId", controller_id},
           {"nested", nested_json}};
    LOG(WARNING) << "Message with arg_data: " << arg_data.dump(2);
    // sendmessage
    send(make_message("ControllerMgr", "SubprocessMgr", "start_controller", arg_data));
}
