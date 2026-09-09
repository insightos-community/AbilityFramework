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
