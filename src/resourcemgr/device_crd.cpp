// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#include "resourcemgr/device_crd.hpp"
#include "util/json_codec_macros.hpp"
#include "util/scope.hpp"
#include <glog/logging.h>

using namespace nlohmann::json_literals;
void from_json(const nlohmann::json& j, DeviceCRD::Metadata& x) {
    read_required(name);
    read_optional_unless_empty(labels);
    read_optional_unless_empty(annotations);
}

void to_json(nlohmann::json& j, const DeviceCRD::Metadata& x) {
    output(name);
    output_unless_empty(labels);
    output_unless_empty(annotations);
}

void from_json(const nlohmann::json& j, DeviceCRD::Access& x) {
    read_required(type);
    read_optional_unless_empty(params);
}

void to_json(nlohmann::json& j, const DeviceCRD::Access& x) {
    output(type);
    output_unless_empty(params);
}

void from_json(const nlohmann::json& j, DeviceCRD::Spec& x) {
    read_optional_unless_empty(access);

    auto jp = "/schema/openAPIV3Schema"_json_pointer;
    if (j.contains(jp)) { j.at(jp).get_to(x.openAPIV3Schema); }
}

void to_json(nlohmann::json& j, const DeviceCRD::Spec& x) {
    output_unless_empty(access);

    if (!x.openAPIV3Schema.empty()) { j["schema"]["openAPIV3Schema"] = x.openAPIV3Schema; }
}

void from_json(const nlohmann::json& j, DeviceCRD& x) {
    scope_fail complain([&j] { LOG(WARNING) << "when parsing device crd:\n" << j.dump(2); });
    j.at("packageName").get_to(x.package);
    x.version = semver::version(j.at("version").get<std::string>());
    read_required(kind);
    read_required(metadata);
    read_optional(id);
    read_optional(description);
    read_required(spec);
}

void to_json(nlohmann::json& j, const DeviceCRD& x) {
    j["packageName"] = x.package;
    j["version"] = x.version.to_string();
    j["kind"] = x.kind;
    j["metadata"] = x.metadata;
    j["id"] = x.id;
    if (x.description.has_value()) { j["description"] = x.description.value(); }
    output(spec);
}
