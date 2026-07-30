// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "resourcemgr/ability_cr.hpp"
#include "resourcemgr/ability_manifest.hpp"
#include "util/expected.hpp"
#include <filesystem>
#include <yaml-cpp/yaml.h>

bool extension_is_yaml(const std::filesystem::path& p);
expected<YAML::Node, std::string> read_yaml_from_path(const std::filesystem::path& p);

// old interface: validate CR after expanding CRD schema (still used by some modules)
expected<void, ErrorMsg> validate_with_expanded_crd(
    const AbilityCR& cr, const nlohmann::json& crd_json
);
expected<void, ErrorMsg> validate_to_expected(
    const nlohmann::json& target, const nlohmann::json& schema
);

// framework-level CR structure validation (using built-in CRD schema)
expected<void, ErrorMsg> validate_cr_framework_level(const nlohmann::json& cr_json);

// ability-level CR validation (using schema in manifest)
expected<void, ErrorMsg> validate_cr_manifest_level(
    const AbilityCR& cr, const AbilityManifest& manifest
);

// force check: CR mustdeclare spec.tasks, andeach item taskName are all in manifest.tasks exists in
// raw JSON is used because AbilitySpec does not parse the tasks field
expected<void, ErrorMsg> validate_cr_tasks_field(
    const nlohmann::json& cr_json, const AbilityManifest& manifest
);
