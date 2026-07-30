// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#include "resourcemgr/service_cr.hpp"

#define read_required(Name) j.at(#Name).get_to(x.Name)
#define read_optional(Name)                         \
    {                                               \
        auto it = j.find(#Name);                    \
        if (it != j.end()) { it->get_to(x.Name); } \
    }
#define output(Name) (j[#Name] = x.Name)
#define output_unless_empty(Name) \
    if (!x.Name.empty()) { j[#Name] = x.Name; }

void from_json(const nlohmann::json& j, ServiceSpec::Probe& x) {
    read_required(type);
    read_optional(path);
    read_optional(port);
    read_optional(intervalSeconds);
    read_optional(failureThreshold);
}

void to_json(nlohmann::json& j, const ServiceSpec::Probe& x) {
    output(type);
    output_unless_empty(path);
    if (x.port != 0) { output(port); }
    output(intervalSeconds);
    output(failureThreshold);
}

void from_json(const nlohmann::json& j, ServiceSpec::HealthCheck& x) {
    if (j.contains("liveness")) { x.liveness = j.at("liveness").get<ServiceSpec::Probe>(); }
    if (j.contains("readiness")) { x.readiness = j.at("readiness").get<ServiceSpec::Probe>(); }
}

void to_json(nlohmann::json& j, const ServiceSpec::HealthCheck& x) {
    if (x.liveness) { j["liveness"] = *x.liveness; }
    if (x.readiness) { j["readiness"] = *x.readiness; }
}

void from_json(const nlohmann::json& j, ServiceSpec& x) {
    read_required(package);
    x.version = semver::version(j.at("version").get<std::string>());
    read_required(serviceName);
    read_optional(position);
    read_optional(config);
    read_optional(restartPolicy);
    read_optional(maxRestarts);
    read_optional(restartBackoffSeconds);
    if (j.contains("healthCheck")) { x.healthCheck = j.at("healthCheck").get<ServiceSpec::HealthCheck>(); }
    read_optional(devices);
    read_optional(models);
}

void to_json(nlohmann::json& j, const ServiceSpec& x) {
    output(package);
    j["version"] = x.version.to_string();
    output(serviceName);
    output(position);
    if (!x.config.is_null()) { output(config); }
    output(restartPolicy);
    output(maxRestarts);
    output(restartBackoffSeconds);
    if (x.healthCheck) { j["healthCheck"] = *x.healthCheck; }
    output_unless_empty(devices);
    output_unless_empty(models);
}

void from_json(const nlohmann::json& j, ServiceCR::Metadata& x) {
    read_required(name);
    read_optional(labels);
    read_optional(annotations);
}

void to_json(nlohmann::json& j, const ServiceCR::Metadata& x) {
    output(name);
    output_unless_empty(labels);
    output_unless_empty(annotations);
}

void from_json(const nlohmann::json& j, ServiceCR& x) {
    read_required(kind);
    read_required(metadata);
    if (j.contains("spec")) {
        x.spec = std::make_shared<ServiceSpec>(j.at("spec").get<ServiceSpec>());
    }
    read_optional(id);
    read_optional(state);
    read_optional(restartCount);
}

void to_json(nlohmann::json& j, const ServiceCR& x) {
    output(kind);
    output(metadata);
    if (x.spec) { j["spec"] = *x.spec; }
    if (x.id != uuids::uuid{}) { j["id"] = to_string(x.id); }
    output(state);
    output(restartCount);
}
