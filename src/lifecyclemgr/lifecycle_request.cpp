// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#include "lifecyclemgr/lifecycle_request.hpp"

namespace {
[[nodiscard]]
std::string to_lower(std::string s) {
    for (auto& c : s) {
        c = static_cast<char>(tolower(c));
    }
    return s;
}

bool is_legal(std::string_view cmd) {
    constexpr std::string_view cmds[] = {"start", "connect", "disconnect", "terminate"};
    for (auto& c : cmds) {
        if (c == cmd) { return true; }
    }
    return false;
}
} // namespace

void from_json(const nlohmann::json& j, LifecycleRequest& r) {
    auto cmd = to_lower(j.at("command").get<std::string>());
    if (!is_legal(cmd)) { throw std::invalid_argument("invalid command: " + cmd); }
    j.at("abilityInstanceId").get_to(r.abilityInstanceId);
    j.at("command").get_to(r.command);
}

void to_json(nlohmann::json& j, const LifecycleRequest& r) {
    j["abilityInstanceId"] = r.abilityInstanceId;
    j["command"] = r.command;
}