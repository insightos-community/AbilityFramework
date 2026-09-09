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