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
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <uuid.h>
#include <vector>
// LifecycleState在此定义
#include "cr_crd_common.hpp"
#include "lifecyclemgr/heartbeat.hpp"
#include "util/semver.hpp"
#include "util/uuid_json_convert.hpp"

struct AbilitySpec {
    // 用于组合能力指定子能力
    std::optional<uuids::uuid> id;
    // 用于记录子能力状态
    std::optional<nlohmann::json> status;
    std::string package;
    semver::version version;
    std::string abilityName; // 能力类名称
    // 能力所在的主机位置,默认为localhost或与父能力保持一致
    std::string position;

    std::optional<int> priority;
    nlohmann::json activityCondition;

    // 是否自动启动（仅框架启动时自动拉起一次）
    std::optional<bool> autoStart;
    // 是否保活（能力退出后自动重启）
    std::optional<bool> keepAlive;
    // 是否单例（true: 同一 CR 只能启动一个实例，用于硬件独占类能力）
    std::optional<bool> singleton;

    nlohmann::json config;      // 启动该能力相关的配置参数,内部格式由能力定义
    nlohmann::json debugOption; // 启动该能力相关的调试参数,内部格式由能力定义

    // 若有子能力,每项存储子能力的信息
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
    // 标识该cr是由文件生成还是组合能力中的子能力
    struct Tag {
        std::string source;
        std::string parent;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(AbilityCR::Tag, source, parent);
    };
    Tag tag;
    struct Metadata {
        std::string name; // 能力实例名称
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

    uuids::uuid id; // 能力id
    std::string kind;
    // 能力的运行时状态,若未运行,该项为空json
    nlohmann::json status;
    // 能力的运行基础信息
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
        // 表示该能力所在的主机名
        std::string position;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(AbilityCR::SubAbilityEntry, id, position);
    };
    // 启动后,框架在这里记录它的子能力相关信息,每个子能力也会有自己的CR
    std::vector<SubAbilityEntry> subabilities;
    struct DeviceEntry {
        uuids::uuid id;
        // 表示该能力所在的主机名
        std::string position;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(AbilityCR::DeviceEntry, id, position);
    };
    std::vector<DeviceEntry> devices;
    std::shared_ptr<AbilitySpec> spec;

    friend void from_json(const nlohmann::json&, AbilityCR&);
    friend void to_json(nlohmann::json&, const AbilityCR&);
};
