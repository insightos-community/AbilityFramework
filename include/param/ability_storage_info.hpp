// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace msg_params {
struct AbilityStorageInfo {
    // path of the ability package
    std::string package_path;
    // path of the ability executable
    std::string executable_path;
    // path of the controller; empty if this ability has no controller yet
    std::string controller_path;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(
        AbilityStorageInfo, package_path, executable_path, controller_path
    )
};

} // namespace msg_params
