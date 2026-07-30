// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "discoverymgr/election_mgr.hpp"
#include "discoverymgr/mdns.hpp"
#include "multi_map.hpp"
#include "prelude.hpp"
// #include "util/RedisHelper.hpp"
#include "util/discovery_utils.hpp"
#include "util/expected.hpp"
#include <iostream>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <uuid.h>
#include <uvw.hpp>
#include <variant>
#include <vector>

namespace httplib {
class Server;
}

// discovery method
struct Methods {
    bool ipv4 = true;
    bool ipv6 = false;
};

// team token
struct TeamToken {
    std::string teamName; // team name
    uuids::uuid teamID; // team ID
    std::string secret; // team secret
    std::string jwt;                   // jwt token
    int masterPort; // port number
    std::vector<std::string> masterIP; // leader IP address
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(TeamToken, teamName, teamID, secret, jwt, masterPort, masterIP);
};

class DiscoveryManager : public std::enable_shared_from_this<DiscoveryManager> {
public:
    DiscoveryManager(std::shared_ptr<uvw::loop> loop);
    // load team info from config file
    bool load_team_config_from_file(std::filesystem::path config_path);
    // load team info from Redis
    bool load_team_config_from_db();
    // load team info
    void load_team_config(bool redis_enabled, const std::filesystem::path& config_path);
    // start receiving broadcast packets
    void start_receiving();
    // start periodic data sending
    void start_sending();
    // send team data
    void send_team_data();
    // send IPv4 broadcast packet
    void send_ipv4_broadcast(const nlohmann::json& data);
    // parse IPv4 broadcast packet
    void parse_ipv4_broadcast(const std::string& packet);
    // send IPv6 broadcast packet
    void send_ipv6_broadcast(const nlohmann::json& data);
    // parse IPv6 broadcast packet
    void parse_ipv6_broadcast(const std::string& packet);
    // save data to Redis
    void save_team_info_to_db();
    // start syncing to Redis
    void start_sync_team_info_to_db(int interval_seconds);
    // periodically check electionmgr whether self is the leader; if yes enable mdns, if no disable mdns
    void start_update_mdns_state();
    // clean up timed-out members
    void start_cleanup_inactive_member(int timeout_seconds = 30, int interval_seconds = 15);

    // create leader info
    void create_master_info(MultiMap::Master& master_info, const nlohmann::json& data);
    // update leader info
    void update_master_info(MultiMap::Master& existing_master, const nlohmann::json& data);

    // join team
    expected<bool, ErrorMsg> join_team(uuids::uuid teamID, const std::string& jwt);
    // create team member info
    void create_member_info(MultiMap::Member& member_info, const nlohmann::json& data);
    // update team member info
    void update_member_info(MultiMap::Member& existing_member, const nlohmann::json& data);
    // create peer info
    void create_peer_info(MultiMap::Peer& peer_info, const nlohmann::json& data);
    // update peer info
    void update_peer_info(MultiMap::Peer& existing_peer, const nlohmann::json& data);

private:
    std::shared_ptr<uvw::loop> loop_; // event loop
    nlohmann::json data; // IPv4broadcast data
    nlohmann::json data6; // IPv6broadcast data

    std::string name; // framework name
    uuids::uuid framework_id; // framework ID

    bool master = false; // whether is the leader
    TeamToken master_token; // leader team info
    std::vector<TeamToken> team_tokens; // multiple team tokens
    MultiMap multi_map; // savemultiple kindsinfo
    std::recursive_mutex m_multi_map; // mutex protecting multi_map

    Methods method; // discovery method config
    std::vector<std::string> ipv4_addresses; // device IPv4 address
    std::vector<std::string> ipv6_addresses; // device IPv6 address

    // RedisHelper redis;   // Redis
    mdns_cpp::mDNS mdns; // mDNS

    std::unique_ptr<electionmgr::ElectionManager> election_mgr;

    friend void build_api(std::shared_ptr<DiscoveryManager> mgr, httplib::Server&);
    struct DiscoveryMgrElectionInterface;
    void init_election_mgr();
};
