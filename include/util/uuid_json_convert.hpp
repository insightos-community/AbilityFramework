// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <nlohmann/json.hpp>
#include <span>
#include <uuid.h>

namespace uuids {
inline void from_json(const nlohmann::json& j, uuid& u) {
    if (!j.is_string()) { throw std::invalid_argument("json is not valid uuid string: " + j.dump()); }
    auto op_id = uuid::from_string(j.get<std::string>());
    if (!op_id) { throw std::invalid_argument("json is not valid uuid: " + j.dump()); }
    u = *op_id;
}
inline void to_json(nlohmann::json& j, const uuid& u) {
    j = to_string(u);
}
}; // namespace uuids

namespace std {
template <typename T>
inline void to_json(nlohmann::json& j, const optional<T>& op) {
    if (!op) {
        j = {};
        return;
    }
    to_json(j, *op);
}
}; // namespace std
