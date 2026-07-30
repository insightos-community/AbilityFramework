// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <unordered_map>
#include <uuid.h>
#include <vector>
// LifecycleState defined here
#include "cr_crd_common.hpp"
#include "util/semver.hpp"
#include "util/uuid_json_convert.hpp"

struct Metadata {
    std::string name; // device instance name
    std::unordered_map<std::string, std::string> labels;
    std::unordered_map<std::string, std::string> annotations;
    friend void from_json(const nlohmann::json&, Metadata&);
    friend void to_json(nlohmann::json&, const Metadata&);
};

struct DeviceSpec {
    std::string package;
    std::string deviceName;
    semver::version version;
    std::string position; // host location of the device
    OwnershipMode ownership;
    BindMode bind;
    // a kind of device's other info stored here
    std::unordered_map<std::string, nlohmann::json> someDevice;
    struct Access {
        std::string type;
        nlohmann::json params;
        friend void from_json(const nlohmann::json&, Access&);
        friend void to_json(nlohmann::json&, const Access&);
    };
    Access access;

    friend void from_json(const nlohmann::json&, DeviceSpec&);
    friend void to_json(nlohmann::json&, const DeviceSpec&);
};

struct DeviceCR {
    std::string kind;
    std::string description;
    Metadata metadata;
    uuids::uuid id;
    nlohmann::json status;
    DeviceSpec spec;

    // id, position
    std::unordered_map<uuids::uuid, std::string> sharers;
    struct OwnerEntry {
        uuids::uuid abilityInstanceId;
        std::string position;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(DeviceCR::OwnerEntry, abilityInstanceId, position);
    };
    OwnerEntry owner;

    friend void from_json(const nlohmann::json&, DeviceCR&);
    friend void to_json(nlohmann::json&, const DeviceCR&);
};
