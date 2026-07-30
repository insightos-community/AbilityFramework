// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

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
