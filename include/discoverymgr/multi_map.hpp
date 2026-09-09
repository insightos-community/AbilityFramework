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

#ifndef MULTI_MAP_HPP
#define MULTI_MAP_HPP

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include "util/uuid_json_convert.hpp"

class MultiMap {
public:
    // 队长
    struct Master {
        std::string name;      // 框架名称
        int port;              // 端口号
        std::string teamName;  // 队伍名称
        uuids::uuid teamID;    // 队伍ID
        std::vector<std::string> ipv4; // ipv4地址 可存储多个地址
        std::vector<std::string> ipv6; // ipv6地址 可存储多个地址
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Master, name, port, teamName, teamID, ipv4, ipv6);
    };
    std::unordered_map<std::string, Master> TeamMaster;

    struct MasterTeam {
        std::string teamName;  // 队伍名称
        uuids::uuid teamID;    // 队伍ID
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(MasterTeam, teamName, teamID);
    };

    // 队伍成员
    struct Member {
        std::string name; // 框架名称
        int port;         // 端口号
        std::string jwt;  // jwt token
        int expiry;       // 有效期，秒，-1表示永久有效
        std::time_t last_updated;      // 上次更新时间（时间戳）
        std::vector<std::string> ipv4; // ipv4地址 可存储多个地址
        std::vector<std::string> ipv6; // ipv6地址 可存储多个地址
        MasterTeam master_team; 
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Member, name, port, jwt, expiry, last_updated, ipv4, ipv6, master_team);
    };
    std::unordered_map<std::string, Member> TeamMember;

    // Peer
    struct Peer {
        std::string name;              // 框架名称
        int port;                      // 端口号
        std::time_t last_updated;      // 上次更新时间（时间戳）
        std::vector<std::string> ipv4; // ipv4地址 可存储多个地址
        std::vector<std::string> ipv6; // ipv6地址 可存储多个地址
        std::string teamName;          // 队伍名称
        uuids::uuid teamID;            // 队伍ID
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Peer, name, port, last_updated, ipv4, ipv6, teamName, teamID);
    };
    std::unordered_map<std::string, Peer> Peers;
    
};

#endif
