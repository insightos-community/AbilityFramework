// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#include "resourcemgr/ability_manifest.hpp"

// ability_crd.cpp defines Deps from_json/to_json
// but Deps friend declaresays DeviceDep&(original code bug), hereforward-declare the correct signature
void from_json(const nlohmann::json&, AbilityCRD::Deps&);
void to_json(nlohmann::json&, const AbilityCRD::Deps&);

#define read_required(Name) j.at(#Name).get_to(x.Name)

#define read_optional(Name)                         \
    {                                               \
        auto it = j.find(#Name);                    \
        if (it != j.end()) { it->get_to(x.Name); } \
    }

#define output(Name) (j[#Name] = x.Name)

#define output_unless_empty(Name) \
    if (!x.Name.empty()) { j[#Name] = x.Name; }

void from_json(const nlohmann::json& j, AbilityManifest::Schema& x) {
    read_optional(constants);
    read_optional(openAPIV3Schema);
}

void to_json(nlohmann::json& j, const AbilityManifest::Schema& x) {
    output_unless_empty(constants);
    if (!x.openAPIV3Schema.is_null()) { output(openAPIV3Schema); }
}

void from_json(const nlohmann::json& j, AbilityManifest& x) {
    read_required(abilityName);
    read_required(kind);
    read_optional(provides);
    read_optional(types);
    read_optional(rpcMethods);
    read_optional(tasks);
    read_optional(config);
    read_optional(debugOption);
    read_optional(schema);
    {
        auto it = j.find("depends");
        if (it != j.end()) { from_json(*it, x.depends); }
    }
    read_optional(fwk);
}

void to_json(nlohmann::json& j, const AbilityManifest& x) {
    output(abilityName);
    output(kind);
    output_unless_empty(provides);
    output_unless_empty(types);
    output_unless_empty(rpcMethods);
    output_unless_empty(tasks);
    output_unless_empty(config);
    output_unless_empty(debugOption);
    j["schema"] = x.schema;
    if (!x.depends.empty()) { j["depends"] = x.depends; }
}
