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

#include "messagebus/message_client.hpp"
#include "prelude.hpp"
#include "taskmgr/common_tasks.hpp"
#include "taskmgr/task.hpp"
#include "taskmgr/task_mgr.hpp"
#include <glog/logging.h>
#include <uvw/async.h>
#include <uvw/idle.h>
#include <uvw/timer.h>

TaskManager::TaskManager(std::shared_ptr<uvw::loop> loop)
    : timer_handler(loop->resource<uvw::timer_handle>()) {
    CHECK_NOTNULL(timer_handler);
    timer_handler->on<uvw::timer_event>([this](uvw::timer_event&, uvw::timer_handle& self) {
        update();
    });
    LOG(WARNING) << "TaskManager start idle handler";
    using namespace std::chrono_literals;
    timer_handler->start(500ms, 30ms);
};

// 检查每个任务的状况,如果空闲且就绪,就使其执行一次
void TaskManager::update() {
    std::lock_guard _lk(m);
    for (auto it = active_handles.begin(); it != active_handles.end();) {
        auto& handle = it->second;
        // add() stores TaskPtr (shared_ptr<TaskInterface>) in shared_ptr<void>.
        // Recover that exact pointer type: virtual inheritance can place the
        // TaskInterface subobject at a different address from TaskBase on MSVC.
        auto task = handle->data<TaskInterface>();
        auto state = task->state();
        switch (state) {
        case TaskState::unstarted:
        case TaskState::running:
        case TaskState::fine: {
            handle->send(); // 给一轮时间使其执行
            ++it;
            continue;
        } break;
        case TaskState::finished:
        case TaskState::error:
        case TaskState::cancelled: {
            handle->close();
            it = active_handles.erase(it); // 清除该句柄
        };
        default: {
            // 这个任务所依赖的条件还没有准备好
        } break;
        }
    }
}

// 添加任务
void TaskManager::add(TaskPtr task) {
    std::lock_guard _lk(m);
    CHECK_NOTNULL(task);
    auto id = task->id();
    tasks[id] = task;
    last_updated[id] = Clock::now();
    auto handle = timer_handler->parent().resource<uvw::async_handle>();
    handle->data(task);
    handle->on<uvw::close_event>([](uvw::close_event& ev, uvw::async_handle& self) {
        auto task = self.data<TaskInterface>();
        auto s = task->state();
        if (s == TaskState::finished) {
            LOG(WARNING) << "task name=" << task->name() << ", id=" << task->id() << " finished";
        }
        else if (s == TaskState::error) {
            if (!task->error()) {
                LOG(ERROR) << "the task " << task->name()
                           << " errored without setting an exception ptr";
                return;
            }
            try {
                std::rethrow_exception(task->error());
            }
            catch (std::exception& e) {
                LOG(ERROR) << "task name=" << task->name() << ", id=" << task->id()
                           << " errored: " << e.what();
            }
            catch (...) {
                LOG(ERROR) << "task name=" << task->name() << ", id=" << task->id()
                           << " errored: Unknown Error";
            }
        }
    });
    handle->on<uvw::async_event>([task](uvw::async_event& ev, uvw::async_handle& self) {
        // LOG(INFO) << "task name=" << task->name() << ", id=" << task->id() << " get execution
        // time";
        auto s = task->resume();
    });
    active_handles[id] = handle;
}
TaskPtr TaskManager::at(uuids::uuid id) const {
    std::lock_guard _lk(m);
    auto it = tasks.find(id);
    if (it == tasks.end()) { return nullptr; }
    return it->second;
}
expected<void, ErrorMsg> TaskManager::add_task_factory(
    const std::string& task_type, TaskFactory factory
) {
    std::lock_guard _lk(m);
    if (!factory) {
        auto msg = strjoin("adding empty factory for ", task_type);
        LOG(ERROR) << msg;
        return ::semantic_expected::unexpected{msg};
    }
    task_factories[task_type] = std::move(factory);
    LOG(INFO) << "added task facotory for " << task_type;
    return {};
}

// 构造api
void build_api(std::shared_ptr<TaskManager> task_entry, httplib::Server& server) {
    using httplib::Request, httplib::Response;
    using std::views::transform, std::views::keys;

    // 定义 GET /api/task 接口，返回所有任务的 UUID 列表
    server.Get("/api/task", [task_entry](const Request& req, Response& res) {
        std::lock_guard _lk(task_entry->m);
        nlohmann::json payload; // 创建一个 JSON 对象用于存储任务 UUID 列表
        if (req.params.contains("active")) { // 如果请求参数中包含 active 字段
            for (auto& [uuid, _] : task_entry->active_handles) {
                payload.push_back(to_string(uuid));
            }
        }
        else {
            for (auto& [uuid, _] : task_entry->tasks) {
                payload.push_back(to_string(uuid));
            }
        }
        res.set_content(payload.dump(2), "application/json");
    });

    // 定义 GET /api/task/:id 接口，返回指定 UUID 任务的状态
    server.Get("/api/task/:id", [task_entry](const Request& req, Response& res) {
        std::lock_guard _lk(task_entry->m);
        auto uuid = uuids::uuid::from_string(req.path_params.at("id")); // 从请求路径中获取 UUID
        if (!uuid) { throw std::invalid_argument("input is not valid uuid"); }
        auto it = task_entry->tasks.find(*uuid); // 使用std::map 中的find方法查找任务
        if (it == task_entry->tasks.end()) {
            throw make_error<std::invalid_argument>("no such task of uuid ", to_string(*uuid));
        }
        // 将任务状态转换为 JSON 格式并响应
        res.set_content(it->second->status().dump(2), "application/json");
    });

    // test   message_bus
    server.Get("/api/message", [](const Request& req, Response& res) {
        // 构造 JSON 对象
        nlohmann::json data = {
            {"name", "test_task"},
            {"type", "atomic"},
            {"function", "hello world"},
        };
        // 将 JSON 对象格式化为字符串并输出到控制台
        VLOG(1) << "尝试向task mgr发送data: " << data.dump(2);
        send_msg("another_module", "TaskMgr", "add_task/json", data);
        res.set_content("ok, GET /api/message", "text/plain");
    });

    // 定义 GET /api/extra 接口，返回 "ok"
    server.Get("/api/extra", [](const Request& req, Response& res) {
        // std::cout << "GET /api/extra" << std::endl;

        // 创建一个原子任务
        TaskPtr task
            = tasks::atomic("test_task_extra", []() { std::cout << "hello extra" << std::endl; });

        // 发送消息
        send_extra_msg("another_module", "TaskMgr", "add_task/raw", task);

        // 响应 "ok"
        res.set_content("ok, GET /api/extra", "text/plain");
    });
    // 定义 Post /api/task 接口，用于创建任务
    server.Post("/api/task", [task_entry](const Request& req, Response& res) {
        auto j = nlohmann::json::parse(req.body);
        if (!j.contains("task_type")) { throw std::invalid_argument("need 'task_type'"); }
        if (!j.at("task_type").is_string()) {
            throw std::invalid_argument("need 'task_type' be string");
        }
        auto task_type = j.at("task_type").get<std::string>();

        std::lock_guard _lk(task_entry->m);
        auto it = task_entry->task_factories.find(task_type);
        if (it == task_entry->task_factories.end()) {
            throw std::invalid_argument("unknown task type: " + task_type);
        }
        auto& factory = it->second;
        nlohmann::json payload_params = j.value("payload", nlohmann::json{});
        try {
            TaskPtr task = factory(payload_params);
            if (!task) {
                throw std::runtime_error(strjoin(
                    "during creating task of type ", task_type, ", factory returned empty taskptr"
                ));
            }
            task_entry->add(task);

            nlohmann::json payload;
            payload["taskId"] = to_string(task->id());

            res.set_content(payload.dump(), "application/json");
            return;
        }
        catch (std::exception& e) {
            LOG(ERROR) << "error making task " << task_type << ": " << e.what();
            throw;
        }
    });

    task_mgr::add_task_factory("test.hello", [](const nlohmann::json& j) -> TaskPtr {
        int a = j.at("a");
        int b = j.at("b");
        return tasks::atomic_with_return_value("test-add", [a, b]() -> nlohmann::json {
            int res = a + b;
            return nlohmann::json(res);
        });
    });
}

// 接收消息
expected<void, std::string> TaskManager::on_receive(const message_bus::Message& message) {

    if (message.router.operation == "add_task/json") {
        auto j = nlohmann::json::parse(message.content);
        VLOG(1) << "receive data:" << j.dump(2);

        // 从 JSON 中提取参数
        std::string name = j.at("name").get<std::string>();
        std::string type = j.at("type").get<std::string>();
        std::string function_code = j.at("function").get<std::string>();
        // 如何转换为函数?
        // 只传输需要的函数参数，在创建任务时候向预设的函数传递参数

        if (type == "atomic") {
            // 创建任务
            auto atomic_task
                = tasks::atomic(name, []() { std::cout << "hello world" << std::endl; });

            // 提交任务到任务管理器
            add(atomic_task);
        }
        else if (type == "sequence") {
            // 创建任务

            // auto sequence_task = tasks::sequence(name,vector);
            //  提交任务到任务管理器
            // add(sequence_task);

            // 相关定义
            // TaskPtr sequence(std::string_view name, std::vector<TaskPtr> tasks) {
            //     return std::make_shared<SequentialTask>(std::move(tasks), name);
        }
        else if (type == "parallel") {
            // 创建任务
            // 提交任务到任务管理器
            // add(parallel_task);
        }
        else { return ::semantic_expected::unexpected{"Unknown task type"}; }
        return {};
    }

    // 解析json得到任务类型,但是不立刻执行它  make_task
    else if (message.router.operation == "make_task/json") {
        VLOG(1) << "receive: 'make_task/json'";
        auto j = nlohmann::json::parse(message.content);
        VLOG(1) << "receive data:" << j.dump(2);
        std::string name = j.at("name").get<std::string>();
        std::string type = j.at("type").get<std::string>(); // 任务类型

        // json转为TaskPtr

        return {};
    }

    // add_task/raw extra字段获取一个TaskPtr,然后执行
    else if (message.router.operation == "add_task/raw") {
        VLOG(1) << "receive: 'add_task/raw'";
        // 获取extra字段
        std::shared_ptr<message_bus::AbstractExtra> shared_extra = std::move(message.extra);
        // 动态转换为 message_bus::Extra<TaskPtr>
        auto extra = std::dynamic_pointer_cast<message_bus::Extra<TaskPtr>>(shared_extra);
        if (!extra) { return ::semantic_expected::unexpected{"extra is not TaskPtr"}; }
        TaskPtr task = extra->content;
        // 提交任务到任务管理器
        add(task);
        message.respond("OK");
        return {};
    }
    else if (message.router.operation == "add_task_factory/raw") {
        auto* extra = message.get_extra<TaskFactory>();
        if (!extra) { return ::semantic_expected::unexpected{"extra is not TaskFactory"}; }
        nlohmann::json payload = nlohmann::json::parse(message.view());
        if (!payload.contains("task_type")) { return ::semantic_expected::unexpected{"need task_type in payload"}; }
        std::string task_type = payload.at("task_type");

        auto res = add_task_factory(task_type, std::move(*extra));
        if (!res) {
            message.respond_error(res.error());
            return {};
        }
        message.respond("OK");
        return {};
    }

    return ::semantic_expected::unexpected{"Unknown operation"};
};

// 测试发送message
void send_msg(
    const std::string& from,
    const std::string& to,
    const std::string& operation,
    nlohmann::json& data
) {
    std::string serialized_data = nlohmann::json(data).dump();
    message_bus::Message message{
        .header{
            .id = make_uuid(),
            .parent_id{},
            .timestamp{std::chrono::system_clock::now()},
            .is_synchronous = false
        },
        .router{.source = from, .destination = to, .operation = operation},
        .content{serialized_data.begin(), serialized_data.end()}
    };
    message_bus::send(message);
}

// 测试发送message with extra
void send_extra_msg(
    const std::string& from, const std::string& to, const std::string& operation, TaskPtr task
) {
    // std::string serialized_data = nlohmann::json(data).dump();
    auto extra = std::make_unique<message_bus::Extra<TaskPtr>>(task);
    message_bus::Message message{
        .header{
            .id = make_uuid(),
            .parent_id{},
            .timestamp{std::chrono::system_clock::now()},
            .is_synchronous = false
        },
        .router{.source = from, .destination = to, .operation = operation},
        //.content{serialized_data.begin(), serialized_data.end()},
        .extra = std::move(extra)
    };
    message_bus::send(message);
}

namespace task_mgr {
void add_task_factory(const std::string& task_type, TaskFactory task_factory) {
    if (!task_factory) { throw std::invalid_argument("empty task factory"); }
    nlohmann::json payload;
    payload["task_type"] = task_type;
    auto msg = make_message("unknown", "TaskMgr", "add_task_factory/raw", payload);
    msg.set_extra(std::move(task_factory));
    auto res = send_sync(std::move(msg));
    if (!res) { LOG(ERROR) << __func__ << " send message failed: " << res.error(); }
    if (!res->success()) { LOG(ERROR) << __func__ << " failed: error response: " << res->view(); }
}

} // namespace task_mgr
