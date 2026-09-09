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

#include "resourcemgr/ability_crd.hpp"
#include "resourcemgr/ability_manifest.hpp"
#include "resourcemgr/device_crd.hpp"
#include "util/semver.hpp"
#include <filesystem>
#include <string>
#include <unordered_map>
#include <uuid.h>
#include <vector>

// 能力包格式
// 包内必须包含 package.yaml 和 ability.manifest.yaml
// CRD 由框架内置，包只携带 manifest
struct AbilityPackage {
    using Path = std::filesystem::path;
    std::string name;
    semver::version version;                               // 包版本
    std::unordered_map<uuids::uuid, AbilityCRD> abilities; // 从 manifest 合成的 CRD（供下游模块使用）
    std::unordered_map<uuids::uuid, DeviceCRD> devices;    // 包中含有的设备
    std::unordered_map<std::string, AbilityManifest> manifests; // 能力清单, keyed by abilityName
    Path path;                                             // 包的根目录地址
    // 如果该能力存在,则返回它的地址,否则返回空path
    Path ability_path(std::string_view ability_name) const;
};
