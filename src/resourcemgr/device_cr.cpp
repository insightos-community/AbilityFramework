// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#include "resourcemgr/device_cr.hpp"
#include "glog/logging.h"
#include "util/json_codec_macros.hpp"
#include "util/scope.hpp"
#include "util/semver_json_convert.hpp"
#include "util/uuid_json_convert.hpp"

void from_json(const nlohmann::json& j, Metadata& x) {
    j.at("name").get_to(x.name);
    read_optional(labels);
    read_optional(annotations);
}

void to_json(nlohmann::json& j, const Metadata& x) {
    j["name"] = x.name;
    j["labels"] = x.labels;
    output_unless_empty(labels);
    output_unless_empty(annotations);
}

void from_json(const nlohmann::json& j, DeviceSpec& x) {
    read_required(package);
    read_required(deviceName);
    read_required(version);
    read_with_default(position, "localhost");
    read_optional(ownership);
    read_optional(bind);
    read_required(access);

    for (auto it = j.cbegin(); it != j.cend(); ++it) {
        if (it.key() != "position" && it.key() != "ownership" && it.key() != "bind") {
            x.someDevice[it.key()] = it.value();
        }
    }
}

void to_json(nlohmann::json& j, const DeviceSpec& x) {
    for (const auto& [key, value] : x.someDevice) {
        j[key] = value;
    }
    output(deviceName);
    output(package);
    j["version"] = x.version.to_string();
    j["position"] = x.position;
    j["ownership"] = x.ownership;
    j["bind"] = x.bind;
    output(access);
}

void from_json(const nlohmann::json& j, DeviceSpec::Access& x) {
    read_required(type);
    read_optional_unless_empty(params);
}

void to_json(nlohmann::json& j, const DeviceSpec::Access& x) {
    output(type);
    output_unless_empty(params);
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
void from_json(const nlohmann::json& j, DeviceCR& x) {
    scope_fail complain([&j] { LOG(WARNING) << "when parsing device cr\n" << j.dump(2); });

    // j.at("packageName").get_to(x.package);
    j.at("kind").get_to(x.kind);
    read_optional(description);
    j.at("metadata").get_to(x.metadata);
    read_optional(id);
    read_optional(status);
    read_required(spec);

    if (!x.metadata.labels.empty()) {
        if (x.metadata.labels.find("fwk.io/sharers") != x.metadata.labels.end()) {
            std::string sharers = x.metadata.labels["fwk.io/sharers"];
            x.sharers = parse_sharers(sharers);
        }
        if (x.metadata.labels.find("fwk.io/owner") != x.metadata.labels.end()) {
            std::string owner = x.metadata.labels["fwk.io/owner"];
            if (judge_owner_validity(owner)) { x.owner = parse_owner(owner); }
        }
    }
    if (j.contains("sharers") && !j.at("sharers").empty()) {
        x.sharers.clear();
        for (auto& [uuid_str, position] : j["sharers"].items()) {
            auto id = uuids::uuid::from_string(uuid_str);
            x.sharers[*id] = position;
        }
    }
    if (j.contains("owner") && !j.at("owner").empty()) { j.at("owner").get_to(x.owner); }
}

void to_json(nlohmann::json& j, const DeviceCR& x) {
    // j["packageName"] = cr.package;
    //  j["version"] = cr.version.to_string();
    j["kind"] = x.kind;
    output_unless_empty(description);
    j["metadata"] = x.metadata;
    j["id"] = x.id;
    output_unless_empty(status);
    j["spec"] = x.spec;

    nlohmann::json j_ = nlohmann::json::object();
    for (const auto& pair : x.sharers) {
        j_[to_string(pair.first)] = pair.second;
    }
    j["sharers"] = j_;
    if (!x.owner.position.empty())
        j["owner"] = x.owner;
    else
        j["owner"] = nlohmann::json::object();
}
