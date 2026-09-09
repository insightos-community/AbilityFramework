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
#include "prelude.hpp"
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <string>
#include <uuid.h>
#include <yaml-cpp/yaml.h>
namespace electionmgr {
/// 一个Multiaddr 是用斜杠分割的一串地址,
/// 可以为 "/ip4/192.0.2.14", "/ip6/2001:db8::14" 或其他的地址族
using MultiAddr = std::string;

struct PeerInfo {
    uuids::uuid id;                // 框架id
    std::set<MultiAddr> addresses; // 已知的ip地址(可能有多个)
};

struct ElectionConfig {
    std::string method;
    nlohmann::json params;
    friend void from_json(const nlohmann::json& j, ElectionConfig&);
    friend void to_json(nlohmann::json& j, const ElectionConfig&);
};

struct ElectionMsg {
    uuids::uuid sourceId; // 发送者框架所在的id
    MultiAddr sourceAddr; // 发送者框架所在的地址
    // 按照每种通讯方式设置前缀名,
    // 如 unique.send, unique.response, bully.challange, bully.victory
    std::string type;
    nlohmann::json detail; // omit empty, 每种消息具体信息
    friend void from_json(const nlohmann::json& j, ElectionMsg&);
    friend void to_json(nlohmann::json& j, const ElectionMsg&);
};

struct Timeout {};
struct Empty {};

using ElectionMsgRes = std::variant<Empty, ElectionMsg, ErrorMsg, Timeout>;

inline bool is_timeout(const ElectionMsgRes& m) {
    return std::holds_alternative<Timeout>(m);
}
inline bool is_ok(const ElectionMsgRes& m) {
    return std::holds_alternative<ElectionMsg>(m) || std::holds_alternative<Empty>(m);
}

ElectionMsgRes impl_send(const std::string& addr, const ElectionMsg& msg);
ElectionMsgRes impl_send(const PeerInfo& peer, const ElectionMsg& msg);

// 该类型表示了ElectionManager如何与外界交互, 包括发送信息, 广播信息等
//
struct ElectionInterface {
    virtual ElectionMsgRes send(const std::string& addr, const ElectionMsg& msg) {
        return ::electionmgr::impl_send(addr, msg);
    };
    virtual void broadcast(const ElectionMsg& msg) = 0;
    virtual ~ElectionInterface() = default;
};

struct ElectionManager {
    virtual ~ElectionManager() = default;
    virtual uuids::uuid id() const = 0;
    virtual std::optional<PeerInfo> leader() const = 0;
    // 消息回调, 三种返回类型分别表示无返回消息, 有返回消息或产生异常
    virtual ElectionMsgRes on_election_message(const ElectionMsg&) = 0;
    virtual bool is_running() const = 0;
    virtual void start() = 0;
    virtual void finish() = 0;
    // 工厂方法,获得具体的ElectionManager
    //  @param self_id 自己所在的uuid
    //  @param method 选举方法,目前支持 bully,
    //  @param params 每种选举方法特定的参数
    static std::unique_ptr<ElectionManager> from_config(
        std::string method,
        uuids::uuid self_id,
        const nlohmann::json& params,
        std::unique_ptr<ElectionInterface> intf
    );
};

inline bool self_is_leader(ElectionManager& m) {
    auto l = m.leader();
    if (!l) { return false; }
    return l->id == m.id();
}
} // namespace electionmgr
