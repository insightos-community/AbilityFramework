// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "util/semver.hpp"
#include <nlohmann/json.hpp>
#include <uuid.h>

namespace semver {
inline void from_json(const nlohmann::json& j, version& v) {
    if (!j.is_string()) {
        throw std::invalid_argument("json is not valid semver string: " + j.dump());
    }
    auto ver_str = j.get<std::string>();
    v = version(ver_str);
}
inline void to_json(nlohmann::json& j, const version& u) {
    j = u.to_string();
}
}; // namespace semver
