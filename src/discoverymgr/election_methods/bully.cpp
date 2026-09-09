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

#include "discoverymgr/election_mgr.hpp"
#include <glog/logging.h>
#include <mutex>
#include <regex>
#include <thread>

using namespace electionmgr;
namespace {
std::chrono::milliseconds random_milliseconds(int min, int max) {
    std::random_device rd;
    std::uniform_int_distribution<int> d(min, max);
    return std::chrono::milliseconds{d(rd)};
};
int random_int(int min, int max) {
    std::random_device rd;
    std::uniform_int_distribution<int> d(min, max);
    return d(rd);
};

struct Key {
    uuids::uuid id;
    int weight = 0;
    bool operator<(const Key& other) const {
        if (weight < other.weight) { return true; }
        if (weight == other.weight) { return id < other.id; }
        return false;
    }
    bool operator==(const Key& other) const = default;
    static Key from_msg(const ElectionMsg& msg) {
        return {msg.sourceId, msg.detail.value("weight", 0)};
    };
};
struct ExtPeerInfo {
    PeerInfo pinfo;
    int weight{};
    bool operator<(const Key& k) const {
        if (weight < k.weight) { return true; }
        if (weight == k.weight) { return pinfo.id < k.id; }
        return false;
    }
};
struct Bully : public ElectionManager {

    Key key;
    struct Config {
        std::chrono::milliseconds min_interval{6000}, max_interval{10000};
    };
    Config config;

    std::unordered_map<uuids::uuid, ExtPeerInfo> peers;

    std::optional<ExtPeerInfo> current_leader;
    std::atomic<bool> running;
    mutable std::recursive_mutex m;
    std::thread th_working;
    std::unique_ptr<ElectionInterface> out;

    ~Bully() { finish(); }

    uuids::uuid id() const override { return key.id; }

    std::optional<PeerInfo> leader() const override {
        std::lock_guard _lk(m);
        if (!current_leader) { return {}; }
        return current_leader->pinfo;
    };
    void add_peers(const ElectionMsg& msg) {
        auto& p = peers[msg.sourceId];
        p.pinfo.id = msg.sourceId;
        p.pinfo.addresses.insert(msg.sourceAddr);
        p.weight = msg.detail.value("weight", 0);
    }
    // 消息回调, 三种返回类型分别表示无返回消息, 有返回消息或产生异常
    ElectionMsgRes on_election_message(const ElectionMsg& msg) override {
        if (!running) { return Timeout{}; }
        VLOG(1) << "peer " << key.id << " recv msg type: " << msg.type;
        std::lock_guard _lk(m);
        add_peers(msg);
        if (msg.type == "bully.victory") { return on_msg_victory(msg); }
        if (msg.type == "bully.ping") { return on_msg_ping(msg); }
        if (msg.type == "bully.challange") { return on_msg_challange(msg); }
        return "invalid msg type " + msg.type;
    }
    ElectionMsgRes on_msg_victory(const ElectionMsg& msg) {
        auto other_key = Key::from_msg(msg);

        if (other_key < key) {
            LOG(INFO) << "peer " << key.id << " recv false victory";
            return {};
        }

        LOG(WARNING) << "peer " << key.id << " set leader: " << msg.sourceId << ", leader value is "
                     << other_key.weight;
        current_leader = peers[msg.sourceId];
        return {};
    }
    ElectionMsgRes on_msg_ping(const ElectionMsg& msg) {
        auto other_key = Key::from_msg(msg);

        if (other_key < key) {
            VLOG(1) << "peer " << key.id << " recv false victory";
            return {};
        }
        if (current_leader.has_value() && !(current_leader.value() < other_key)) {
            return ElectionMsg{key.id, {}, "bully.pong"};
        }
        LOG(WARNING) << "peer " << key.id << " set leader: " << msg.sourceId << ", leader value is "
                     << other_key.weight;
        current_leader = peers[msg.sourceId];

        return ElectionMsg{key.id, {}, "bully.pong"};
    }
    ElectionMsgRes on_msg_challange(const ElectionMsg& msg) {
        auto other_key = Key::from_msg(msg);

        if (other_key < key) {
            LOG(INFO) << "peer " << key.id << " recv false challange";
            return {};
        }
        if (current_leader.has_value() && !(current_leader.value() < other_key)) {
            return ElectionMsg{key.id, {}, "bully.pong"};
        }

        LOG(WARNING) << "peer " << key.id << " set leader: " << msg.sourceId << ", leader value is "
                     << other_key.weight;
        current_leader = peers[msg.sourceId];
        return ElectionMsg{key.id, {}, "bully.pong"};
    }

    bool is_running() const override { return th_working.joinable(); };
    void start() override {
        if (th_working.joinable()) { return; }
        running = true;
        th_working = std::thread(&Bully::work_working, this);
    }
    void finish() override {
        running = false;
        if (th_working.joinable()) { th_working.join(); }

        std::lock_guard _lk(m);
        current_leader.reset();
        peers.clear();
    };

    ElectionMsg make_msg_ping() const {
        return ElectionMsg{key.id, {}, "bully.ping", {{"weight", key.weight}}};
    }
    ElectionMsg make_msg_victory() const {
        return ElectionMsg{key.id, {}, "bully.victory", {{"weight", key.weight}}};
    }
    void work_working() {
        LOG(WARNING) << "Bully manager working thread start, peer " << key.id;
        while (running) {
            {
                std::lock_guard _lk(m);
                if (self_is_leader(*this)) {
                    std::vector<uuids::uuid> need_erase{};
                    for (auto& [id, peer] : peers) {
                        bool ok = send_victory(peer.pinfo);
                        if (!ok) { need_erase.push_back(id); }
                    }
                    for (auto id : need_erase) {
                        peers.erase(id);
                    }
                }
                else if (current_leader) {
                    auto res = send_ping(current_leader->pinfo);
                    if (!is_ok(res)) {

                        LOG(WARNING) << "leader down, peer " << key.id
                                     << " former leader: " << current_leader->pinfo.id;

                        peers.erase(current_leader->pinfo.id);

                        if (is_biggest_in_peers()) {
                            LOG(WARNING)
                                << "self is leader, peer " << key.id << ", value " << key.weight;
                            current_leader = ExtPeerInfo{{key.id}, key.weight};
                            out->broadcast(make_msg_ping());
                        }
                        else if (!peers.empty()) {

                            current_leader = max_id_in_peers();
                            LOG(WARNING) << "set leader in max peers, peer " << key.id
                                         << ", leader: " << current_leader->pinfo.id << ", value "
                                         << current_leader->weight;
                        }
                    }
                }
                else {
                    LOG(WARNING) << "self is leader, peer " << key.id;
                    current_leader = ExtPeerInfo{{key.id}, key.weight};
                }

                out->broadcast(make_msg_ping());
            }
            sleep_at_most(
                random_milliseconds(config.min_interval.count(), config.max_interval.count())
            );
        }
        LOG(ERROR) << "Bully manager working thread finish, peer " << key.id;
    };
    void sleep_at_most(std::chrono::milliseconds ms) const {
        using namespace std::chrono;
        VLOG(1) << "bully sleep for " << ms.count();
        milliseconds sleeped{0};
        const milliseconds interval{100};
        for (; sleeped < ms; sleeped += interval) {
            if (!running) { return; }
            std::this_thread::sleep_for(interval);
        }
    }

    bool send_victory(const PeerInfo& p) const {
        for (const auto& addr : p.addresses) {
            auto res = out->send(addr, make_msg_victory());
            if (!is_timeout(res)) {
                VLOG(1) << "victory " << key.id << " -> " << p.id;
                return true;
            }
        }
        return false;
    };
    ElectionMsgRes send_ping(const PeerInfo& p) const {
        auto msg = make_msg_ping();
        for (const auto& addr : p.addresses) {
            auto res = out->send(addr, msg);
            if (!is_timeout(res)) {
                VLOG(1) << "victory " << key.id << " -> " << p.id;
                return res;
            }
        }
        return Timeout{};
    };
    bool is_biggest_in_peers() const {
        std::lock_guard _lk(m);
        for (const auto& [id, peers] : peers) {
            if (!(peers < key)) { return false; }
        }
        return true;
    }
    std::optional<ExtPeerInfo> max_id_in_peers() const {
        std::lock_guard _lk(m);
        if (peers.empty()) { return {}; }

        uuids::uuid res_id;
        ExtPeerInfo res;
        for (const auto& [id, p] : peers) {
            if (res_id == uuids::uuid{}) {
                res_id = id;
                res = p;
                continue;
            }
            if (id < res_id) { continue; }
            res_id = id;
            res = p;
        }
        return res;
    }
};

using svmatch = std::match_results<std::string_view::const_iterator>;
std::chrono::milliseconds parse_time_duration(std::string_view duration) {
    // 正则表达式匹配时间格式
    std::regex time_regex(R"((\d+(\.\d+)?)(ms|s|m|min|h))");
    svmatch match;

    if (std::regex_match(duration.begin(), duration.end(), match, time_regex)) {
        double value = std::stod(match[1].str());
        std::string unit = match[3].str();

        if (unit == "ms") { return std::chrono::milliseconds(static_cast<long long>(value)); }
        else if (unit == "s") {
            return std::chrono::milliseconds(static_cast<long long>(value * 1000));
        }
        else if (unit == "m" || unit == "min") {
            return std::chrono::milliseconds(static_cast<long long>(value * 60 * 1000));
        }
        else if (unit == "h") {
            return std::chrono::milliseconds(static_cast<long long>(value * 3600 * 1000));
        }
    }

    throw std::invalid_argument("Invalid time duration format");
}

} // namespace

std::unique_ptr<electionmgr::ElectionManager> make_election_mgr_bully(
    uuids::uuid self_id, const nlohmann::json& params, std::unique_ptr<ElectionInterface> intf
) {
    using namespace std::literals;
    using std::chrono::milliseconds;
    auto mgr = std::make_unique<Bully>();
    mgr->key.id = self_id;
    mgr->out = std::move(intf);

    mgr->key.weight = params.value("weight", 0);
    try {
        auto read_duration = [&params](std::string_view key, auto& dest, auto default_value) {
            if (!params.contains(key)) { return; }
            auto& v = params.at(key);
            if (v.is_number()) {
                dest = milliseconds{v.get<int64_t>()};
                return;
            }
            if (v.is_string()) {
                auto sv = v.get<std::string_view>();
                dest = parse_time_duration(sv);
            }
        };
        read_duration("minInterval", mgr->config.min_interval, 6000ms);
        read_duration("maxInterval", mgr->config.max_interval, 12000ms);
        if (mgr->config.min_interval > mgr->config.max_interval) {
            throw std::invalid_argument(strjoin(
                "expectd minInterval <= maxInterval, but find ", mgr->config.min_interval.count(), " > ",
                mgr->config.max_interval.count()
            ));
        }
    }
    catch (std::exception& e) {
        LOG(INFO) << "read bully config failed: " << e.what() << ", using default config";
        mgr->config.min_interval = 6000ms;
        mgr->config.max_interval = 12000ms;
    }
    LOG(INFO) << "election algorithm bully, weight=" << mgr->key.weight
              << ", minInterval=" << mgr->config.min_interval.count()
              << ", maxInterval=" << mgr->config.max_interval.count();
    return std::move(mgr);
}
