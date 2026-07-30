// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "prelude.hpp"
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <string>
#include <uuid.h>
#include <yaml-cpp/yaml.h>
namespace electionmgr {
/// a Multiaddr is a slash-separated address string,
/// can be "/ip4/192.168.26.14", "/ip6/2406:280:1005:146e:495d:2b46:d7bc:f176" or other address families
using MultiAddr = std::string;

struct PeerInfo {
    uuids::uuid id; // framework id
    std::set<MultiAddr> addresses; // known IP addresses (possibly multiple)
};

struct ElectionConfig {
    std::string method;
    nlohmann::json params;
    friend void from_json(const nlohmann::json& j, ElectionConfig&);
    friend void to_json(nlohmann::json& j, const ElectionConfig&);
};

struct ElectionMsg {
    uuids::uuid sourceId; // id of the sender framework
    MultiAddr sourceAddr; // address of the sender framework
    // set a prefix name per communication method,
    // e.g. unique.send, unique.response, bully.challange, bully.victory
    std::string type;
    nlohmann::json detail; // omit empty; each kind has message-specific info
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

// this type represents how ElectionManager interacts with the outside, including sending/broadcasting messages, etc.
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
    // message callback; three return types mean no reply, a reply, or an exception
    virtual ElectionMsgRes on_election_message(const ElectionMsg&) = 0;
    virtual bool is_running() const = 0;
    virtual void start() = 0;
    virtual void finish() = 0;
    // factory method to get a concrete ElectionManager
    // @param self_id self uuid
    // @param method election method,currently supports bully,
    // @param params: per-election-method specific parameters
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
