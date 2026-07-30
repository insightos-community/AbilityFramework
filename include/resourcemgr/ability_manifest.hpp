// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "resourcemgr/ability_crd.hpp"
#include <nlohmann/json.hpp>
#include <string>

// ability manifest: describes a specific ability's interface, schema, dependencies, etc.
// ability-specific fields extracted from the original AbilityCRD, published with the package
// CRD is the framework built-in resource type spec; Manifest is the ability description written by the ability developer
struct AbilityManifest {
    std::string abilityName;
    std::string kind; // AtomAbility | ComposeAbility | AbstractAbility

    // ability-specific fields (migrated from AbilityCRD::Spec)
    nlohmann::json provides;
    nlohmann::json types;
    nlohmann::json rpcMethods;
    nlohmann::json tasks;
    nlohmann::json config; // default config value
    nlohmann::json debugOption; // default debug option

    // Schema is used to validate the corresponding fields in the CR
    struct Schema {
        nlohmann::json constants; // provide constants for parent abilities
        nlohmann::json openAPIV3Schema; // validate config/status/debugOption
        friend void from_json(const nlohmann::json&, Schema&);
        friend void to_json(nlohmann::json&, const Schema&);
    };
    Schema schema;

    // dependency declaration (migrated from AbilityCRD::Deps, reuse existing type)
    AbilityCRD::Deps depends;

    // framework processing status
    AbilityCRD::FwkDetail fwk;

    friend void from_json(const nlohmann::json&, AbilityManifest&);
    friend void to_json(nlohmann::json&, const AbilityManifest&);
};
