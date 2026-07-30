// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <nlohmann/json.hpp>

// get the framework built-in Ability CRD schema (for validating CR structure)
const nlohmann::json& get_builtin_ability_crd_schema();

// get the framework built-in Service CRD schema (Phase 2)
const nlohmann::json& get_builtin_service_crd_schema();
