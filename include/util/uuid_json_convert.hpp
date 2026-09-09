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
