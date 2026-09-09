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

// 发现方法
struct Methods {
    bool ipv4 = true;
    bool ipv6 = false;
};

// 队伍令牌
struct TeamToken {
    std::string teamName;              // 队伍名称
    uuids::uuid teamID;                // 队伍ID
    std::string secret;                // 队伍密钥
    std::string jwt;                   // jwt token
    int masterPort;                    // 端口号
    std::vector<std::string> masterIP; // 队长IP地址
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(TeamToken, teamName, teamID, secret, jwt, masterPort, masterIP);
};

class DiscoveryManager : public std::enable_shared_from_this<DiscoveryManager> {
public:
    DiscoveryManager(std::shared_ptr<uvw::loop> loop);
    // 从配置文件加载队伍信息
    bool load_team_config_from_file(std::filesystem::path config_path);
    // 从Redis加载队伍信息
    bool load_team_config_from_db();
    // 加载队伍信息
    void load_team_config(bool redis_enabled, const std::filesystem::path& config_path);
    // 启动接收广播包
    void start_receiving();
    // 启动定时发送数据
    void start_sending();
    // 发送队伍数据
    void send_team_data();
    // 发送IPv4广播包
    void send_ipv4_broadcast(const nlohmann::json& data);
    // 解析IPv4广播包
    void parse_ipv4_broadcast(const std::string& packet);
    // 发送IPv6广播包
    void send_ipv6_broadcast(const nlohmann::json& data);
    // 解析IPv6广播包
    void parse_ipv6_broadcast(const std::string& packet);
    // 保存数据到Redis
    void save_team_info_to_db();
    // 启动同步到Redis
    void start_sync_team_info_to_db(int interval_seconds);
    // 定期检查electionmgr, 自己是否是队长, 如果是,则打开mdns,如果不是,则关闭mdns
    void start_update_mdns_state();
    // 清理超时成员
    void start_cleanup_inactive_member(int timeout_seconds = 30, int interval_seconds = 15);

    // 创建队长信息
    void create_master_info(MultiMap::Master& master_info, const nlohmann::json& data);
    // 更新队长信息
    void update_master_info(MultiMap::Master& existing_master, const nlohmann::json& data);

    // 加入队伍
    expected<bool, ErrorMsg> join_team(uuids::uuid teamID, const std::string& jwt);
    // 创建队伍成员信息
    void create_member_info(MultiMap::Member& member_info, const nlohmann::json& data);
    // 更新队伍成员信息
    void update_member_info(MultiMap::Member& existing_member, const nlohmann::json& data);
    // 创建Peer信息
    void create_peer_info(MultiMap::Peer& peer_info, const nlohmann::json& data);
    // 更新Peer信息
    void update_peer_info(MultiMap::Peer& existing_peer, const nlohmann::json& data);

private:
    std::shared_ptr<uvw::loop> loop_; // 事件循环
    nlohmann::json data;              // IPv4广播数据
    nlohmann::json data6;             // IPv6广播数据

    std::string name;         // 框架名称
    uuids::uuid framework_id; // 框架ID

    bool master = false;                // 是否为队长
    TeamToken master_token;             // 队长的队伍信息
    std::vector<TeamToken> team_tokens; // 多个队伍信息
    MultiMap multi_map;                 // 保存多种信息
    std::recursive_mutex m_multi_map;   // 保护multi_map的mutex

    Methods method;                          // 发现方法配置
    std::vector<std::string> ipv4_addresses; // 设备的 IPv4 地址
    std::vector<std::string> ipv6_addresses; // 设备的 IPv6 地址

    // RedisHelper redis;   // Redis
    mdns_cpp::mDNS mdns; // mDNS

    std::unique_ptr<electionmgr::ElectionManager> election_mgr;

    friend void build_api(std::shared_ptr<DiscoveryManager> mgr, httplib::Server&);
    struct DiscoveryMgrElectionInterface;
    void init_election_mgr();
};
