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
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <uuid.h>
#include <vector>
// LifecycleState在此定义
#include "cr_crd_common.hpp"
#include "util/semver.hpp"
#include "util/uuid_json_convert.hpp"

struct DeviceCRD {
    struct Metadata {
        std::string name; // 设备类名称
        std::unordered_map<std::string, std::string> labels;
        std::unordered_map<std::string, std::string> annotations;
        friend void from_json(const nlohmann::json&, Metadata&);
        friend void to_json(nlohmann::json&, const Metadata&);
    };
    struct Access {
        std::string type;
        nlohmann::json params;
        friend void from_json(const nlohmann::json&, Access&);
        friend void to_json(nlohmann::json&, const Access&);
    };
    struct Spec {
        std::vector<Access> access;
        nlohmann::json openAPIV3Schema;
        friend void from_json(const nlohmann::json&, Access&);
        friend void to_json(nlohmann::json&, const Access&);
    };
    std::string package;
    semver::version version;
    std::string kind;
    Metadata metadata;
    uuids::uuid id;
    std::optional<std::string> description;
    Spec spec;

    friend void from_json(const nlohmann::json&, DeviceCRD&);
    friend void to_json(nlohmann::json&, const DeviceCRD&);
};
