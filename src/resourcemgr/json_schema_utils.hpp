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
#include "resourcemgr/ability_cr.hpp"
#include "resourcemgr/ability_manifest.hpp"
#include "util/expected.hpp"
#include <filesystem>
#include <yaml-cpp/yaml.h>

bool extension_is_yaml(const std::filesystem::path& p);
expected<YAML::Node, std::string> read_yaml_from_path(const std::filesystem::path& p);

// 旧接口: 展开 CRD schema 后校验 CR (仍被部分模块使用)
expected<void, ErrorMsg> validate_with_expanded_crd(
    const AbilityCR& cr, const nlohmann::json& crd_json
);
expected<void, ErrorMsg> validate_to_expected(
    const nlohmann::json& target, const nlohmann::json& schema
);

// 框架级 CR 结构验证 (使用内置 CRD schema)
expected<void, ErrorMsg> validate_cr_framework_level(const nlohmann::json& cr_json);

// 能力级 CR 验证 (使用 manifest 中的 schema)
expected<void, ErrorMsg> validate_cr_manifest_level(
    const AbilityCR& cr, const AbilityManifest& manifest
);

// 强制检测: CR 必须声明 spec.tasks，且每项 taskName 都在 manifest.tasks 中存在
// 用 raw JSON 是因为 AbilitySpec 不解析 tasks 字段
expected<void, ErrorMsg> validate_cr_tasks_field(
    const nlohmann::json& cr_json, const AbilityManifest& manifest
);
