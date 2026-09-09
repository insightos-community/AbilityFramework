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
#include "cr_crd_common.hpp"
#include "lifecyclemgr/service_state.hpp"
#include "resourcemgr/ability_cr.hpp" // for DeviceEntry, ModelEntry
#include "util/semver.hpp"
#include "util/uuid_json_convert.hpp"
#include <chrono>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <uuid.h>
#include <vector>

struct ServiceSpec {
    std::string package;
    semver::version version;
    std::string serviceName;
    std::string position = "localhost";
    nlohmann::json config;

    // 重启策略
    std::string restartPolicy = "always"; // always | on-failure | never
    int maxRestarts = 5;
    int restartBackoffSeconds = 10;

    // 健康检查
    struct Probe {
        std::string type;  // "http" | "process"
        std::string path;
        int port = 0;
        int intervalSeconds = 10;
        int failureThreshold = 3;
        friend void from_json(const nlohmann::json&, Probe&);
        friend void to_json(nlohmann::json&, const Probe&);
    };
    struct HealthCheck {
        std::optional<Probe> liveness;
        std::optional<Probe> readiness;
        friend void from_json(const nlohmann::json&, HealthCheck&);
        friend void to_json(nlohmann::json&, const HealthCheck&);
    };
    std::optional<HealthCheck> healthCheck;

    // 设备和模型引用 (复用 AbilitySpec 中的类型)
    std::vector<AbilitySpec::DeviceEntry> devices;
    std::vector<AbilitySpec::ModelEntry> models;

    friend void from_json(const nlohmann::json&, ServiceSpec&);
    friend void to_json(nlohmann::json&, const ServiceSpec&);
};

struct ServiceCR {
    uuids::uuid id;
    std::string kind = "Service";
    struct Metadata {
        std::string name;
        std::unordered_map<std::string, std::string> labels;
        std::unordered_map<std::string, std::string> annotations;
        friend void from_json(const nlohmann::json&, Metadata&);
        friend void to_json(nlohmann::json&, const Metadata&);
    };
    Metadata metadata;
    std::shared_ptr<ServiceSpec> spec;

    // 运行时状态
    ServiceState state = ServiceState::Stopped;
    int restartCount = 0;
    std::optional<std::chrono::system_clock::time_point> lastRestart;

    friend void from_json(const nlohmann::json&, ServiceCR&);
    friend void to_json(nlohmann::json&, const ServiceCR&);
};
