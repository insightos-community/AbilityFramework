// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>
nlohmann::json yaml_to_json(const YAML::Node& ynode);
YAML::Node json_to_yaml(const nlohmann::json& j);
