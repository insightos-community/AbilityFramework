// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

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

// ability package format
// package must contain package.yaml and ability.manifest.yaml
// CRD is built-in to the framework; packages only carry manifest
struct AbilityPackage {
    using Path = std::filesystem::path;
    std::string name;
    semver::version version; // package version
    std::unordered_map<uuids::uuid, AbilityCRD> abilities; // CRD synthesized from manifest (for downstream modules)
    std::unordered_map<uuids::uuid, DeviceCRD> devices; // devices contained in the package
    std::unordered_map<std::string, AbilityManifest> manifests; // ability manifest, keyed by abilityName
    Path path; // root directory of the package
    // if the ability exists,then return its address,else return emptypath
    Path ability_path(std::string_view ability_name) const;
};
