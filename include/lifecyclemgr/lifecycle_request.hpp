// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "util/uuid_json_convert.hpp"
#include <nlohmann/json.hpp>
#include <uuid.h>

// info needed for lifecycle operations
struct LifecycleRequest {
    uuids::uuid abilityInstanceId;
    std::string command;
    friend void from_json(const nlohmann::json&, LifecycleRequest&);
    friend void to_json(nlohmann::json&,const LifecycleRequest&);
};
