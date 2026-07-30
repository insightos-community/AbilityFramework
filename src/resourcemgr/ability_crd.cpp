// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#include "resourcemgr/ability_crd.hpp"
#include <glog/logging.h>

#define UnImplemented(Name) \
    { throw std::logic_error(#Name + std::string("is not implemented")); }
#define read_required(Name) j.at(#Name).get_to(x.Name)

#define read_optional(Name)                        \
    {                                              \
        auto it = j.find(#Name);                   \
        if (it != j.end()) { it->get_to(x.Name); } \
    }

#define output(Name) (j[#Name] = x.Name)

#define output_unless_empty(Name) \
    if (!x.Name.empty()) { j[#Name] = x.Name; }

void from_json(const nlohmann::json& j, AbilityCRD::Metadata& x) {
    read_required(name);
    read_optional(labels);
    read_optional(annotations);
}

void to_json(nlohmann::json& j, const AbilityCRD::Metadata& x) {
    output(name);
    output_unless_empty(labels);
    output_unless_empty(annotations);
}

void from_json(const nlohmann::json& j, AbilityCRD::AbilityDep& x) {
    read_required(abilityName);

    read_optional(ownership);
    read_optional(package);
    read_optional(bind);
    read_optional(description);
}

void to_json(nlohmann::json& j, const AbilityCRD::AbilityDep& x) {
    output_unless_empty(package);
    output(abilityName);
    output(ownership);
    output(bind);
    output_unless_empty(description);
}

void from_json(const nlohmann::json& j, AbilityCRD::DeviceDep& x) {
    read_required(deviceName);

    read_optional(ownership);
    read_optional(package);
    read_optional(bind);
    read_optional(description);
}

void to_json(nlohmann::json& j, const AbilityCRD::DeviceDep& x) {
    output_unless_empty(package);
    output(deviceName);
    output(ownership);
    output(bind);
    output_unless_empty(description);
}

void from_json(const nlohmann::json& j, AbilityCRD::Deps& x) {
    if (j.contains("abilities")) {
        const auto& abilities = j.at("abilities");
        if (abilities.is_array()) {
            abilities.get_to(x.abilities);
            x.num_ability_is_arbitrary = false;
        }
        else if (abilities.is_object()) {
            x.num_ability_is_arbitrary = true;
            x.abilities.clear();
            x.abilities.push_back(abilities.get<AbilityCRD::AbilityDep>());
        }
        else { throw std::invalid_argument("invalid field /depends/abilities"); }
    }
    read_optional(devices);
    read_optional(additionalAbilities);
    read_optional(additionalDevices);
}

void to_json(nlohmann::json& j, const AbilityCRD::Deps& x) {
    if (x.num_ability_is_arbitrary) {
        CHECK_GE(x.abilities.size(), 1) << "why? class invariant violated";
        j["abilities"] = x.abilities[0];
    }
    else { output_unless_empty(abilities); }
    output_unless_empty(devices);
    output_unless_empty(additionalAbilities);
    output_unless_empty(additionalDevices);
}

void from_json(const nlohmann::json& j, AbilityCRD::SchemaInfo& x) {
    read_optional(constants);
    // openAPIV3Schema needs intent expansion
    // but for now read it in first, deal with its expansion later
    read_required(openAPIV3Schema);
}

void to_json(nlohmann::json& j, const AbilityCRD::SchemaInfo& x) {
    output_unless_empty(constants);
    output(openAPIV3Schema);
}

void from_json(const nlohmann::json& j, AbilityCRD::Spec& x) {
    read_required(schema);
    read_optional(provides);
    read_optional(types);
    read_optional(rpcMethods);
    read_optional(tasks);
    read_optional(config);
    read_optional(debugOption);
}

void to_json(nlohmann::json& j, const AbilityCRD::Spec& x) {
    output_unless_empty(provides);
    output(schema);
    output_unless_empty(types);
    output_unless_empty(rpcMethods);
    output_unless_empty(tasks);
    output_unless_empty(config);
    output_unless_empty(debugOption);
}

void from_json(const nlohmann::json& j, AbilityCRD::FwkDetail& x) {
    read_optional(complete);
    read_optional(ref_expanded);
}

void to_json(nlohmann::json& j, const AbilityCRD::FwkDetail& x) {
    output(complete);
    output(ref_expanded);
}

void from_json(const nlohmann::json& j, AbilityCRD& x) {
    read_required(packageName);
    x.version = semver::version(j.at("version").get<std::string>());
    read_required(kind);
    read_required(metadata);
    read_required(spec);
    read_optional(depends);
    read_optional(id);
}

void to_json(nlohmann::json& j, const AbilityCRD& x) {
    output(packageName);
    j["version"] = x.version.to_string();
    output(kind);
    output_unless_empty(depends);
    if (x.id != uuids::uuid{}) { j["id"] = to_string(x.id); }
    output(metadata);
    output(spec);
    output(fwk);
}
