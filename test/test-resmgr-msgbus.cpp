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
#include "resourcemgr/resource_mgr.hpp"
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
    auto test_mod_ptr = std::make_shared<ResourceManager>();
    test_mod_ptr->update();
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
    LOG(WARNING) << "begin test sync msg";
    uuids::uuid id = make_uuid();
    auto res = send_msg_sync( "test", "ResourceMgr", "find_ability_instance/id", id );
    if (!res) { LOG(ERROR) << "send sync msg failed: " << res.error(); }
    else {
        nlohmann::json res_json = nlohmann::json::parse(res->content);
        LOG(INFO) << "send sync res response: " << res_json.dump(4);
    }
}
