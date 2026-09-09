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
#include <nlohmann/json.hpp>
#include <string_view>

enum class ServiceState : char {
    Stopped = 0,
    Starting = 1,
    Running = 2,
    Failed = 3,
    Restarting = 4,
};

inline std::string_view to_string(ServiceState s) {
    switch (s) {
    case ServiceState::Stopped: return "Stopped";
    case ServiceState::Starting: return "Starting";
    case ServiceState::Running: return "Running";
    case ServiceState::Failed: return "Failed";
    case ServiceState::Restarting: return "Restarting";
    default: return "<unknown>";
    }
}

#define _field(Name) \
    { ServiceState::Name, #Name }
NLOHMANN_JSON_SERIALIZE_ENUM(
    ServiceState,
    {_field(Stopped), _field(Starting), _field(Running), _field(Failed), _field(Restarting)}
)
#undef _field
