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
    using HandlePtr = std::shared_ptr<uvw::process_handle>; // 进程句柄
    mutable std::recursive_mutex m;
    std::unordered_map<uuids::uuid, HandlePtr> handles;
    std::shared_ptr<uvw::loop> loop;
    void cleanup();

public:
    using LabelMap = std::unordered_map<std::string, std::string>;
    SubprocessManager(std::shared_ptr<uvw::loop> l)
        : loop(std::move(l)) {}

    /**
     * @param path 程序所在的路径
     * @param args 命令行参数
     * @param envs 环境变量(如果要设置)
     * @param on_exit 程序退出时调用的回调函数
     * @param labels 该进程附加的额外标签,用以判断进程类型
     * @return 一个uuid,表示该进程
     */
    uuids::uuid start_process(
        const std::string& path,
        std::span<const std::string> args,
        const std::unordered_map<std::string, std::string>& envs = {},
        ProcessExitCallback on_exit = nullptr,
        const LabelMap& labels = {}
    );
    // 将自己的api注册到http服务器
    friend void build_api(std::shared_ptr<SubprocessManager>, httplib::Server& server);

    void on_exit() override;

private:
    // 添加一个现成的进程句柄,它的data项如果有,则必须是一个LabelMap,否则行为未定义
    void add(const uuids::uuid& name, std::shared_ptr<uvw::process_handle> h) {
        std::lock_guard _lk(m);
        handles.emplace(name, h);
    }
    // 实现 Module接口
    std::string module_name() const override { return "SubprocessMgr"; }
    expected<void, std::string> on_receive(const message_bus::Message&) override;
};
