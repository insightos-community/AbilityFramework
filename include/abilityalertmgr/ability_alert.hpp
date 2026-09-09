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
#include "lifecyclemgr/heartbeat.hpp"
#include "prelude.hpp"

#include <nlohmann/json.hpp>
using AbilityErrorCode = int;

enum class AbilityAlertLevel {
    unknown,
    // 一些平常信息,用于能力正常运行时发出
    info,
    // 能力运行时遭遇的异常情况,但是原有的流程还能正常运行
    anomaly,
    // 能力遭遇严重错误,原有的逻辑无法运行,但还可以通过错误处理机制挽救,之后还可以继续运行
    error,
    // 能力遭遇致命错误,无法继续运行,不久后将终止
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
    // 能力自定义的错误码
    AbilityErrorCode code;
    std::string message; // 错误原因
    // 导致错误的过程,这一项不一定使函数名,也可能是任务名
    std::string error_operation;
    AbilityAlertLevel level; // 错误等级
                             //
    using DateTimeStr = std::string;
    DateTimeStr time; // 错误发生的时间

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

    // 能力包含的错误细节
    nlohmann::json detail;
    // 能力产生异常时的环境
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
