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

#include "ability_client.hpp"
#include "heartbeat.hpp"
#include "lifecyclemgr/lifecycle_request.hpp"
#include "messagebus/basic_module.hpp"
#include "param/task_id_wrapper.hpp"
#include "taskmgr/task.hpp"
#include "util/concurrent_map.hpp"
#include "util/expected.hpp"
#include "util/generator.hpp"
#include <mutex>
#include <unordered_map>

#ifndef HEARTBEAT_SHARD_COUNT
#define HEARTBEAT_SHARD_COUNT 8
#endif

namespace httplib {
class Server;
}

class LifecycleManager : public message_bus::BasicModule<LifecycleManager> {
    using Clock = std::chrono::system_clock;
    using Timepoint = Clock::time_point;
    struct Entry {
        Heartbeat heartbeat;
        Timepoint last_update;
        // 如果该能力没有RUNNING过,该项为空
        std::optional<Timepoint> last_connect;
        AbilityClientPtr client; // 这个能力所对应的client
    };
    mutable std::recursive_mutex m;
    util::ShardedLockMap<uuids::uuid, Entry, HEARTBEAT_SHARD_COUNT> heartbeats;

public:
#ifdef ENABLE_HEARTBEAT_PERF_ANALYSIS
    static void log_perf(const char* func, double wait_us, double hold_us);
#endif
    // 为实现 Module 接口
    std::string module_name() const override { return "LifecycleMgr"; }

    // 返回 true 表示心跳被接受 (instance_id 在 ResourceMgr 的
    // AbilityInstance 表里命中); false 表示来源是孤儿进程, HTTP 层应回
    // 410 Gone 让 SDK 自毁。本方法只在 accepted=true 时更新本地心跳 map,
    // 避免孤儿被 /api/ability-heartbeat 列表泄漏出去。
    [[nodiscard]] bool on_heartbeat(const Heartbeat& heartbeat);
    /// 生成一个lifecycle_request类型的任务并交付执行,
    ///  @return 如果成功,返回这个任务的id
    expected<uuids::uuid, std::string> on_lifecycle_request(const LifecycleRequest& request);
    friend void build_api(std::shared_ptr<LifecycleManager>, httplib::Server&);

    [[nodiscard]]
    bool contains_active(uuids::uuid instance_id) const;
    std::optional<Heartbeat> get_heartbeat(uuids::uuid instance_id) const;

    void clear_stale_heartbeats();

    void add_message_handlers() override {
        // 注: on_heartbeat 不再注册为 ON_EVENT。它的唯一调用入口是 HTTP
        // POST /api/ability-heartbeat handler 直接调用 mgr->on_heartbeat(hb),
        // 不走 msgbus。msgbus 上的 HeartbeatEvent 只由 LifecycleMgr 主动
        // send_sync 发给 ResourceMgr, 不回流到 LifecycleMgr 自己。
        ON_QUERY("lifecycle_request", on_query_lifecycle_request);
        ON_QUERY("get_heartbeat/id", get_heartbeat);
        ON_QUERY("running_abilities", get_running_abilities);
    }

private:
    TaskPtr task_send_lifecycle_operation(const LifecycleRequest&);
    TaskPtr task_wait_for_lifecycle_state(const uuids::uuid& instance_id, LifecycleState desired);
    TaskPtr task_wait_for_heartbeat(const uuids::uuid& instance_id);
    void on_ability_exit(uuids::uuid instance_id);
    /// @param instance_id 故障能力的实例id
    void try_inform_parent_ability(uuids::uuid instance_id);
    expected<TaskPtr, std::string> make_start_ability_request(const LifecycleRequest& request);
    expected<TaskPtr, std::string> make_lifecycle_adjust_request(const LifecycleRequest& request);
    /// 遍历heartbeats 以取得心跳包
    /// 注意,这个生成器不是线程安全的,请先加锁再使用
    // generator<const Heartbeat&, Heartbeat> iterate_running_abilities() const;

    expected<msg_params::TaskIdWrapper, ErrorMsg> on_query_lifecycle_request(
        const LifecycleRequest& request
    ) {

        return on_lifecycle_request(request).transform([](uuids::uuid id) {
            return msg_params::TaskIdWrapper(id);
        });
    }
    std::vector<Heartbeat> get_running_abilities() const;
};
