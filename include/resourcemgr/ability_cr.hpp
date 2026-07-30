// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <uuid.h>
#include <vector>
// LifecycleState defined here
#include "cr_crd_common.hpp"
#include "lifecyclemgr/heartbeat.hpp"
#include "util/semver.hpp"
#include "util/uuid_json_convert.hpp"

struct AbilitySpec {
    // used by composite abilities to specify sub-abilities
    std::optional<uuids::uuid> id;
    // used to record sub-ability status
    std::optional<nlohmann::json> status;
    std::string package;
    semver::version version;
    std::string abilityName; // ability class name
    // ability host location; default is localhost or keep consistent with parent ability
    std::string position;

    std::optional<int> priority;
    nlohmann::json activityCondition;

    // auto-start (auto-launch once on framework startup only)
    std::optional<bool> autoStart;
    // keep-alive(abilityauto-restart after exit)
    std::optional<bool> keepAlive;
    // singleton (true: only one instance per CR, for hardware-exclusive abilities)
    std::optional<bool> singleton;

    nlohmann::json config; // config parameters for starting this ability,internal format defined by the ability
    nlohmann::json debugOption; // debug parameters for starting this ability,internal format defined by the ability

    // if there are sub-abilities, each stores sub-ability info
    std::vector<std::shared_ptr<AbilitySpec>> subabilities;
    struct DeviceEntry {
        std::string deviceName;
        std::string instanceName;
        OwnershipMode ownership{OwnershipMode::unique};
        BindMode bind{BindMode::local};

        friend void from_json(const nlohmann::json&, DeviceEntry&);
        friend void to_json(nlohmann::json&, const DeviceEntry&);
    };
    std::vector<DeviceEntry> devices;
    struct ModelEntry {
        std::string id;
        friend void from_json(const nlohmann::json&, ModelEntry&);
        friend void to_json(nlohmann::json&, const ModelEntry&);
    };

    std::vector<ModelEntry> models;
    friend void from_json(const nlohmann::json&, AbilitySpec&);
    friend void to_json(nlohmann::json&, const AbilitySpec&);
};

struct AbilityCR {
    // indicates this cr is generated from a file or is a sub-ability in a composite ability
    struct Tag {
        std::string source;
        std::string parent;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(AbilityCR::Tag, source, parent);
    };
    Tag tag;
    struct Metadata {
        std::string name; // ability instance name
        std::unordered_map<std::string, std::string> labels;
        std::unordered_map<std::string, std::string> annotations;
        friend void from_json(const nlohmann::json&, Metadata&);
        friend void to_json(nlohmann::json&, const Metadata&);
    };
    // id, position
    std::unordered_map<uuids::uuid, std::string> sharers;
    struct OwnerEntry {
        uuids::uuid abilityInstanceId;
        std::string position;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(AbilityCR::OwnerEntry, abilityInstanceId, position);
    };
    OwnerEntry owner;
    Metadata metadata;

    uuids::uuid id; // ability id
    std::string kind;
    // runtime status of the ability; empty json if not running
    nlohmann::json status;
    // basic runtime info of the ability
    struct RunInfo {
        using Clock = std::chrono::system_clock;
        using TimePoint = std::chrono::system_clock::time_point;
        LifecycleState lifecycleState;
        TimePoint lastUpdate;
        TimePoint lastConnect;
        friend void from_json(const nlohmann::json&, RunInfo&);
        friend void to_json(nlohmann::json&, const RunInfo&);
    };
    std::optional<RunInfo> runInfo;
    struct SubAbilityEntry {
        uuids::uuid id;
        // indicates the host where this ability resides
        std::string position;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(AbilityCR::SubAbilityEntry, id, position);
    };
    // after startup, the framework records sub-ability info here; each sub-ability also has its own CR
    std::vector<SubAbilityEntry> subabilities;
    struct DeviceEntry {
        uuids::uuid id;
        // indicates the host where this ability resides
        std::string position;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(AbilityCR::DeviceEntry, id, position);
    };
    std::vector<DeviceEntry> devices;
    std::shared_ptr<AbilitySpec> spec;

    friend void from_json(const nlohmann::json&, AbilityCR&);
    friend void to_json(nlohmann::json&, const AbilityCR&);
};
