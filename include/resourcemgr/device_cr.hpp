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
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <unordered_map>
#include <uuid.h>
#include <vector>
// LifecycleState在此定义
#include "cr_crd_common.hpp"
#include "util/semver.hpp"
#include "util/uuid_json_convert.hpp"

struct Metadata {
    std::string name; // 设备实例名称
    std::unordered_map<std::string, std::string> labels;
    std::unordered_map<std::string, std::string> annotations;
    friend void from_json(const nlohmann::json&, Metadata&);
    friend void to_json(nlohmann::json&, const Metadata&);
};

struct DeviceSpec {
    std::string package;
    std::string deviceName;
    semver::version version;
    std::string position; // 设备所在的主机位置
    OwnershipMode ownership;
    BindMode bind;
    // 一种设备的其他信息存储在此
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
