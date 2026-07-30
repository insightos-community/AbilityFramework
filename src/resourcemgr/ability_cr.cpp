// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#include "resourcemgr/ability_cr.hpp"
#include "util/json_codec_macros.hpp"
#include <glog/logging.h>

void from_json(const nlohmann::json& j, AbilityCR::Metadata& x) {
    j.at("name").get_to(x.name);
    read_optional(labels);
    read_optional(annotations);
}

void to_json(nlohmann::json& j, const AbilityCR::Metadata& m) {
    j["name"] = m.name;
    if (!m.labels.empty()) { j["labels"] = m.labels; }
    if (!m.annotations.empty()) { j["annotations"] = m.annotations; }
}

void from_json(const nlohmann::json& j, AbilityCR::RunInfo& r) {
    using TimePoint = AbilityCR::RunInfo::TimePoint;
    using Duration = TimePoint::duration;
    auto make_timepoint = [&j](std::string_view sv) {
        auto x = j.at(sv).get<int64_t>();
        return TimePoint(Duration(x));
    };
    r.lifecycleState = j.at("lifecycleState");
    r.lastUpdate = make_timepoint("lastUpdate");
    r.lastConnect = make_timepoint("lastConnect");
}

void to_json(nlohmann::json& j, const AbilityCR::RunInfo& r) {
    j["lifecycleState"] = r.lifecycleState;
    j["lastUpdate"] = r.lastUpdate.time_since_epoch().count();
    j["lastConnect"] = r.lastConnect.time_since_epoch().count();
}

void from_json(const nlohmann::json& j, AbilitySpec& x) {
    if (j.contains("id")) {
        x.id.emplace();
        j.at("id").get_to(x.id.value());
    }
    if (j.contains("status")) {
        x.status.emplace();
        j.at("status").get_to(x.status.value());
    }
    j.at("package").get_to(x.package);
    x.version = semver::version(j.at("version").get<std::string>());
    j.at("abilityName").get_to(x.abilityName);
    read_with_default(position, "localhost");
    read_optional(config);
    read_optional(debugOption);
    read_optional_unless_empty(activityCondition);
    read_optional(priority);
    // handle devices
    read_optional_unless_empty(devices);
    read_optional_unless_empty(models);

    // handle sub-abilities
    if (j.contains("subabilities") && !j.at("subabilities").empty()) {
        for (const auto& sub : j.at("subabilities")) {
            auto subAbility = std::make_shared<AbilitySpec>();
            from_json(sub, *subAbility);
            x.subabilities.push_back(subAbility);
        }
    }

    read_optional(autoStart);
    read_optional(keepAlive);
    read_optional(singleton);
}

void to_json(nlohmann::json& j, const AbilitySpec& x) {
    output_if_has_value(id);
    output_if_has_value(status);
    output_if_has_value(priority);
    output_unless_empty(activityCondition);
    output_if_has_value(autoStart);
    output_if_has_value(keepAlive);
    output_if_has_value(singleton);

    j["package"] = x.package;
    j["version"] = x.version.to_string();
    j["abilityName"] = x.abilityName;
    j["position"] = x.position;
    j["config"] = x.config;
    output_unless_empty(debugOption);

    output_unless_empty(devices);
    output_unless_empty(models);

    // handle sub-abilities
    if (!x.subabilities.empty()) {
        j["subabilities"] = nlohmann::json::array();
        for (const auto& sub : x.subabilities) {
            nlohmann::json subJson;
            to_json(subJson, *sub);
            j["subabilities"].push_back(subJson);
        }
    }
}
namespace {
std::unordered_map<uuids::uuid, std::string> parse_sharers(const std::string& input) {
    std::unordered_map<uuids::uuid, std::string> result;
    std::istringstream stream(input);
    std::string token;

    while (std::getline(stream, token, ';')) {
        size_t pos = token.find('@');
        if (pos == std::string::npos) { continue; }
        std::string uuid_str = token.substr(0, pos);
        std::string position = token.substr(pos + 1);
        auto id = uuids::uuid::from_string(uuid_str);
        if (id) { result[*id] = position; }
        else
            continue;
    }
    return result;
}

bool judge_owner_validity(const std::string& str) {
    return str.find(';') == std::string::npos;
}

nlohmann::json parse_owner(const std::string& input) {
    nlohmann::json result;
    size_t pos = input.find('@');
    if (pos == std::string::npos) { return result; }
    std::string uuid_str = input.substr(0, pos);
    std::string position = input.substr(pos + 1);
    result["abilityInstanceId"] = uuid_str;
    result["position"] = position;
    return result;
}
} // namespace
void from_json(const nlohmann::json& j, AbilityCR& cr) try {
    if (j.contains("tag")) { j.at("tag").get_to(cr.tag); }
    j.at("metadata").get_to(cr.metadata);
    if (j.contains("id")) { j.at("id").get_to(cr.id); }
    j.at("kind").get_to(cr.kind);
    if (j.contains("status")) { j.at("status").get_to(cr.status); }

    if (j.contains("runInfo") && !j.at("runInfo").empty()) {
        cr.runInfo.emplace();
        j.at("runInfo").get_to(cr.runInfo.value());
    }

    if (j.contains("subabilities") && !j.at("subabilities").empty()) {
        cr.subabilities = j.at("subabilities").get<std::vector<AbilityCR::SubAbilityEntry>>();
    }
    if (j.contains("devices") && !j.at("devices").empty()) {
        cr.devices = j.at("devices").get<std::vector<AbilityCR::DeviceEntry>>();
    }
    if (!cr.metadata.labels.empty()) {
        if (cr.metadata.labels.find("fwk.io/sharers") != cr.metadata.labels.end()) {
            std::string sharers = cr.metadata.labels["fwk.io/sharers"];
            cr.sharers = parse_sharers(sharers);
        }
        if (cr.metadata.labels.find("fwk.io/owner") != cr.metadata.labels.end()) {
            std::string owner = cr.metadata.labels["fwk.io/owner"];
            if (judge_owner_validity(owner)) { cr.owner = parse_owner(owner); }
        }
    }
    if (j.contains("sharers") && !j.at("sharers").empty()) {
        cr.sharers.clear();
        for (auto& [uuid_str, position] : j["sharers"].items()) {
            auto id = uuids::uuid::from_string(uuid_str);
            cr.sharers[*id] = position;
        }
    }
    if (j.contains("owner") && !j.at("owner").empty()) { j.at("owner").get_to(cr.owner); }
    cr.spec = std::make_shared<AbilitySpec>();
    CHECK_NOTNULL(cr.spec);
    j.at("spec").get_to(*cr.spec);
}
catch (nlohmann::json::exception& e) {
    LOG(WARNING) << "when parsing cr:\n" << j.dump(2);
    throw;
}

void to_json(nlohmann::json& j, const AbilityCR& cr) {
    j["tag"] = cr.tag;
    j["metadata"] = cr.metadata;
    j["id"] = cr.id;
    j["kind"] = cr.kind;
    j["status"] = cr.status;

    if (cr.runInfo.has_value()) { j["runInfo"] = cr.runInfo.value(); }

    if (!cr.subabilities.empty()) { j["subabilities"] = cr.subabilities; }
    if (!cr.devices.empty()) { j["devices"] = cr.devices; }
    if (cr.spec) { j["spec"] = *cr.spec; }

    nlohmann::json j_ = nlohmann::json::object();
    for (const auto& pair : cr.sharers) {
        j_[to_string(pair.first)] = pair.second;
    }
    j["sharers"] = j_;
    if (!cr.owner.position.empty())
        j["owner"] = cr.owner;
    else
        j["owner"] = nlohmann::json::object();
}

void from_json(const nlohmann::json& j, AbilitySpec::DeviceEntry& x) {
    read_required(deviceName);
    read_required(instanceName);
    read_optional(ownership);
    read_optional(bind);
}
void to_json(nlohmann::json& j, const AbilitySpec::DeviceEntry& x) {
    j["deviceName"] = x.deviceName;
    j["instanceName"] = x.instanceName;
    j["ownership"] = x.ownership;
    j["bind"] = x.bind;
}

std::string read_as_string(const nlohmann::json& j) {
    if (j.is_string()) { return j.get<std::string>(); }
    if (j.is_number()) { return std::to_string(j.get<int64_t>()); }
    throw std::runtime_error("need model id be either number or string, but get: " + j.dump());
}

void from_json(const nlohmann::json& j, AbilitySpec::ModelEntry& x) {
    x.id = read_as_string(j.at("id"));
}
void to_json(nlohmann::json& j, const AbilitySpec::ModelEntry& x) {
    output(id);
}
