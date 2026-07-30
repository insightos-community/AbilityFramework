// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

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
