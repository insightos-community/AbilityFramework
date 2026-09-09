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

#include "lifecyclemgr/lifecycle_mgr.hpp"
#include "databasemgr/database_mgr.hpp"
#include "messagebus/message_client.hpp"
#include "messagebus/messagebus.hpp"
#include "param/ability_storage_info.hpp"
#include "param/process_option.hpp"
#include "param/task_id_wrapper.hpp"
#include "prelude.hpp"
#include "resourcemgr/ability_cr.hpp"
#include "subprocessmgr/process_exit_callback.hpp"
#include "taskmgr/common_tasks.hpp"
#include "util/global_vars.hpp"
#include <glog/logging.h>
#include <httplib.h>
#include <iomanip>
#include <nlohmann/json.hpp>

bool LifecycleManager::on_heartbeat(const Heartbeat& heartbeat) {
    VLOG(1) << "receive heartbeat:\n" << nlohmann::json(heartbeat).dump(2);

    // 先问 ResourceMgr — 它是 AbilityInstance 表的 source of truth。
    // 返回 {"accepted": bool}; false 意味着这个 instance_id 不在 framework
    // 注册的实例集里 (典型场景: 上一代 framework 遗留的 python 僵尸进程
    // reparent 到 init 后还在打心跳)。此时本地 map 不 touch,
    // HTTP 层回 410 让 SDK 自毁。
    auto res = send_sync(make_message("LifecycleMgr", "ResourceMgr", "HeartbeatEvent", heartbeat));
    bool accepted = false;
    if (res && res->success()) {
        try {
            auto j = nlohmann::json::parse(res->view());
            accepted = j.value("accepted", false);
        }
        catch (const std::exception& e) {
            LOG(WARNING) << "HeartbeatEvent response parse failed: " << e.what()
                         << "; treating heartbeat as orphan";
        }
    }
    else {
        LOG(WARNING) << "HeartbeatEvent sync failed"
                     << (res ? std::string{" body=" + std::string{res->view()}} : std::string{})
                     << "; treating heartbeat as orphan";
    }
    if (!accepted) {
        // 不更新本地 heartbeats map — 一旦调 exec_on_key, 它会自动为未知
        // key 插入一个默认 Entry (concurrent_map 的行为), 孤儿就混进 map。
        return false;
    }

    heartbeats.exec_on_key(heartbeat.id, [&](Entry& entry) {
        auto old_port = entry.heartbeat.IPCPort;
        entry.heartbeat = heartbeat;
        entry.last_update = Clock::now();
        if (heartbeat.state == LifecycleState::Running) { entry.last_connect = Clock::now(); }

        if (!entry.client) [[unlikely]] {
            entry.client = AbilityClient::make(
                {.url = strjoin(heartbeat.IPCProtocol, "://localhost:", heartbeat.IPCPort)}
            );
        }
        else if (old_port != heartbeat.IPCPort) {
            // 如果端口变了,则重新创建一个client，并更新client的url
            LOG(WARNING) << "ability: " << heartbeat.id << " port changed from " << old_port
                         << " to " << heartbeat.IPCPort;
            entry.client = AbilityClient::make(
                {.url = strjoin(heartbeat.IPCProtocol, "://localhost:", heartbeat.IPCPort)}
            );
        }
    });
    return true;
}

TaskPtr LifecycleManager::task_send_lifecycle_operation(const LifecycleRequest& request) {
    // TODO: 检查生命周期是否合法
    auto opt_entry = heartbeats.find(request.abilityInstanceId);
    if (!opt_entry) { throw std::out_of_range("ability instance not found"); }
    auto client = opt_entry->client;
    return tasks::on_thread_queue("send-lifecycle-operation", [client, request]() {
        LOG(INFO) << "execute lifecycle request " << request.command << " to "
                  << request.abilityInstanceId;
        auto res = client->execute(request.command);
        if (!res) {
            LOG(ERROR) << "execute lifecycle request " << request.command << " to "
                       << request.abilityInstanceId << " failed: " << res.error();
            throw std::runtime_error("error reading from ability http server: " + res.error());
        }
        LOG(INFO) << "execute lifecycle request " << request.command << " to "
                  << request.abilityInstanceId << " success";
    });
}
namespace {
auto make_wait_timeout_callback(uuids::uuid instance_id) {
    return [instance_id]() {
        std::ostringstream oss;
        oss << "wait ability " << to_string(instance_id) << "heartbeat timeout ";
        LOG(ERROR) << oss.str();
        return oss.str();
    };
}
} // namespace

TaskPtr LifecycleManager::task_wait_for_lifecycle_state(
    const uuids::uuid& instance_id, LifecycleState desired
) {
    using namespace std::chrono_literals;
    return tasks::wait_once(
        "wait-task-state",
        [instance_id, desired, this]() {
            auto opt_entry = heartbeats.find(instance_id);
            if (!opt_entry) { return false; }
            const auto& entry = *opt_entry;
            bool ok = entry.heartbeat.state == desired;
            LOG_IF(INFO, ok) << "ability " << to_string(instance_id) << " task state wait to be "
                             << to_string(desired) << " complete, corresponding heartbeat is "
                             << nlohmann::json(entry.heartbeat);
            return ok;
        },
        15s,
        make_wait_timeout_callback(instance_id)
    );
}

TaskPtr LifecycleManager::task_wait_for_heartbeat(const uuids::uuid& instance_id) {
    using namespace std::chrono_literals;
    return tasks::wait_once(
        "wait-task-state",
        [instance_id, this]() -> bool { return heartbeats.contains(instance_id); },
        15s,
        make_wait_timeout_callback(instance_id)
    );
}
namespace {
LifecycleState desired_state(const std::string& command) {
    if (command == "start") { return LifecycleState::Standby; }
    if (command == "connect") { return LifecycleState::Running; }
    if (command == "disconnect") { return LifecycleState::Suspend; }
    if (command == "terminate") { return LifecycleState::Terminated; }
    throw std::invalid_argument("unknown comand: " + command);
}

std::string vector_to_string(const std::vector<char>& v) {
    return std::string(v.begin(), v.end());
}

nlohmann::json make_framework_address() {
    nlohmann::json res{{"port", global_vars::get_config<int>("/http_port")}, {"protocol", "http"}};
    std::string opentelemetry_url = global_vars::get_config<std::string>("/opentelemetry/url", "");
    if (!opentelemetry_url.empty()) { res["opentelemetry"] = opentelemetry_url; }
    return res;
}

auto make_instance_directory(uuids::uuid instance_id) try {
    auto destinated_path = global_vars::home_path() / "data" / "ability" / to_string(instance_id);
    LOG(INFO) << "making instance directory at " << destinated_path;
    bool newly_created = create_directories(destinated_path);
    return destinated_path;
}
catch (std::filesystem::filesystem_error& e) {
    LOG(ERROR) << "error when making instance directory for " << to_string(instance_id);
    throw;
}

msg_params::ProcessOption gather_process_args(
    msg_params::AbilityStorageInfo& storage, uuids::uuid instance_id, const AbilityCR& cr
) {
    CHECK(!storage.executable_path.empty()) << "why?";
    msg_params::ProcessOption res;
    res.path = storage.executable_path;
    res.args.push_back(to_string(instance_id));
    auto argv2 = make_framework_address();
    LOG(WARNING) << "argv2: " << argv2.dump(2);

    argv2["instanceDirectory"] = make_instance_directory(instance_id).string();
    {
        auto assets_path = std::filesystem::path(storage.package_path) / "assets";
        if (exists(assets_path)) { argv2["assetsDirectory"] = assets_path.string(); }
        else { LOG(WARNING) << "assets path " << assets_path << " does not exist"; }
    }
    res.args.push_back(argv2.dump());

    res.labels["ability"] = to_string(instance_id);
    // 尚未添加用户在CR中指定的环境变量,之后加
    return res;
}
template <OstreamPrintable T>
auto prefix_by(T&& v) {
    using U = std::decay_t<T>;
    return [pre = U(std::forward<T>(v))](auto&& x) { return strjoin(pre, x); };
}

template <typename T>
expected<T, std::string> parse_if_is_success(const message_bus::Message& m) {
    if (!m.success()) { return unexpected{m.view()}; }
    return m.try_parse<T>();
}

void send_subability_error(
    int parent_ipc_port, uuids::uuid instance_id, LifecycleState last_state
) {

    httplib::Client cli("localhost", parent_ipc_port);
    nlohmann::json payload{{"abilityInstanceId", instance_id}, {"lifecycleState", last_state}};
    LOG(INFO) << "post ability error to localhost:" << parent_ipc_port
              << " payload = " << payload.dump(2);
    auto post_res = cli.Post("/api/subability-error", payload.dump(2), "application/json");
    if (!post_res) {
        LOG(ERROR) << "post to localhost:" << parent_ipc_port
                   << "failed: " << to_string(post_res.error());
        return;
    }
    else if (post_res->status != 200) {
        LOG(ERROR) << "post to localhost:" << parent_ipc_port << "failed: " << post_res->body;
        return;
    }
}

} // namespace

void LifecycleManager::try_inform_parent_ability(uuids::uuid instance_id) {
    // 询问这个能力是不是谁的子能力
    LifecycleState last_status = [&, this]() {
        auto hb = get_heartbeat(instance_id);
        if (hb) { return hb->state; }
        return LifecycleState::Unknown;
    }();
    auto parent_id
        = send_sync(make_message("LifecycleMgr", "ResourceMgr", "find_parent/id", instance_id))
              .transform_error(prefix_by("send message failed: "))
              .and_then(parse_if_is_success<uuids::uuid>);
    // {
    //     LOG(INFO) << "remove lifecycle entry for ability " << to_string(instance_id);
    //     heartbeats.erase(instance_id);
    // }
    if (!parent_id) {
        LOG(WARNING) << " no parent ability for " << instance_id;
        return;
    }
    auto parent_hb = get_heartbeat(*parent_id);
    if (!parent_hb) {
        LOG(ERROR) << "no parent ability heartbeat for " << instance_id;
        return;
    }
    send_subability_error(parent_hb->IPCPort, instance_id, last_status);
}

void LifecycleManager::on_ability_exit(uuids::uuid instance_id) {
    try_inform_parent_ability(instance_id);
    // 从内存心跳表中移除，使 contains_active 返回 false
    heartbeats.erase(instance_id);
    // 通知 ResourceMgr 销毁 AbilityInstance 行 (无论 singleton 还是 replica)
    auto msg
        = make_message("LifecycleMgr", "ResourceMgr", "ability_instance_exited", instance_id);
    auto res = send_sync(std::move(msg));
    if (!res) {
        LOG(WARNING) << "notify instance exit failed: " << res.error();
    }
}

bool LifecycleManager::contains_active(uuids::uuid instance_id) const {
    auto opt_entry = heartbeats.find(instance_id);
    if (!opt_entry) { return false; }
    return is_active(opt_entry->heartbeat.state);
}

std::optional<Heartbeat> LifecycleManager::get_heartbeat(uuids::uuid instance_id) const {
    auto opt_entry = heartbeats.find(instance_id);
    if (!opt_entry) { return {}; }
    return opt_entry->heartbeat;
}

// generator<const Heartbeat&, Heartbeat> LifecycleManager::iterate_running_abilities() const {
//     for (const auto& [id, entry] : heartbeats) {
//         co_yield entry.heartbeat;
//     }
// }
std::vector<Heartbeat> LifecycleManager::get_running_abilities() const {
    std::vector<Heartbeat> res;
    res.reserve(heartbeats.size());
    heartbeats.for_each([&](const uuids::uuid&, const Entry& entry) {
        res.push_back(entry.heartbeat);
    });
    return res;
}
expected<TaskPtr, std::string> LifecycleManager::make_start_ability_request(
    const LifecycleRequest& request
) {
    // 检查能力是否存在
    // 只有start能力时,才去resourcemgr中询问是否有inactive的能力
    if (contains_active(request.abilityInstanceId)) {
        return make_unexpected("ability ", request.abilityInstanceId, " is already active");
    }

    auto res = send_sync(make_message(
                             "LifecycleMgr", "ResourceMgr", "find_ability_instance/id",
                             request.abilityInstanceId
                         ))
                   .transform_error(prefix_by("send message find ability instance failed: "))
                   .and_then(parse_if_is_success<AbilityCR>);

    if (!res) { return res.error(); }
    const auto& res_cr = res.value();

    auto res_storage = send_sync(make_message(
                                     "LifecycleMgr", "ResourceMgr", "find_ability_storage_info/id",
                                     request.abilityInstanceId
                                 ))
                           .transform_error(prefix_by("send message find ability storage failed: "))
                           .and_then(parse_if_is_success<msg_params::AbilityStorageInfo>);

    if (!res_storage) { return res_storage.error(); }
    auto& ability_storage_info = *res_storage;

    // 1. subprocessmgr中启动一个能力程序
    auto task_1 = tasks::atomic(
        "start-process",
        [this,
         args_for_process
         = gather_process_args(ability_storage_info, request.abilityInstanceId, res_cr),
         id = request.abilityInstanceId]() {
            // 向SubprocessMgr发送消息,启动该能力
            auto msg
                = make_message("LifecycleMgr", "SubprocessMgr", "start_process", args_for_process);
            msg.set_extra<ProcessExitCallback>([this, id](const uvw::exit_event&) {
                on_ability_exit(id);
            });
            auto res = send_sync(std::move(msg));

            if (!res) {
                LOG(ERROR) << "send_msg failed: " << res.error();
                throw std::runtime_error("send msg to subprocessmgr failed: " + res.error());
            }
            if (!res->success()) {
                LOG(ERROR) << "start process failed: "
                           << std::string_view(res->content.begin(), res->content.end());
                throw std::runtime_error(strjoin("start process failed", res->view()));
            }
            return;
        }
    );
    // 2. 等待直到这个能力发送心跳包
    auto task_2 = task_wait_for_heartbeat(request.abilityInstanceId);
    auto total_task = tasks::sequence("lifecycle-adjust", task_1, task_2);
    return total_task;
}

expected<TaskPtr, std::string> LifecycleManager::make_lifecycle_adjust_request(
    const LifecycleRequest& request
) {
    // 如果是其他调节任务,那么包括
    // 1. 向能力发送对应的生命周期操作
    if (!contains_active(request.abilityInstanceId)) {
        return make_unexpected("no such ability of id ", request.abilityInstanceId);
    }

    auto task_1 = task_send_lifecycle_operation(request);
    // 2. 等待能力发送的心跳包显示自身到达了目标状态
    auto task_2
        = task_wait_for_lifecycle_state(request.abilityInstanceId, desired_state(request.command));
    auto total_task = tasks::sequence("lifecycle-adjust", task_1, task_2);
    return total_task;
}

namespace {
expected<uuids::uuid, std::string> submit_task_local(TaskPtr task) {
    auto msg = make_message("LifecycleMgr", "TaskMgr", "add_task/raw");
    msg.set_extra(task);

    if (auto res = send_sync(std::move(msg)); !res) {
        return unexpected{"send message failed" + res.error()};
    }
    return task->id();
}
} // namespace

expected<uuids::uuid, std::string> LifecycleManager::on_lifecycle_request(
    const LifecycleRequest& request
) {

    if (request.command == "start") {
        return make_start_ability_request(request)
            .transform_error(prefix_by("make start task failed: "))
            .and_then(submit_task_as{"LifecycleMgr"});
    }
    if (request.command != "start") {
        return make_lifecycle_adjust_request(request)
            .transform_error(prefix_by("make adjust task failed: "))
            .and_then(submit_task_as{"LifecycleMgr"});
    }
    throw std::logic_error("unreachable");
}

// 实验内容,尚未测试稳定性
//  using MessageHandler
//      = std::function<expected<void, std::string>(LifecycleManager&, const
//      message_bus::Message&)>;
//
//  template <typename R, typename T, typename U>
//  using MemberPtr = R (T::*)(U);
//
//  template <typename T, typename U>
//  auto parse_and_delegate_to(MemberPtr<void, T, U> mptr) {
//      return
//          [mptr](T& the_module, const message_bus::Message& message) -> expected<void,
//          std::string> {
//              using U_Value = std::remove_cvref_t<U>;
//              auto data = message.parse_as<U_Value>();
//              (the_module.*mptr)(data);
//              return {};
//          };
//  }
//
//  template <typename T, typename U>
//  auto get_extra_and_delegate_to(MemberPtr<void, T, U> mptr) {
//      return
//          [mptr](T& the_module, const message_bus::Message& message) -> expected<void,
//          std::string> {
//              using U_Value = std::remove_cvref_t<U>;
//              const U_Value* extra_p = message.get_extra<U_Value>();
//              if (!extra_p) { return unexpected{"invalid extra data"}; }
//              (the_module.*mptr)(*extra_p);
//              return {};
//          };
//  }
//
//  const std::unordered_map<std::string, MessageHandler> MessageHandlerMap{
//      {"HeartbeatEvent", parse_and_delegate_to(&LifecycleManager::on_heartbeat)},
//      {"HeartbeatEvent/raw", get_extra_and_delegate_to(&LifecycleManager::on_heartbeat)}
//  };

namespace {
template <typename T>
constexpr auto into = []<class U>(U&& x) { return static_cast<T>(std::forward<U>(x)); };

nlohmann::json to_json_array(auto&& r) {
    auto res = nlohmann::json::array();
    for (auto&& x : r) {
        res.push_back(x);
    }
    return res;
}
} // namespace

using namespace std::views;

namespace {
constexpr std::chrono::minutes MAX_HEARTBEAT_VALID_TIME{1};
};

void LifecycleManager::clear_stale_heartbeats() {
    auto now = Clock::now();
    std::vector<uuids::uuid> stale_ids;
    heartbeats.for_each([&](const uuids::uuid& id, const Entry& entry) {
        if (now - entry.last_update > MAX_HEARTBEAT_VALID_TIME) { stale_ids.push_back(id); }
    });
    for (const auto& id : stale_ids) {
        LOG(INFO) << "herartbeat " << to_string(id) << " is stale, clear it";
        heartbeats.erase(id);
    }
}

namespace {

nlohmann::json read_json_by_content_type(const std::string& content_type, const std::string& data) {
    if (content_type == "application/json") { return nlohmann::json::parse(data); }     // 解析json
    if (content_type == "application/cbor") { return nlohmann::json::from_cbor(data); } // 解析cbor
    throw std::invalid_argument("invalid content-type: " + content_type);               // 抛出异常
}

std::string get_content_type(const httplib::Request& req) {
    return req.has_header("content-type") ? req.get_header_value("content-type")
                                          : "application/json";
}

void delegate_to_ability(
    LifecycleManager& mgr,
    uuids::uuid ability_instance_id,
    std::string_view method,
    const std::string& path,
    const httplib::Request& request,
    httplib::Response& response
) {
    using namespace std::literals;
    auto hb = mgr.get_heartbeat(ability_instance_id);
    if (!hb) {
        response.status = 404;
        return;
    }

    if (hb->abilityPort <= 0) {
        throw std::runtime_error(strjoin(
            "ability ", to_string(ability_instance_id), " has invalid ability_port ",
            hb->abilityPort
        ));
    }

    httplib::Client cli("localhost", hb->abilityPort);
    httplib::Result res;
    if (method == "get"sv) { res = cli.Get('/' + path); }
    else if (method == "post"sv) {
        res = cli.Post('/' + path, request.body, request.get_header_value("Content-Type"));
    }
    else if (method == "put"sv) {
        res = cli.Put('/' + path, request.body, request.get_header_value("Content-Type"));
    }
    else if (method == "delete"sv) {
        res = cli.Delete('/' + path, request.body, request.get_header_value("Content-Type"));
    }
    else { throw std::invalid_argument("invalid method type "s + std::string(method)); }
    if (!res) { throw std::runtime_error("connect to subability failed" + to_string(res.error())); }
    response.body = res->body;
    response.status = res->status;
    response.set_header("Content-Type", res->get_header_value("Content-Type"));
}

} // namespace
void build_api(std::shared_ptr<LifecycleManager> mgr, httplib::Server& server) {
    using httplib::Request, httplib::Response;
    CHECK_NOTNULL(mgr);
    server.Post("/api/ability-heartbeat", [mgr](const Request& req, Response& res) {
        auto content_type = get_content_type(req);                  // 获取请求的content-type
        auto j = read_json_by_content_type(content_type, req.body); // 解析请求的json
        auto hb = j.get<Heartbeat>();                               // 转换为Heartbeat
        bool accepted = mgr->on_heartbeat(hb);
        if (!accepted) {
            // 410 Gone: framework 不认识这个 instance_id。典型原因是发送方
            // 是上一代 framework 遗留的 python 僵尸进程。SDK 连续收到 410 后
            // 会自毁 (ability-py-sdk 的 HeartbeatMgr 做了 orphan 检测)。
            res.status = 410;
            res.set_content(
                R"({"error":"unknown instance","hint":"instance_id is not registered with this framework; sender should self-terminate"})",
                "application/json"
            );
            return;
        }
        res.set_content("OK", "text/plain");
    });
    server.Get("/api/ability-heartbeat", [mgr](const Request& req, Response& res) {
        nlohmann::json payload = nlohmann::json::array();
        {
            mgr->heartbeats.for_each([&](const uuids::uuid&, const LifecycleManager::Entry& entry) {
                payload.push_back(entry.heartbeat);
            });
        }
        res.set_content(payload.dump(2), "application/json");
    });
    server.Get("/api/ability-heartbeat/:id", [mgr](const Request& req, Response& res) {
        const std::string& id_str = req.path_params.at("id");
        auto id = uuids::uuid::from_string(id_str);
        if (!id) { throw std::invalid_argument("invalid id: " + id_str); }

        nlohmann::json payload;

        auto opt_entry = mgr->heartbeats.find(*id);
        if (!opt_entry) {
            res.status = 404;
            res.set_content("null", "application/json");
            return;
        }
        payload = opt_entry->heartbeat;

        res.set_content(payload.dump(2), "application/json");
    });
    server.Post("/api/lifecycle-request", [mgr](const Request& req, Response& res) {
        auto content_type = get_content_type(req);
        if (content_type != "application/json") {
            throw std::invalid_argument("unsupported content type");
        }
        LOG(WARNING) << "receive lifecycle request: " << req.body;
        LifecycleRequest request = nlohmann::json::parse(req.body).get<LifecycleRequest>();

        auto result = mgr->on_lifecycle_request(request);
        if (result) {
            res.set_content(nlohmann::json{{"taskId", *result}}.dump(), "application/json");
            return;
        }
        res.set_content(result.error(), "text/plain");
        res.status = 404;
        return;
    });
    // /api/ability/:id/<sub-path> → 转发到 instance 的 abilityPort
    //
    // 注意: cpp-httplib 一旦在 pattern 里看到 `(.*)` 就把整个模式当成纯正则,
    // 此时 `:id` 不再是 path-param 占位符, 而是字面 ":" "i" "d" 三字符。
    // 历史写法 `R"(/api/ability/:id/(.*))"` 会让 URL 必须含字面 `/:id/` 才匹配 —
    // 也就是从来没匹配过, 所有走代理的 task 调用都直接 404。
    //
    // 改成纯正则 + 两组 capture: 第 1 组吃 UUID (8-4-4-4-12 hex), 第 2 组吃 sub-path,
    // handler 走 req.matches[1..2]。
    constexpr const char* ABILITY_PROXY_PATTERN =
        R"(/api/ability/([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})/(.*))";

    auto make_proxy_handler = [mgr](std::string_view method) {
        return [mgr, method](const Request& req, Response& res) {
            std::string id_str = req.matches[1];
            auto id = uuids::uuid::from_string(id_str);
            if (!id) { throw std::invalid_argument("invalid id: " + id_str); }
            std::string subitem = req.matches[2];
            delegate_to_ability(*mgr, *id, method, subitem, req, res);
        };
    };
    server.Get(ABILITY_PROXY_PATTERN, make_proxy_handler("get"));
    server.Post(ABILITY_PROXY_PATTERN, make_proxy_handler("post"));
    server.Put(ABILITY_PROXY_PATTERN, make_proxy_handler("put"));
    server.Delete(ABILITY_PROXY_PATTERN, make_proxy_handler("delete"));
}
