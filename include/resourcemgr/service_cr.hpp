// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

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

    // restart policy
    std::string restartPolicy = "always"; // always | on-failure | never
    int maxRestarts = 5;
    int restartBackoffSeconds = 10;

    // health check
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

    // device and model references (reuse types from AbilitySpec)
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

    // runtime status
    ServiceState state = ServiceState::Stopped;
    int restartCount = 0;
    std::optional<std::chrono::system_clock::time_point> lastRestart;

    friend void from_json(const nlohmann::json&, ServiceCR&);
    friend void to_json(nlohmann::json&, const ServiceCR&);
};
