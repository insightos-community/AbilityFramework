// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "util/uuid_json_convert.hpp"
#include <nlohmann/json.hpp>

namespace msg_params {
struct AbilityClassInfo {
    std::string abilityPackageName;
    std::string abilityVersion;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(AbilityClassInfo, abilityPackageName, abilityVersion);
};

} // namespace msg_params
