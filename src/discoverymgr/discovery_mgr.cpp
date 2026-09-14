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

#include "discoverymgr/discovery_mgr.hpp"
#include "discoverymgr/jwt.hpp"
#include "util/global_vars.hpp"
#include "util/parse_addr.hpp"
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <glog/logging.h>
#include <httplib.h>
#include <iostream>
#include <thread>
#include <yaml-cpp/yaml.h>

namespace {

// 读取JSON数据，根据Content-Type解析
nlohmann::json read_json_by_content_type(const std::string& content_type, const std::string& data) {
    if (content_type == "application/json") { return nlohmann::json::parse(data); }
    if (content_type == "application/cbor") { return nlohmann::json::from_cbor(data); }
    throw std::invalid_argument("invalid content-type: " + content_type);
}

// 获取请求的Content-Type
std::string get_content_type(const httplib::Request& req) {
    LOG(WARNING) << "on get header value:";
    if (req.has_header("content-type")) { return req.get_header_value("content-type"); }
    return "application/json";
}

// 获取自身的JSON信息
nlohmann::json self_json() {
    nlohmann::json res;
    res["ipv4"].push_back("127.0.0.1");
    res["port"] = global_vars::get_config<int>("/http_port", 8080);
    res["name"] = global_vars::get_config<std::string>("/framework_name");
    res["id"] = to_string(global_vars::framework_id());
    return res;
}

struct ElectionMsgResVisitor {
    using Res = std::optional<electionmgr::ElectionMsg>;
    Res operator()(electionmgr::Empty) const { return {}; }
    Res operator()(electionmgr::Timeout) const {
        throw std::runtime_error("election mgr didn't return valid reply");
    }
    Res operator()(const ErrorMsg& emsg) const { throw std::runtime_error(emsg); }
    Res operator()(const electionmgr::ElectionMsg& msg) const { return msg; }
    Res operator()(electionmgr::ElectionMsg&& msg) const { return std::move(msg); }
};

std::string to_multiaddr(const std::string& s) {
    if (is_valid_ipv4(s)) { return "/ip4/" + s; }
    if (is_valid_ipv6(s)) { return "/ip6/" + s; }
    return s;
};

} // namespace

struct DiscoveryManager::DiscoveryMgrElectionInterface : public electionmgr::ElectionInterface {
    using EMsg = electionmgr::ElectionMsg;
    DiscoveryManager* w_dmgr;
    std::thread th_broadcast;
    std::atomic<bool> running = true;
    std::mutex m;
    std::condition_variable cv;
    EMsg msg_buf;
    DiscoveryMgrElectionInterface(DiscoveryManager* p)
        : w_dmgr(p) {}
    DiscoveryMgrElectionInterface(const DiscoveryMgrElectionInterface&) = delete;
    DiscoveryMgrElectionInterface& operator=(const DiscoveryMgrElectionInterface&) = delete;
    ~DiscoveryMgrElectionInterface() {
        running = false;
        cv.notify_all();
    }
    void broadcast(const EMsg& msg) override {
        {
            std::lock_guard _lk(m);
            msg_buf = msg;
        }
        cv.notify_one();
    }
    void work_routine() {
        while (running) {
            std::unique_lock _lk(m);
            cv.wait(_lk);
            if (!running) { return; }
            auto msg = std::move(msg_buf);
            work_broadcast(w_dmgr, std::move(msg));
        }
    }

    static void work_broadcast(DiscoveryManager* dmgr, EMsg msg) {
        if (!dmgr) {
            LOG(ERROR) << "discovery_mgr is empty";
            return;
        }
        LOG(INFO) << "election broadcast";
        std::unordered_map<std::string, MultiMap::Peer> peers;
        {
            std::lock_guard _lk(dmgr->m_multi_map);
            peers = dmgr->multi_map.Peers;
        }
        for (const auto& [id, minfo] : peers) {
            auto pinfo = make_pinfo(id, minfo);
            if (!pinfo) { continue; }
            impl_send(*pinfo, msg);
        }
    }
    static std::optional<electionmgr::PeerInfo> make_pinfo(
        std::string_view id, const MultiMap::Peer& minfo
    ) {
        auto opt_id = uuids::uuid::from_string(id);
        if (!opt_id) { return {}; }
        electionmgr::PeerInfo pinfo;
        std::string suffix;
        if (minfo.port != 8080) { suffix = strjoin("/http/", minfo.port); }
        pinfo.id = *opt_id;
        for (auto& ip4 : minfo.ipv4) {
            pinfo.addresses.insert(strjoin("/ip4/", ip4, suffix));
        }
        for (auto& ip6 : minfo.ipv6) {
            pinfo.addresses.insert(strjoin("/ip6/", ip6, suffix));
        }
        return {std::move(pinfo)};
    }
};
// api
void build_api(std::shared_ptr<DiscoveryManager> mgr, httplib::Server& server) {
    using httplib::Request, httplib::Response;

    // GET 返回自身ip地址
    server.Get("/api/discovery", [mgr](const Request& req, Response& res) {
        LOG(WARNING) << "On handling /api/discovery";
        nlohmann::json response_json;
        response_json["ipv4"] = mgr->ipv4_addresses;
        // response_json["ipv6"] = mgr->ipv6_addresses;
        res.set_content(response_json.dump(), "application/json");
        return;
    });

    // POST 返回id对应的ipv4地址
    server.Post("/api/discovery", [mgr](const Request& req, Response& res) {
        LOG(WARNING) << "On handling POST /api/discovery";
        nlohmann::json request_json;
        try {
            request_json = nlohmann::json::parse(req.body);
        }
        catch (const nlohmann::json::parse_error& e) {
            throw std::invalid_argument("Invalid JSON format");
        }
        if (!request_json.contains("id") || !request_json["id"].is_string()) {
            throw std::invalid_argument("Missing or invalid id");
        }

        std::string id = request_json["id"].get<std::string>();
        // 如果 ID 是当前框架的 ID，返回自身信息
        if (id == uuids::to_string(mgr->framework_id)) {
            nlohmann::json response_json;
            response_json["ipv4"] = mgr->ipv4_addresses;
            // response_json["ipv6"] = mgr->ipv6_addresses;
            res.set_content(response_json.dump(), "application/json");
            return;
        }

        // 在 TeamMember 中查找匹配的 Member
        auto it = mgr->multi_map.TeamMember.find(id);
        if (it == mgr->multi_map.TeamMember.end()) {
            throw std::invalid_argument("ID not found in TeamMember");
        }
        // 获取匹配的 Member 信息
        const auto& member = it->second;
        nlohmann::json response_json;
        response_json["ipv4"] = member.ipv4;
        res.set_content(response_json.dump(), "application/json");
        return;
    });

    // GET 返回广播中已知的master信息
    server.Get("/api/team/masters", [mgr](const httplib::Request& req, httplib::Response& res) {
        LOG(INFO) << "On handling GET /api/team/masters";
        nlohmann::json response_json = nlohmann::json::array();
        // 遍历 TeamMaster
        for (const auto& [id, master] : mgr->multi_map.TeamMaster) {
            nlohmann::json master_json = master;
            master_json["id"] = id;
            response_json.push_back(std::move(master_json));
        }
        res.set_content(response_json.dump(), "application/json");
        return;
    });

    // GET 获取队伍信息列表
    server.Get("/api/team", [mgr](const httplib::Request& req, httplib::Response& res) {
        LOG(INFO) << "On handling GET /api/team";
        nlohmann::json response_json = nlohmann::json::array();
        // 遍历 team_tokens
        for (const auto& token : mgr->team_tokens) {
            response_json.push_back(
                {{"teamName", token.teamName}, {"teamID", uuids::to_string(token.teamID)}}
            );
        }
        res.set_content(response_json.dump(), "application/json");
        return;
    });

    // POST 加入队伍
    server.Post("/api/team/join", [mgr](const Request& req, Response& res) {
        LOG(INFO) << "On handling POST /api/team/join";
        nlohmann::json request_json;
        try {
            request_json = nlohmann::json::parse(req.body);
        }
        catch (const nlohmann::json::parse_error& e) {
            throw std::invalid_argument("Invalid JSON format");
        }
        if (!request_json.contains("teamID") || !request_json["teamID"].is_string()
            || !request_json.contains("jwt") || !request_json["jwt"].is_string()) {
            throw std::invalid_argument("Missing required fields: teamID or jwt");
        }
        // 检验 teamID 是否合法
        auto teamID_opt = uuids::uuid::from_string(request_json["teamID"].get<std::string>());
        if (!teamID_opt.has_value()) { throw std::invalid_argument("Invalid teamID format"); }
        // 尝试加入队伍
        auto result = mgr->join_team(teamID_opt.value(), request_json["jwt"].get<std::string>());
        if (!result) { throw std::runtime_error(result.error()); }
        res.status = 200;
        if (result.value()) { res.set_content("Successfully joined team", "text/plain"); }
        else { res.set_content("Already in team", "text/plain"); }
    });

    // POST 退出队伍
    server.Post("/api/team/leave", [mgr](const Request& req, Response& res) {
        LOG(INFO) << "On handling POST /api/team/leave";
        nlohmann::json request_json;
        try {
            request_json = nlohmann::json::parse(req.body);
        }
        catch (const nlohmann::json::parse_error& e) {
            throw std::invalid_argument("Invalid JSON format");
        }
        // 检查是否包含 teamID
        if (!request_json.contains("teamID") || !request_json["teamID"].is_string()) {
            throw std::invalid_argument("Missing or invalid teamID");
        }
        // 检验 teamID 是否合法
        auto teamID_opt = uuids::uuid::from_string(request_json["teamID"].get<std::string>());
        if (!teamID_opt.has_value()) { throw std::invalid_argument("Invalid teamID format"); }
        // 遍历队伍信息列表
        auto& team_tokens = mgr->team_tokens;
        auto it = std::remove_if(
            team_tokens.begin(), team_tokens.end(),
            [&teamID_opt](const TeamToken& token) { return token.teamID == teamID_opt.value(); }
        );
        // 未匹配到token
        if (it == team_tokens.end()) { throw std::invalid_argument("Team token not found"); }
        team_tokens.erase(it, team_tokens.end());
        mgr->save_team_info_to_db();
        res.status = 200;
        res.set_content("Successfully left team", "text/plain");
        return;
    });

    // GET 返回队伍成员的信息
    server.Get("/api/team/peers", [mgr](const Request& req, Response& res) {
        LOG(INFO) << "On handling /api/team/peers";
        nlohmann::json response_json = nlohmann::json::array();
        // 遍历 TeamMember
        for (const auto& [id, member] : mgr->multi_map.TeamMember) {
            nlohmann::json member_json;
            member_json["name"] = member.name;
            member_json["port"] = member.port;
            member_json["expiry"] = member.expiry; // -1 表示 jwt token 永不过期
            member_json["ipv4"] = member.ipv4;
            member_json["ipv6"] = member.ipv6;
            member_json["id"] = id;
            member_json["last_updated"] = member.last_updated;
            // 检查 master_team 是否有效
            if (!member.master_team.teamName.empty() && !member.master_team.teamID.is_nil()) {
                member_json["master_team"]
                    = {{"teamName", member.master_team.teamName},
                       {"teamID", uuids::to_string(member.master_team.teamID)}};
            }
            response_json.push_back(std::move(member_json));
        }
        res.status = 200;
        res.set_content(response_json.dump(), "application/json");
        return;
    });

    // POST 接受队伍成员发送的信息
    server.Post("/api/team-heartbeat", [mgr](const Request& req, Response& res) {
        LOG(INFO) << "On handling POST /api/team-heartbeat";
        nlohmann::json request_json;
        try {
            request_json = nlohmann::json::parse(req.body);
        }
        catch (const nlohmann::json::parse_error& e) {
            throw std::invalid_argument("Invalid JSON format");
        }
        // id 字段是否存在且合法
        if (!request_json.contains("id") || !request_json["id"].is_string()) {
            throw std::invalid_argument("Missing or invalid id field");
        }
        // team 字段是否存在且合法
        if (!request_json.contains("team") || !request_json["team"].is_array()) {
            LOG(WARNING) << "No team data in the received data";
            throw std::invalid_argument("No team data in the received data");
        }
        // teamID 字段是否合法
        const auto& team = request_json["team"][0];
        if (team["teamID"] != uuids::to_string(mgr->master_token.teamID)) {
            LOG(WARNING) << "Team ID mismatch";
            throw std::invalid_argument("Team ID mismatch");
        }
        // jwt 字段是否合法
        std::string jwt = team["jwt"];
        bool is_valid = is_valid_jwt_token(jwt, mgr->master_token.secret);
        bool is_expired = is_expired_jwt_token(jwt, mgr->master_token.secret);
        if (!is_valid || is_expired) {
            LOG(WARNING) << "Invalid or expired JWT";
            throw std::invalid_argument("Invalid or expired JWT");
        }

        std::string id_opt = request_json["id"].get<std::string>();
        LOG(INFO) << "Received team member heartbeat with the correct jwt token from: " << id_opt;
        // 检查是否已存在该框架信息
        auto it = mgr->multi_map.TeamMember.find(id_opt);
        if (it == mgr->multi_map.TeamMember.end()) {
            MultiMap::Member member_info;
            mgr->create_member_info(member_info, request_json);
            mgr->multi_map.TeamMember[id_opt] = member_info;
            LOG(INFO) << "Saved new member data for : " << id_opt;
        }
        else {
            MultiMap::Member& existing_member = it->second;
            mgr->update_member_info(existing_member, request_json);
        }
        res.status = 200;
        res.set_content("OK", "text/plain");
        return;
    });

    server.Post("/api/team/election-msg", [mgr](const Request& req, Response& res) {
        if (!mgr->election_mgr) { throw std::runtime_error("election mgr is not initialized"); }
        using electionmgr::ElectionMsg;
        auto j = nlohmann::json::parse(req.body);
        ElectionMsg msg = j.get<ElectionMsg>();

        msg.sourceAddr = to_multiaddr(req.remote_addr);
        auto msg_res = mgr->election_mgr->on_election_message(msg);
        auto visit_res = visit(ElectionMsgResVisitor{}, std::move(msg_res));
        if (!visit_res) {
            // 返回空字符串, 但表示OK
            return;
        }
        nlohmann::json payload = *visit_res;
        res.set_content(payload.dump(), "application/json");
    });

    // POST 查询能力信息
    server.Post("/api/findAbility", [mgr](const Request& req, Response& res) {
        LOG(INFO) << "On handling POST /api/findAbility";
        nlohmann::json request_json;
        try {
            request_json = nlohmann::json::parse(req.body);
        }
        catch (const nlohmann::json::parse_error& e) {
            res.status = 400;
            res.set_content("Invalid JSON", "text/plain");
            return;
        }
        // todo
    });
}

// 构造函数，读取配置文件，获取设备信息等
DiscoveryManager::DiscoveryManager(std::shared_ptr<uvw::loop> loop)
    : loop_(std::move(loop)) {

    LOG(INFO) << "DiscoveryManager initialized";

    // 获取框架名称，ID和方法等
    name = global_vars::get_config<std::string>("/framework_name");
    framework_id = global_vars::framework_id();
    method.ipv4 = global_vars::get_config<bool>("/discovery_mgr/methods/ipv4", true);
    method.ipv6 = global_vars::get_config<bool>("/discovery_mgr/methods/ipv6", false);
    int expiry_opt = global_vars::get_config<int>("/discovery_mgr/expiry", 30);
    std::filesystem::path config_path = global_vars::config_path();

    // 获取MAC地址，暂时没有用到
    /* std::string interface = get_default_interface();
    std::string mac_address = get_mac_address(interface);
    if (mac_address.empty()) {
        LOG(ERROR) << "Failed to get MAC address";
    }
    LOG(INFO) << "Get MAC Address: " << mac_address; */

    // 获取 IPv4 地址
    ipv4_addresses = get_ipv4_addresses();
    LOG(INFO) << "Get IPv4 Addresses: ";
    for (const auto& addr : ipv4_addresses) {
        LOG(INFO) << addr;
    }

    // 获取 IPv6 地址
    ipv6_addresses = get_ipv6_addresses();
    LOG(INFO) << "Get IPv6 Addresses: ";
    for (const auto& addr : ipv6_addresses) {
        LOG(INFO) << addr;
    }

    // 加载队伍信息
    const bool redis_enabled = false;
    DiscoveryManager::load_team_config(redis_enabled, config_path);

    DiscoveryManager::start_receiving();
    DiscoveryManager::start_sending();

    // 定时同步队伍信息到 Redis
    // TODO: redis -> sqlite
    // if (redis_enabled) { DiscoveryManager::start_sync_to_redis(10); }

    // 即使节点不是队长,它也尝试配置mdns信息
    // 定时清理超时的成员，参数：超时时长，清理间隔
    DiscoveryManager::start_cleanup_inactive_member(expiry_opt, 10);
    // 启动 mDNS 服务
    mdns.setServiceName("_AbilityFramework._tcp.local.");
    mdns.setServiceHostname(master_token.teamName);
    mdns.setServicePort(global_vars::get_config<int>("/http_port", 8080));

    init_election_mgr();
    if (election_mgr) { start_update_mdns_state(); }
    else { LOG(WARNING) << "election mgr is not valid, mdns will not start"; }
}

void DiscoveryManager::init_election_mgr() {
    using namespace electionmgr;
    auto config = global_vars::get_config<ElectionConfig>("/discovery_mgr/election", {});
    if (config.method.empty()) {
        LOG(WARNING) << "election method is empty, election will not start";
        return;
    }

    LOG(INFO) << "initing election mgr, method= " << config.method;
    election_mgr = ElectionManager::from_config(
        config.method, framework_id, config.params,
        std::make_unique<DiscoveryMgrElectionInterface>(this)
    );
    if (!election_mgr) {
        LOG(ERROR) << "initializing election mgr failed, elect will not start";
        return;
    }
    election_mgr->start();
}

// 从 config.yaml 加载队伍信息
bool DiscoveryManager::load_team_config_from_file(std::filesystem::path config_path) {
    try {
        auto yaml = YAML::LoadFile(config_path.string());
        if (yaml["discovery_mgr"] && yaml["discovery_mgr"]["teams"]) {
            const auto& teams = yaml["discovery_mgr"]["teams"];

            for (const auto& team : teams) {
                // 检查是否包含 teamName, teamID 和 secret
                bool is_valid = team["teamName"] && team["teamID"] && team["secret"];
                if (!is_valid) {
                    LOG(WARNING) << "Incomplete team configuration found in discovery_mgr/teams";
                    continue;
                }
                // 解析队伍信息
                try {
                    TeamToken token;
                    token.teamName = team["teamName"].as<std::string>();
                    auto teamID_opt = uuids::uuid::from_string(team["teamID"].as<std::string>());
                    if (teamID_opt) { token.teamID = teamID_opt.value(); }
                    else {
                        LOG(ERROR) << "Invalid teamID format: " << team["teamID"].as<std::string>();
                        continue;
                    }
                    token.secret = team["secret"].as<std::string>();
                    if (team["master"] && team["master"].as<bool>()) {
                        master = true;
                        master_token = token;
                        LOG(INFO) << "This is a master framework for team: " << token.teamName;
                    }
                    else {
                        token.jwt
                            = generate_jwt_token(token.secret, uuids::to_string(framework_id));
                        LOG(INFO) << "Generated Token for team: " << token.teamName << "\n"
                                  << token.jwt;
                        team_tokens.push_back(token);
                    }
                }
                catch (const YAML::TypedBadConversion<std::string>& e) {
                    LOG(ERROR) << "Failed to parse team entry: " << e.what();
                }
            }
            return true;
        }
        else {
            LOG(WARNING) << "Incomplete discovery_mgr configuration";
            return false;
        }
    }
    catch (const YAML::Exception& e) {
        LOG(ERROR) << "Failed to load config file: " << e.what();
        return false;
    }
}

// 从 Redis 加载数据
bool DiscoveryManager::load_team_config_from_db() {
    UnImplementedWarning(__func__);
    return false;
}

// 加载队伍信息
void DiscoveryManager::load_team_config(
    bool redis_enabled, const std::filesystem::path& config_path
) {
    if (load_team_config_from_file(config_path)) {
        LOG(INFO) << "Loaded team config from config.yaml";
        return;
    }
    // 如果都失败，记录警告
    LOG(WARNING) << "Team config not found";
}

// 接收数据
void DiscoveryManager::start_receiving() {

    // 从配置文件读取 IPv4/IPv6 UDP 监听地址和端口
    const std::string ipv4_host = global_vars::get_config<std::string>(
        "/discovery_mgr/udp/ipv4/host", "0.0.0.0"
    );
    const int ipv4_port = global_vars::get_config<int>("/discovery_mgr/udp/ipv4/port", 12345);
    const std::string ipv6_host
        = global_vars::get_config<std::string>("/discovery_mgr/udp/ipv6/host", "::");
    const int ipv6_port = global_vars::get_config<int>("/discovery_mgr/udp/ipv6/port", 12346);

    // 创建用于接收 IPv4 数据包的 UDP 句柄
    auto udp = loop_->resource<uvw::udp_handle>();

    udp->on<uvw::error_event>([](const uvw::error_event& event, uvw::udp_handle&) {
        LOG(ERROR) << "IPV4_receiving Error: " << event.what();
    });

    udp->on<uvw::udp_data_event>([this](const uvw::udp_data_event& event, uvw::udp_handle&) {
        std::string packet(event.data.get(), event.length);
        // 解析 IPv4 广播包
        parse_ipv4_broadcast(packet);
    });

    udp->bind(ipv4_host, ipv4_port);
    udp->recv();

    // 创建用于接收 IPv6 数据包的 UDP 句柄
    auto udp_v6 = loop_->resource<uvw::udp_handle>();

    udp_v6->on<uvw::error_event>([](const uvw::error_event& event, uvw::udp_handle&) {
        LOG(ERROR) << "IPV6_receiving Error: " << event.what();
    });

    udp_v6->on<uvw::udp_data_event>([this](const uvw::udp_data_event& event, uvw::udp_handle&) {
        std::string packet(event.data.get(), event.length);
        // 解析 IPv6 广播包
        parse_ipv6_broadcast(packet);
    });

    udp_v6->bind(ipv6_host, ipv6_port);
    udp_v6->recv();
}

bool try_send_team_heartbeat(const TeamToken& token, const nlohmann::json& data) try {
    static const std::string endpoint = "/api/team-heartbeat";
    for (const auto& addr : token.masterIP) {
        httplib::Client client(addr, token.masterPort);
        auto res = client.Post(endpoint, data.dump(), "application/json");
        if (res && res->status == 200) {
            LOG(INFO) << "Sent team heartbeat to " << addr << ":" << token.masterPort;
            return true;
        }
        LOG(ERROR) << "Failed to send team data to " << addr << ":" << token.masterPort
                   << ". Status: " << (res ? res->status : -1);
    }
    return false;
}
catch (const std::exception& e) {
    LOG(ERROR) << "Exception while sending team data: " << e.what();
    return false;
}

// 通过http发送队伍数据
void DiscoveryManager::send_team_data() {
    nlohmann::json data;
    if (team_tokens.empty()) {
        LOG_FIRST_N(INFO, 30) << "team token is empty, won't send team data";
        return;
    }
    for (const auto& token : team_tokens) {
        data
            = {{"name", name},
               {"id", uuids::to_string(framework_id)},
               {"ipv4", ipv4_addresses},
               {"ipv6", ipv6_addresses},
               {"port", global_vars::get_config<int>("/http_port", 8080)}};
        data["team"].push_back(
            {{"teamName", token.teamName},
             {"teamID", uuids::to_string(token.teamID)},
             {"jwt", token.jwt}}
        );
        // 如果是队长框架，添加 master 信息
        if (master) {
            data["master"].push_back(
                {{"teamName", master_token.teamName},
                 {"teamID", uuids::to_string(master_token.teamID)}}
            );
        }
        try_send_team_heartbeat(token, data);
    }
}

// 启动定时发送
void DiscoveryManager::start_sending() {

    // 定时发送广播包，只有队长框架需要广播，队员框架通过http发送队伍数据
    auto timer = loop_->resource<uvw::timer_handle>();
    timer->on<uvw::timer_event>([this](const uvw::timer_event&, uvw::timer_handle& handle) {
        if (method.ipv4) {
            for (const auto& token : team_tokens) {
                // 所有框架都会发的广播包
                data = {
                    {"name", name},
                    {"id", uuids::to_string(framework_id)},
                    {"ipv4", ipv4_addresses},
                    {"ipv6", ipv6_addresses},
                    {"port", global_vars::get_config<int>("/http_port", 8080)},
                    {"teamName", token.teamName},
                    {"teamID", uuids::to_string(token.teamID)},
                };
                send_ipv4_broadcast(data);
                // 延迟2秒
                std::this_thread::sleep_for(std::chrono::seconds(2));
            };

            if (master) {
                // 队长广播包
                data
                    = {{"name", name},
                       {"id", uuids::to_string(framework_id)},
                       {"ipv4", ipv4_addresses},
                       {"ipv6", ipv6_addresses},
                       {"port", global_vars::get_config<int>("/http_port", 8080)}};
                data["master"] = nlohmann::json::array();
                data["master"].push_back(
                    {{"teamName", master_token.teamName},
                     {"teamID", uuids::to_string(master_token.teamID)}}
                );
                send_ipv4_broadcast(data);
                // 延迟2秒
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
            // http发送队伍心跳包
            send_team_data();
        }
        else { LOG(INFO) << "IPv4 broadcast is disabled."; }

        // ipv6 广播  暂时用不到
        if (method.ipv6) {
            for (const auto& token : team_tokens) {
                // 所有人都会发送广播包
                data = {
                    {"name", name},
                    {"id", uuids::to_string(framework_id)},
                    {"ipv4", ipv4_addresses},
                    {"ipv6", ipv6_addresses},
                    {"port", global_vars::get_config<int>("/http_port", 8080)},
                    {"teamName", token.teamName},
                    {"teamID", uuids::to_string(token.teamID)},
                };
                send_ipv6_broadcast(data);
                // 延迟2秒
                std::this_thread::sleep_for(std::chrono::seconds(2));
            };
            if (master) {
                // 队长广播包
                data
                    = {{"name", name},
                       {"id", uuids::to_string(framework_id)},
                       {"ipv4", ipv4_addresses},
                       {"ipv6", ipv6_addresses},
                       {"port", global_vars::get_config<int>("/http_port", 8080)}};
                data["master"] = nlohmann::json::array();
                data["master"].push_back(
                    {{"teamName", master_token.teamName},
                     {"teamID", uuids::to_string(master_token.teamID)}}
                );
                send_ipv4_broadcast(data);
                // 延迟2秒
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
            send_team_data();
        }
        else {
            // LOG(INFO) << "IPv6 broadcast is disabled.";
        }
    });
    // 10秒一次
    timer->start(uvw::timer_handle::time{0}, uvw::timer_handle::time{10000});
}

namespace {
void guard_mdns_is_on(mdns_cpp::mDNS& m) {
    if (m.isServiceRunning()) { return; }
    LOG(WARNING) << "discoverymgr starting mdns";
    m.startService();
}

void guard_mdns_is_off(mdns_cpp::mDNS& m) {
    if (!m.isServiceRunning()) { return; }
    LOG(WARNING) << "discoverymgr stopping mdns";
    m.stopService();
}
} // namespace

void DiscoveryManager::start_update_mdns_state() {
    using namespace std::chrono_literals;
    if (!election_mgr) {
        LOG(ERROR) << "election mgr is null, mdns will not run";
        return;
    }
    auto timer = loop_->resource<uvw::timer_handle>();
    timer->on<uvw::timer_event>([this](const uvw::timer_event&, uvw::timer_handle& handle) {
        if (!election_mgr) { return; }
        bool is_leader = self_is_leader(*election_mgr);
        if (is_leader) { guard_mdns_is_on(mdns); }
        else { guard_mdns_is_off(mdns); }
    });
    // 10秒一次
    timer->start(uvw::timer_handle::time{0}, 10s);
}

// 发送 IPv4 广播包
void DiscoveryManager::send_ipv4_broadcast(const nlohmann::json& data) {
    auto udp = loop_->resource<uvw::udp_handle>();
    udp->on<uvw::error_event>([](const uvw::error_event& event, uvw::udp_handle& handle) {
        LOG(ERROR) << "Send_ipv4_broadcast error: " << event.what();
        handle.close();
    });
    udp->on<uvw::send_event>([](const uvw::send_event&, uvw::udp_handle& handle) { handle.close(); }
    );
    udp->bind("0.0.0.0", 0);
    udp->broadcast(true);
    std::string message = data.dump();
    const int ipv4_port = global_vars::get_config<int>("/discovery_mgr/udp/ipv4/port", 12345);
    udp->send("255.255.255.255", ipv4_port, message.data(), message.size());
    LOG(INFO) << "send ipv4 broadcast msg complete";
}

// 解析 IPv4 广播包
void DiscoveryManager::parse_ipv4_broadcast(const std::string& packet) {
    try {
        nlohmann::json data = nlohmann::json::parse(packet);
        // 调试时候可取消注释，方便观察接受到的广播包
        // LOG(INFO) << "Received IPv4 broadcast: " << data.dump();

        std::string id_opt;
        if (!data.contains("id")) {
            LOG(ERROR) << "invalid broadcast data\n" << data.dump(2);
            return;
        }

        id_opt = data["id"];
        // 抛弃自身发出的广播包,如果调试可以先注释掉
        if (id_opt == uuids::to_string(framework_id)) { return; }

        VLOG(1) << "Received IPv4 broadcast from: " << id_opt;
        // 如果接收到的广播包中包含 master 字段，队长广播
        if (data.contains("master") && data["master"].is_array()) {
            const auto& master = data["master"][0];
            if (master.contains("teamID") && master.contains("teamName")) {
                std::string team_id = master["teamID"];
                std::string team_name = master["teamName"];
                LOG(INFO) << "Received team master: " << team_name;
                // 检查 teamID 是否合法
                auto teamID_opt = uuids::uuid::from_string(team_id);
                if (!teamID_opt) {
                    LOG(ERROR) << "Invalid teamID format: " << team_id;
                    return;
                }
                std::string id = data["id"];
                // 检查是否已存在该框架信息
                auto it = multi_map.TeamMaster.find(id);
                if (it == multi_map.TeamMaster.end()) {
                    MultiMap::Master master_info;
                    DiscoveryManager::create_master_info(master_info, data);
                    multi_map.TeamMaster[id] = master_info;
                    LOG(INFO) << "Saved master data for ID: " << id;
                }
                else {
                    MultiMap::Master& existing_master = it->second;
                    DiscoveryManager::update_master_info(existing_master, data);
                    LOG(INFO) << "Updated master data for ID: " << id;
                }
            }
        }
        else {
            // 如果没有 master 信息，则认为是 peer
            if (!data.contains("teamID") || !data["teamID"].is_string()) {
                VLOG(1) << "Received peer data without valid teamID";
                return;
            }
            std::string team_id = data["teamID"];
            auto teamID_opt = uuids::uuid::from_string(team_id);
            if (!teamID_opt) {
                LOG(ERROR) << "Invalid teamID format: " << team_id;
                return;
            }

            if (!data.contains("id") || !data["id"].is_string()) {
                LOG(ERROR) << "Received peer data without valid ID";
                return;
            }

            // 遍历 team_tokens 中的 teamID
            bool found = false;
            for (const auto& token : team_tokens) {
                if (token.teamID == teamID_opt) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                LOG(WARNING) << "Received peer data with unknown teamID: " << team_id;
                return;
            }

            std::string id = data["id"].get<std::string>();
            LOG(INFO) << "Received peer data from: " << id;
            // 检查是否已存在该框架信息
            auto it = multi_map.Peers.find(id);
            if (it == multi_map.Peers.end()) {
                MultiMap::Peer peer_info;
                DiscoveryManager::create_peer_info(peer_info, data);
                multi_map.Peers[id] = peer_info;
                LOG(INFO) << "Saved peer data for ID: " << id;
            }
            else {
                MultiMap::Peer& existing_peer = it->second;
                DiscoveryManager::update_peer_info(existing_peer, data);
                LOG(INFO) << "Updated peer data for ID: " << id;
            }
            return;
        }
    }
    catch (const nlohmann::json::parse_error& e) {
        LOG(ERROR) << "Failed to parse broadcast packet: ";
    }
}

// 发送 IPv6 广播包  存在问题  error: network is unreachable
void DiscoveryManager::send_ipv6_broadcast(const nlohmann::json& data) {
    auto udp = loop_->resource<uvw::udp_handle>();
    udp->on<uvw::error_event>([](const uvw::error_event& event, uvw::udp_handle& handle) {
        LOG(ERROR) << "Send_ipv6_broadcast error: " << event.what();
        handle.close();
    });
    udp->on<uvw::send_event>([](const uvw::send_event&, uvw::udp_handle& handle) { handle.close(); }
    );
    udp->bind("::", 0);
    udp->broadcast(true);
    std::string message = data.dump();
    const int ipv6_port = global_vars::get_config<int>("/discovery_mgr/udp/ipv6/port", 12346);
    udp->send("FF02::1", ipv6_port, message.data(), message.size());
}

// 解析 IPv6 广播包
void DiscoveryManager::parse_ipv6_broadcast(const std::string& packet) {
    try {
        nlohmann::json data = nlohmann::json::parse(packet);
        LOG(INFO) << "Received IPv6 broadcast: " << data.dump();

        // 处理接收到的数据
        // todo
    }
    catch (const nlohmann::json::parse_error& e) {
        LOG(ERROR) << "Failed to parse broadcast packet: " << e.what();
    }
}

// 移除超时队员信息，参数 超时时间，执行间隔时间
void DiscoveryManager::start_cleanup_inactive_member(int timeout_seconds, int interval_seconds) {
    auto timer = loop_->resource<uvw::timer_handle>();
    LOG(INFO) << "Running cleanup for inactive members...";
    timer->on<uvw::timer_event>(
        [this, timeout_seconds](const uvw::timer_event&, uvw::timer_handle& handle) {
            auto now = std::time(nullptr); // 获取当前时间
            for (auto it = multi_map.TeamMember.begin(); it != multi_map.TeamMember.end();) {
                if (now - it->second.last_updated > timeout_seconds) {
                    LOG(WARNING) << "Removing inactive member: " << it->first;
                    it = multi_map.TeamMember.erase(it); // 移除超时成员
                }
                else { ++it; }
            }
        }
    );
    timer->start(uvw::timer_handle::time{0}, uvw::timer_handle::time{interval_seconds * 1000});
}

// 保存数据到 Redis
void DiscoveryManager::save_team_info_to_db() {
    UnImplementedWarning(__func__);
}

// 定时同步到 Redis
void DiscoveryManager::start_sync_team_info_to_db(int interval_seconds) {
    UnImplementedWarning(__func__);
    auto timer = loop_->resource<uvw::timer_handle>();
    timer->on<uvw::timer_event>([this](const uvw::timer_event&, uvw::timer_handle& handle) {
        // TODO: sync to db
    });
    timer->start(uvw::timer_handle::time{0}, uvw::timer_handle::time{interval_seconds * 1000});
    LOG(INFO) << "Started periodic sync to db every " << interval_seconds << " seconds.";
}

// 创建队长信息
void DiscoveryManager::create_master_info(
    MultiMap::Master& master_info, const nlohmann::json& data
) {
    std::string team_id = data["master"][0]["teamID"];
    std::string team_name = data["master"][0]["teamName"];
    auto teamID_opt = uuids::uuid::from_string(team_id);
    master_info.teamName = team_name;
    master_info.teamID = teamID_opt.value();
    // 添加框架名
    if (data.contains("name") && data["name"].is_string()) {
        master_info.name = data["name"].get<std::string>();
    }
    else { LOG(WARNING) << "Master name is missing or not a string"; }
    // 添加端口
    if (data.contains("port") && data["port"].is_number_integer()) {
        master_info.port = data["port"].get<int>();
    }
    else { LOG(WARNING) << "Master port is missing or not an integer"; }
    // 添加 IPv4 地址
    if (data.contains("ipv4") && data["ipv4"].is_array()) {
        for (const auto& ipv4 : data["ipv4"]) {
            if (ipv4.is_string()) { master_info.ipv4.push_back(ipv4.get<std::string>()); }
        }
        // 添加 IPv4 地址到对应的 team_tokens 中
        if (!team_tokens.empty()) {
            for (auto& token : team_tokens) {
                if (token.teamID == teamID_opt.value()) {
                    token.masterIP = master_info.ipv4;
                    token.masterPort = master_info.port;
                    // LOG(INFO) << "Added IPv4 address to team token for teamID: " <<
                    // teamID_opt.value();
                }
            }
        }
    }
    // 添加 IPv6 地址
    if (data.contains("ipv6") && data["ipv6"].is_array()) {
        for (const auto& ipv6 : data["ipv6"]) {
            if (ipv6.is_string()) { master_info.ipv6.push_back(ipv6.get<std::string>()); }
        }
    }
}

// 更新队长信息
void DiscoveryManager::update_master_info(
    MultiMap::Master& existing_master, const nlohmann::json& data
) {
    std::string team_id = data["master"][0]["teamID"];
    std::string team_name = data["master"][0]["teamName"];
    auto teamID_opt = uuids::uuid::from_string(team_id);
    // 更新名称
    if (existing_master.name != data["name"].get<std::string>()) {
        existing_master.name = data["name"].get<std::string>();
    }
    // 更新端口号
    if (existing_master.port != data["port"].get<int>()) {
        existing_master.port = data["port"].get<int>();
    }
    // 更新队伍名称
    if (existing_master.teamName != team_name) { existing_master.teamName = team_name; }
    // 更新队伍 ID
    if (existing_master.teamID != teamID_opt.value()) {
        existing_master.teamID = teamID_opt.value();
    }
    // 更新 IPv4 地址
    if (data.contains("ipv4") && data["ipv4"].is_array()) {
        std::vector<std::string> new_ipv4;
        for (const auto& ipv4 : data["ipv4"]) {
            if (ipv4.is_string()) { new_ipv4.push_back(ipv4.get<std::string>()); }
        }
        if (existing_master.ipv4 != new_ipv4) {
            existing_master.ipv4 = new_ipv4;
            if (!team_tokens.empty()) {
                for (auto& token : team_tokens) {
                    if (token.teamID == teamID_opt.value()) {
                        token.masterIP = new_ipv4;
                        token.masterPort = data["port"].get<int>();
                        ;
                    }
                }
            }
        }
    }
    // 更新 IPv6 地址
    if (data.contains("ipv6") && data["ipv6"].is_array()) {
        std::vector<std::string> new_ipv6;
        for (const auto& ipv6 : data["ipv6"]) {
            if (ipv6.is_string()) { new_ipv6.push_back(ipv6.get<std::string>()); }
        }
        if (existing_master.ipv6 != new_ipv6) { existing_master.ipv6 = new_ipv6; }
    }
}

// 加入队伍
expected<bool, std::string> DiscoveryManager::join_team(
    uuids::uuid teamID, const std::string& jwt
) {

    for (const auto& token : team_tokens) {
        if (token.teamID == teamID) {
            return true; // 已加入队伍，返回成功状态
        }
    }

    TeamToken token_opt;
    // 遍历 TeamMaster 查找匹配的队长信息
    for (const auto& master : multi_map.TeamMaster) {
        if (master.second.teamID == teamID) {
            token_opt.teamName = master.second.teamName;
            token_opt.teamID = teamID;
            token_opt.masterIP = master.second.ipv4;
            token_opt.masterPort = master.second.port;
            token_opt.jwt = jwt;
            team_tokens.push_back(token_opt);
            save_team_info_to_db();
            return true; // 成功加入，返回成功状态
        }
    }

    return ::semantic_expected::unexpected{"Team Master not found"}; // 未找到匹配的队长信息，返回错误信息
}

// 创建队伍成员信息
void DiscoveryManager::create_member_info(
    MultiMap::Member& member_info, const nlohmann::json& data
) {
    if (data.contains("name") && data["name"].is_string()) {
        member_info.name = data["name"].get<std::string>();
    }
    if (data.contains("port") && data["port"].is_number_integer()) {
        member_info.port = data["port"].get<int>();
    }
    if (data.contains("ipv4") && data["ipv4"].is_array()) {
        for (const auto& ipv4 : data["ipv4"]) {
            if (ipv4.is_string()) { member_info.ipv4.push_back(ipv4.get<std::string>()); }
        }
    }
    if (data.contains("ipv6") && data["ipv6"].is_array()) {
        for (const auto& ipv6 : data["ipv6"]) {
            if (ipv6.is_string()) { member_info.ipv6.push_back(ipv6.get<std::string>()); }
        }
    }
    if (data.contains("master") && data["master"].is_array()) {
        for (const auto& master : data["master"]) {
            if (master.contains("teamID") && master.contains("teamName")) {
                std::string team_id = master["teamID"];
                std::string team_name = master["teamName"];
                member_info.master_team.teamName = team_name;
                member_info.master_team.teamID = uuids::uuid::from_string(team_id).value();
            }
        }
    }
    std::string jwt = data["team"][0]["jwt"];
    member_info.jwt = jwt;
    member_info.expiry = check_token_expiration(jwt);
    member_info.last_updated = std::time(nullptr);
    // LOG(INFO) << "Created member info for: " << member_info.name ;
}

// 更新队伍成员信息
void DiscoveryManager::update_member_info(
    MultiMap::Member& existing_member, const nlohmann::json& data
) {
    if (data.contains("name") && data["name"].is_string()
        && existing_member.name != data["name"].get<std::string>()) {
        existing_member.name = data["name"].get<std::string>();
    }
    if (data.contains("port") && data["port"].is_number_integer()
        && existing_member.port != data["port"].get<int>()) {
        existing_member.port = data["port"].get<int>();
    }
    if (data.contains("ipv4") && data["ipv4"].is_array()) {
        std::vector<std::string> new_ipv4;
        for (const auto& ipv4 : data["ipv4"]) {
            if (ipv4.is_string()) { new_ipv4.push_back(ipv4.get<std::string>()); }
        }
        if (existing_member.ipv4 != new_ipv4) { existing_member.ipv4 = std::move(new_ipv4); }
    }
    if (data.contains("ipv6") && data["ipv6"].is_array()) {
        std::vector<std::string> new_ipv6;
        for (const auto& ipv6 : data["ipv6"]) {
            if (ipv6.is_string()) { new_ipv6.push_back(ipv6.get<std::string>()); }
        }
        if (existing_member.ipv6 != new_ipv6) { existing_member.ipv6 = std::move(new_ipv6); }
    }
    std::string jwt = data["team"][0]["jwt"];
    existing_member.jwt = jwt;
    existing_member.expiry = check_token_expiration(jwt);
    existing_member.last_updated = std::time(nullptr);
    // LOG(INFO) <<  "Updated member info for: " << existing_member.name;
}

// 创建Peer信息
void DiscoveryManager::create_peer_info(MultiMap::Peer& peer_info, const nlohmann::json& data) {
    if (data.contains("name") && data["name"].is_string()) {
        peer_info.name = data["name"].get<std::string>();
    }
    if (data.contains("port") && data["port"].is_number_integer()) {
        peer_info.port = data["port"].get<int>();
    }
    if (data.contains("ipv4") && data["ipv4"].is_array()) {
        for (const auto& ipv4 : data["ipv4"]) {
            if (ipv4.is_string()) { peer_info.ipv4.push_back(ipv4.get<std::string>()); }
        }
    }
    if (data.contains("ipv6") && data["ipv6"].is_array()) {
        for (const auto& ipv6 : data["ipv6"]) {
            if (ipv6.is_string()) { peer_info.ipv6.push_back(ipv6.get<std::string>()); }
        }
    }
    if (data.contains("teamName") && data["teamName"].is_string()) {
        peer_info.teamName = data["teamName"].get<std::string>();
    }
    if (data.contains("teamID") && data["teamID"].is_string()) {
        peer_info.teamID = uuids::uuid::from_string(data["teamID"].get<std::string>()).value();
    }
}

// 更新Peer信息
void DiscoveryManager::update_peer_info(MultiMap::Peer& existing_peer, const nlohmann::json& data) {
    if (data.contains("name") && data["name"].is_string()
        && existing_peer.name != data["name"].get<std::string>()) {
        existing_peer.name = data["name"].get<std::string>();
    }
    if (data.contains("port") && data["port"].is_number_integer()
        && existing_peer.port != data["port"].get<int>()) {
        existing_peer.port = data["port"].get<int>();
    }
    if (data.contains("ipv4") && data["ipv4"].is_array()) {
        std::vector<std::string> new_ipv4;
        for (const auto& ipv4 : data["ipv4"]) {
            if (ipv4.is_string()) { new_ipv4.push_back(ipv4.get<std::string>()); }
        }
        if (existing_peer.ipv4 != new_ipv4) { existing_peer.ipv4 = std::move(new_ipv4); }
    }
    if (data.contains("ipv6") && data["ipv6"].is_array()) {
        std::vector<std::string> new_ipv6;
        for (const auto& ipv6 : data["ipv6"]) {
            if (ipv6.is_string()) { new_ipv6.push_back(ipv6.get<std::string>()); }
        }
        if (existing_peer.ipv6 != new_ipv6) { existing_peer.ipv6 = std::move(new_ipv6); }
    }
    if (data.contains("teamName") && data["teamName"].is_string()
        && existing_peer.teamName != data["teamName"].get<std::string>()) {
        existing_peer.teamName = data["teamName"].get<std::string>();
    }
    if (data.contains("teamID") && data["teamID"].is_string()
        && existing_peer.teamID
               != uuids::uuid::from_string(data["teamID"].get<std::string>()).value()) {
        existing_peer.teamID = uuids::uuid::from_string(data["teamID"].get<std::string>()).value();
    }
}
