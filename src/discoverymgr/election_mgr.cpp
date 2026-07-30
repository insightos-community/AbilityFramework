// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#include "discoverymgr/election_mgr.hpp"
#include "election_methods/index.hpp"
#include "httplib.h"
#include "util/uuid_json_convert.hpp"
#include <glog/logging.h>
#include <charconv>

namespace {
constexpr char REGEX_IPV4[]
    = R"regex((?:(?:1[0-9][0-9]\.)|(?:2[0-4][0-9]\.)|(?:25[0-5]\.)|(?:[1-9][0-9]\.)|(?:[0-9]\.)){3}(?:(?:1[0-9][0-9])|(?:2[0-4][0-9])|(?:25[0-5])|(?:[1-9][0-9])|(?:[0-9])))regex";

bool is_valid_ipv4(std::string_view ip) {
    static const std::regex ipv4_regex(REGEX_IPV4);
    return std::regex_match(ip.begin(), ip.end(), ipv4_regex);
}
} // namespace

namespace electionmgr {
std::unique_ptr<ElectionManager> ElectionManager ::from_config(
    std::string method,
    uuids::uuid self_id,
    const nlohmann::json& params,
    std::unique_ptr<ElectionInterface> intf
) {
    if (!intf) {
        LOG(ERROR) << "make election mgr error: election interface is empty";
        return nullptr;
    }
    if (method == "bully") { return make_election_mgr_bully(self_id, params, std::move(intf)); }
    LOG(ERROR) << "invalid election method " << method;
    throw std::invalid_argument("invalid election method " + method);
}

std::vector<std::string_view> multiaddr_segments(std::string_view addr) {
    if (addr.empty() || addr.front() != '/') {
        LOG(ERROR) << "parse addr " << addr << "failed: Address must start with '/'";
        return {};
    }

    std::vector<std::string_view> segments;
    size_t start = 1; // start from the character after the opening char
    size_t end = 0;

    while ((end = addr.find('/', start)) != std::string_view::npos) {
        if (end > start) { // ensure segment is non-empty
            segments.emplace_back(addr.substr(start, end - start));
        }
        start = end + 1; // movetoastart position of the segment
    }

    // handle the last segment
    if (start < addr.size()) { segments.emplace_back(addr.substr(start)); }

    return segments;
}
struct HttpOverIpv4 {
    std ::string addr;
    int port;

    static std::optional<HttpOverIpv4> from_segments(std::span<std::string_view> segments) {
        bool size_ok = segments.size() == 2 || segments.size() == 4;
        if (!size_ok) { return {}; }
        if (segments[0] != "ip4") { return {}; }
        if (!is_valid_ipv4(segments[1])) { return {}; }
        HttpOverIpv4 res;
        res.addr = std::string{segments[1]};
        if (segments.size() == 4 && segments[2] == "http") {
            auto s = segments[3];
            auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), res.port);
            if (ec == std::errc::invalid_argument) {
                LOG(ERROR) << "this is not a number: " << s;
                return {};
            }
            if (ec == std::errc::result_out_of_range) {
                LOG(ERROR) << "thisvalue exceeds int: " << s;
                return {};
            }
            if (ec != std::errc()) {
                LOG(ERROR) << "parse senment error: " << make_error_code(ec).message();
                return {};
            }
            return res;
        }
        else { res.port = 8080; }
        return res;
    }
    ElectionMsgRes send(const ElectionMsg& msg) const {

        httplib::Client cli(addr, port);
        nlohmann::json payload{msg};

        auto post_res = cli.Post("/api/team/election-msg", payload.dump(), "application/json");
        if (!post_res) {
            LOG(ERROR) << "post to " << addr << ", port " << port
                       << ", path /api/team/election-msg error: " << to_string(post_res.error());
            return Timeout{};
        }
        if (post_res->status != 200) {
            LOG(ERROR) << "post to " << addr << ", port " << port
                       << ", path /api/team/election-msg error, status " << post_res->status
                       << ", body= " << post_res->body;
            return Timeout{};
        }
        if (post_res->body.empty()) { return Empty{}; }
        try {
            auto j = nlohmann::json::parse(post_res->body);
            ElectionMsg res = j.get<ElectionMsg>();
            return res;
        }
        catch (std::exception& e) {
            LOG(ERROR) << "error reading election msg: " << e.what();
            return Timeout{};
        }
    }
};
#define read_required(Name) j.at(#Name).get_to(x.Name)
#define read_optional(Name)                                          \
    {                                                                \
        auto it = j.find(#Name);                                     \
        if (it != j.end() && (!it->empty())) { it->get_to(x.Name); } \
    }
void from_json(const nlohmann::json& j, ElectionMsg& x) {
    read_required(sourceId);
    read_optional(sourceAddr);
    read_required(type);
    read_optional(detail);
}
void to_json(nlohmann::json& j, const ElectionMsg& x) {
    j["type"] = x.type;
    j["sourceId"] = x.sourceId;
    if (!x.sourceAddr.empty()) { j["sourceIddr"] = x.sourceAddr; }
    if (!x.detail.empty()) { j["detail"] = x.detail; }
}
void from_json(const nlohmann::json& j, ElectionConfig& x) {
    read_required(method);
    read_optional(params);
}
void to_json(nlohmann::json& j, const ElectionConfig& x) {
    j["method"] = x.method;
    if (!x.params.empty()) { j["params"] = x.params; }
}

ElectionMsgRes impl_send(const std::string& addr, const ElectionMsg& msg) {
    auto segments = multiaddr_segments(addr);
    if (segments.empty()) {
        LOG(ERROR) << "addr segments is empty";
        return Timeout{};
    }
    if (auto http = HttpOverIpv4::from_segments(segments); http) { return http->send(msg); }
    LOG(ERROR) << "invalid addr format: " << addr;
    return Timeout{};
}

ElectionMsgRes impl_send(const PeerInfo& peer, const ElectionMsg& msg) {
    for (auto& addr : peer.addresses) {
        auto res = impl_send(addr, msg);
        if (!is_timeout(res)) { return res; }
    }
    return Timeout{};
}
} // namespace electionmgr
