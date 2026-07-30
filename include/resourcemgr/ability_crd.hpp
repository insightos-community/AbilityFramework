// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "cr_crd_common.hpp"
#include "util/semver.hpp"
#include "util/uuid_json_convert.hpp"
#include <nlohmann/json.hpp>
#include <string>
struct AbilityCRD {
    uuids::uuid id;
    std::string packageName; // which package this ability belongs to
    semver::version version;
    std::string kind;
    struct Metadata {
        std::string name;
        std::unordered_map<std::string, std::string> labels;
        std::unordered_map<std::string, std::string> annotations;

        friend void from_json(const nlohmann::json&, Metadata&);
        friend void to_json(nlohmann::json&, const Metadata&);
    };
    Metadata metadata;
    struct AbilityDep {
        std::string package;
        std::string abilityName; // ability type name
        OwnershipMode ownership{OwnershipMode::unique};
        BindMode bind{BindMode::local};
        std::string description; // optional description text
        friend void from_json(const nlohmann::json&, AbilityDep&);
        friend void to_json(nlohmann::json&, const AbilityDep&);
    };
    struct DeviceDep {
        std::string package;
        std::string deviceName; // device type name
        OwnershipMode ownership{OwnershipMode::unique};
        BindMode bind{BindMode::local};
        std::string description; // optional description text
        friend void from_json(const nlohmann::json&, DeviceDep&);
        friend void to_json(nlohmann::json&, const DeviceDep&);
    };
    struct Deps {
        std::vector<AbilityDep> abilities; // sub-ability dependency
        std::vector<AbilityDep> additionalAbilities; // attach sub-ability dependency
        // ifdepends/abilitiesis notarray,but onlyan item,
        // indicates this ability accepts a variable number of sub-abilities, but they must be uniform
        // commonly used to replace abilities in a set
        // if true, there is only one item in abilities
        bool num_ability_is_arbitrary = false;
        std::vector<DeviceDep> devices; // device dependency
        std::vector<DeviceDep> additionalDevices; // attach sub-ability dependency
        friend void from_json(const nlohmann::json&, DeviceDep&);
        friend void to_json(nlohmann::json&, const DeviceDep&);
        [[nodiscard]] bool empty() const {
            return abilities.empty() && devices.empty() && additionalDevices.empty()
                && additionalAbilities.empty();
        }
    };
    Deps depends;
    struct SchemaInfo {
        nlohmann::json constants; // provide constants for parent abilities
        nlohmann::json openAPIV3Schema; // ownSchema
        friend void from_json(const nlohmann::json&, SchemaInfo&);
        friend void to_json(nlohmann::json&, const SchemaInfo&);
    };
    struct Spec {
        nlohmann::json provides;
        SchemaInfo schema;
        nlohmann::json types;
        nlohmann::json rpcMethods;
        nlohmann::json tasks;
        nlohmann::json config;
        nlohmann::json debugOption;
        friend void from_json(const nlohmann::json&, Spec&);
        friend void to_json(nlohmann::json&, const Spec&);
    };
    Spec spec;
    // info used by the framework when processing CRD, not exposed externally
    struct FwkDetail {
        // whether this CRD computation is complete,i.e. itsdepends items are already processed,
        // whether ref, shadow, default are fully expanded
        // ResourceMgr keeps an unexpanded original copy,
        // but during validation, the expanded CRD is needed
        bool complete = false;
        // /spec/schema/openAPIV3Schema in x-ref items whether expanded
        bool ref_expanded = false;
        // as needed, there may be more content here
        friend void from_json(const nlohmann::json&, FwkDetail&);
        friend void to_json(nlohmann::json&, const FwkDetail&);
    };
    FwkDetail fwk;

    friend void from_json(const nlohmann::json&, AbilityCRD&);
    friend void to_json(nlohmann::json&, const AbilityCRD&);
};
