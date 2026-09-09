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

#include "messagebus/messagebus.hpp"
#include "prelude.hpp"
#include "subprocessmgr/subprocess_mgr.hpp"
#include "util/jthread.hpp"
#include "util/make_uuid.hpp"
#include <glog/logging.h>
#include <nlohmann/json.hpp>
#include <uvw.hpp>
using namespace std::chrono_literals;
namespace {
void configure_glog(const char* argv0) {
    FLAGS_colorlogtostderr = true;
    FLAGS_alsologtostderr = true;
    FLAGS_max_log_size = 1024;              // 最大日志大小为100M
    FLAGS_stop_logging_if_full_disk = true; // 当磁盘被写满时，停止日志输出
    google::InitGoogleLogging(argv0);
    google::InstallFailureSignalHandler();
}

struct TestRequest {
    int arg1, arg2;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(TestRequest, arg1, arg2);
};

struct TestResponse {
    int result;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(TestResponse, result);
};

struct TestModule1 : public message_bus::Module {
    [[nodiscard]] std::string module_name() const override { return "test_module1"; };
    expected<void, std::string> on_receive(const message_bus::Message& message) override {
        LOG(INFO) << module_name() << " receives message of kind " << message.router.operation;
        if (message.router.operation == "event1") {
            // 这是一个非回复的消息
            // 对应模块应当处理该消息对应的操作
            return {};
        }
        else if (message.router.operation == "request1") {
            auto j = nlohmann::json::parse(message.content);
            auto request = j.get<TestRequest>();
            auto result = request.arg1 + request.arg2;
            TestResponse response{result};
            std::string data = nlohmann::json(response).dump(2);
            message.respond(data);
            return {};
        }
        else if (message.router.operation == "request_by_extra") {
            const auto* ex = message.get_extra<TestRequest>();
            if (!ex) { return unexpected{"no payload from extra"}; }
            const auto& request = *ex;
            auto result = request.arg1 + request.arg2;
            TestResponse response{result};
            std::string data = nlohmann::json(response).dump(2);
            message.respond(data);
            return {};
        }
        else if (message.router.operation == "test_timeout") {
            auto j = nlohmann::json::parse(message.content);
            auto request = j.get<TestRequest>();
            auto result = request.arg1 + request.arg2;
            TestResponse response{result};
            // 成心等待一段时间,直到它超时
            std::this_thread::sleep_for(30s);
            std::string data = nlohmann::json(response).dump(2);
            message.respond(data);
            return {};
        }
        return make_unexpected("unknown operation type");
    }
};

template <typename T>
void send_msg(
    const std::string& from, const std::string& to, const std::string& operation, T&& data
) {
    std::string serialized_data = nlohmann::json(std::forward<T>(data)).dump();
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
template <typename T>
auto send_msg_sync(
    const std::string& from, const std::string& to, const std::string& operation, T&& data
) {
    std::string serialized_data = nlohmann::json(std::forward<T>(data)).dump();
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
    return message_bus::send_sync(std::move(message));
}
template <typename T>
auto send_msg_sync_by_extra(
    const std::string& from, const std::string& to, const std::string& operation, T&& content
) {
    message_bus::Message message{
        .header{
            .id = make_uuid(),
            .parent_id{},
            .timestamp{std::chrono::system_clock::now()},
            .is_synchronous = false
        },
        .router{.source = from, .destination = to, .operation = operation}
    };
    message.set_extra(std::forward<T>(content));
    return message_bus::send_sync(std::move(message));
}
} // namespace

int main(int argc, const char** argv) {
    configure_glog(argv[0]);
    // 初始化 libuv 事件循环
    auto loop = uvw::loop::get_default();
    CHECK_NOTNULL(loop);
    auto timer = loop->resource<uvw::timer_handle>();
    timer->on<uvw::timer_event>([](const auto& ev, auto& self) { self.close(); });
    timer->start(20s, 0s);
    // 初始化模块
    auto test_mod_ptr = std::make_shared<TestModule1>();
    // 模块添加到总线
    message_bus::add_module(test_mod_ptr);
    // 启动事件循环
    jthread th_uv_loop([&loop]() {
        //message_bus::start(loop);
        LOG(INFO) << "loop run";
        loop->run();
        LOG(INFO) << "loop finish";
    });
    LOG(INFO) << "a message struct has " << sizeof(message_bus::Message) << " bytes";

    // 尝试发送单向消息
    LOG(WARNING) << "begin test simple msg";
    send_msg("another_module", "test_module1", "event1", "asdasd");
    
    // 尝试向task mgr发送
    LOG(WARNING) << "begin test simple msg";
    send_msg("another_module", "task_mgr_message_module", "event1", "asdasd");

    // 尝试发送不合法的消息类型
    LOG(WARNING) << "begin test unknown msg type";
    send_msg("another_module", "test_module1", "sone_unknown_msg_type", "asdasd");

    // 尝试发送不合法的目的地
    LOG(WARNING) << "begin test unknown destination";
    send_msg("another_module", "ASDASDAS", "sone_unknown_msg_type", "asdasd");
    {
        // 尝试发送请求消息
        LOG(WARNING) << "begin test sync msg";
        auto res = send_msg_sync(
            "another_module", "test_module1", "request1", TestRequest{.arg1 = 1, .arg2 = 2}
        );
        if (!res) { LOG(ERROR) << "send sync msg failed: " << res.error(); }
        else {
            LOG(INFO) << "send sync res response: "
                      << std::string_view(res->content.begin(), res->content.end());
        }
    }
    {
        // 测试额外消息
        LOG(WARNING) << "begin test sync msg with extra";
        auto res = send_msg_sync_by_extra(
            "another_module", "test_module1", "request_by_extra", TestRequest{3, 4}
        );
        if (!res) { LOG(ERROR) << "send sync msg failed: " << res.error(); }
        else {
            LOG(INFO) << "send sync res response: "
                      << std::string_view(res->content.begin(), res->content.end());
        }
    }
    {
        // 测试超时
        LOG(WARNING) << "begin test timeout";
        auto res = send_msg_sync(
            "another_module", "test_module1", "test_timeout", TestRequest{.arg1 = 1, .arg2 = 2}
        );
        // 分析结果
        if (!res) { LOG(ERROR) << "send sync msg failed: " << res.error(); }
        else {
            LOG(INFO) << "send sync res response: "
                      << std::string_view(res->content.begin(), res->content.end());
        }
    }
}
