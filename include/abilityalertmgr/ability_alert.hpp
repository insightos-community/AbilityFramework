// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "lifecyclemgr/heartbeat.hpp"
#include "prelude.hpp"

#include <nlohmann/json.hpp>
using AbilityErrorCode = int;

enum class AbilityAlertLevel {
    unknown,
    // some normal info, emitted when the ability is running normally
    info,
    // ability encountered an exception at runtime, but the original flow can still run normally
    anomaly,
    // ability encountered a severe error, original logic cannot run, but can be recovered via error handling and continue afterwards
    error,
    // ability encountered a fatal error, cannot continue, will terminate soon
    fatal
};
std::string_view to_string(AbilityAlertLevel);
FWK_DECLARE_FROM_SV(AbilityAlertLevel);

NLOHMANN_JSON_SERIALIZE_ENUM(
    AbilityAlertLevel,
    {{AbilityAlertLevel::unknown, "unknown"},
     {AbilityAlertLevel::info, "info"},
     {AbilityAlertLevel::anomaly, "anomaly"},
     {AbilityAlertLevel::error, "error"},
     {AbilityAlertLevel::fatal, "fatal"}}
);

struct AbilityAlert {
    // ability-defined error code
    AbilityErrorCode code;
    std::string message; // error reason
    // the process that caused the error; this may not be the function name, could be the task name
    std::string error_operation;
    AbilityAlertLevel level; // error level
                             //
    using DateTimeStr = std::string;
    DateTimeStr time; // error occurrence time

    struct AbilityBaseInfo {
        std::string id;
        std::string abilityName;
        std::string instanceName;
        std::string version;
        int abilityPort;
        int IPCPort;
        LifecycleState state;
    };
    Heartbeat abilityInfo;

    // error details contained in the ability
    nlohmann::json detail;
    // environment when the ability encounters an exception
    nlohmann::json context;

    struct ErrorLocation {
        std::string file;
        int line;
        std::string function;
    };
    ErrorLocation location;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(
    AbilityAlert::AbilityBaseInfo, id, abilityName, instanceName, version
);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AbilityAlert::ErrorLocation, file, line, function);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(
    AbilityAlert,
    code,
    message,
    error_operation,
    level,
    time,
    abilityInfo,
    detail,
    context,
    location
);
