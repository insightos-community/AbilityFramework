// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

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
    // leader
    struct Master {
        std::string name; // framework name
        int port; // port number
        std::string teamName; // team name
        uuids::uuid teamID; // team ID
        std::vector<std::string> ipv4; // ipv4 address; can store multiple
        std::vector<std::string> ipv6; // ipv6 address; can store multiple
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Master, name, port, teamName, teamID, ipv4, ipv6);
    };
    std::unordered_map<std::string, Master> TeamMaster;

    struct MasterTeam {
        std::string teamName; // team name
        uuids::uuid teamID; // team ID
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(MasterTeam, teamName, teamID);
    };

    // team member
    struct Member {
        std::string name; // framework name
        int port; // port number
        std::string jwt;  // jwt token
        int expiry; // validity period, seconds; -1 means permanent
        std::time_t last_updated; // last updated time (timestamp)
        std::vector<std::string> ipv4; // ipv4 address; can store multiple
        std::vector<std::string> ipv6; // ipv6 address; can store multiple
        MasterTeam master_team; 
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Member, name, port, jwt, expiry, last_updated, ipv4, ipv6, master_team);
    };
    std::unordered_map<std::string, Member> TeamMember;

    // Peer
    struct Peer {
        std::string name; // framework name
        int port; // port number
        std::time_t last_updated; // last updated time (timestamp)
        std::vector<std::string> ipv4; // ipv4 address; can store multiple
        std::vector<std::string> ipv6; // ipv6 address; can store multiple
        std::string teamName; // team name
        uuids::uuid teamID; // team ID
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Peer, name, port, last_updated, ipv4, ipv6, teamName, teamID);
    };
    std::unordered_map<std::string, Peer> Peers;
    
};

#endif
