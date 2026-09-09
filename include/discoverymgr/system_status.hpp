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

enum class SystemLoad { idle, busy, optimal, overloaded, unknown };

enum class SystemLoadTrend { increasing, decreasing, stable, unknown };


NLOHMANN_JSON_SERIALIZE_ENUM(
    SystemLoadTrend,
    {{SystemLoadTrend::increasing, "increasing"},
     {SystemLoadTrend::decreasing, "decreasing"},
     {SystemLoadTrend::stable, "stable"},
     {SystemLoadTrend::unknown, "unknown"}}
)
NLOHMANN_JSON_SERIALIZE_ENUM(
    SystemLoad,
    {{SystemLoad::idle, "idle"},
     {SystemLoad::busy, "busy"},
     {SystemLoad::optimal, "optimal"},
     {SystemLoad::overloaded, "overloaded"},
     {SystemLoad::unknown, "unknown"}}
)

struct SystemInfo {
    SystemLoad load;
    SystemLoadTrend trend;
    static SystemInfo current();
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(SystemInfo, load, trend);
};
