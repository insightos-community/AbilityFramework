// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

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
        // empty if this ability has never reached RUNNING
        std::optional<Timepoint> last_connect;
        AbilityClientPtr client; // the client corresponding to this ability
    };
    mutable std::recursive_mutex m;
    util::ShardedLockMap<uuids::uuid, Entry, HEARTBEAT_SHARD_COUNT> heartbeats;

public:
#ifdef ENABLE_HEARTBEAT_PERF_ANALYSIS
    static void log_perf(const char* func, double wait_us, double hold_us);
#endif
    // to implement the Module interface
    std::string module_name() const override { return "LifecycleMgr"; }

    // returns true if the heartbeat is accepted (instance_id hits in ResourceMgr's
    // AbilityInstance table); false means the source is an orphan process, HTTP layer should return
    // 410 Gone so the SDK self-destructs. This method only updates the local heartbeat map when accepted=true,
    // preventing orphans from leaking into the /api/ability-heartbeat list.
    [[nodiscard]] bool on_heartbeat(const Heartbeat& heartbeat);
    /// create a lifecycle_request task and dispatch it for execution,
    /// @return on success, return the task id
    expected<uuids::uuid, std::string> on_lifecycle_request(const LifecycleRequest& request);
    friend void build_api(std::shared_ptr<LifecycleManager>, httplib::Server&);

    [[nodiscard]]
    bool contains_active(uuids::uuid instance_id) const;
    std::optional<Heartbeat> get_heartbeat(uuids::uuid instance_id) const;

    void clear_stale_heartbeats();

    void add_message_handlers() override {
        // note: on_heartbeat is no longer registered as ON_EVENT. Its only entry point is HTTP
        // POST /api/ability-heartbeat handler calls mgr->on_heartbeat(hb) directly,
        // not via msgbus. HeartbeatEvent on the msgbus is only sent actively by LifecycleMgr
        // via send_sync to ResourceMgr, and does not flow back to LifecycleMgr itself.
        ON_QUERY("lifecycle_request", on_query_lifecycle_request);
        ON_QUERY("get_heartbeat/id", get_heartbeat);
        ON_QUERY("running_abilities", get_running_abilities);
    }

private:
    TaskPtr task_send_lifecycle_operation(const LifecycleRequest&);
    TaskPtr task_wait_for_lifecycle_state(const uuids::uuid& instance_id, LifecycleState desired);
    TaskPtr task_wait_for_heartbeat(const uuids::uuid& instance_id);
    void on_ability_exit(uuids::uuid instance_id);
    /// @param instance_id instance id of the failed ability
    void try_inform_parent_ability(uuids::uuid instance_id);
    expected<TaskPtr, std::string> make_start_ability_request(const LifecycleRequest& request);
    expected<TaskPtr, std::string> make_lifecycle_adjust_request(const LifecycleRequest& request);
    /// iterate heartbeats to obtain heartbeat packets
    /// note: this generator is not thread-safe; lock before use
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
