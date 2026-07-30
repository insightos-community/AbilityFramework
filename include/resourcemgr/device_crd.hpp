// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <uuid.h>
#include <vector>
// LifecycleState defined here
#include "cr_crd_common.hpp"
#include "util/semver.hpp"
#include "util/uuid_json_convert.hpp"

struct DeviceCRD {
    struct Metadata {
        std::string name; // device class name
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
