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

#include "resourcemgr/model_info.hpp"
#include "util/json_codec_macros.hpp"
#include "util/scope.hpp"
#include <glog/logging.h>
bool ModelInfoMetric::empty() const {
    return !parameters.has_value() && !quantization.has_value() && !gpu_memory_gb.has_value()
        && !storage_gb.has_value();
}
void from_json(const nlohmann::json& j, ModelInfoMetric& x) {
    scope_fail print_json([&j]() {
        LOG(WARNING) << "when converting json to ModelInfoMetric:\n" << j.dump(2);
    });
    if (j.empty()) { return; }
    read_optional_unless_empty(parameters);
    read_optional_unless_empty(quantization);
    read_optional_unless_empty(gpu_memory_gb);
    read_optional_unless_empty(storage_gb);
}
void to_json(nlohmann::json& j, const ModelInfoMetric& x) {
    output_if_has_value(parameters);
    output_if_has_value(quantization);
    output_if_has_value(gpu_memory_gb);
    output_if_has_value(storage_gb);
}
void from_json(const nlohmann::json& j, ModelInfo& x) {
    read_optional(id);
    read_required(name);
    read_required(fullname);
    read_required(architecture);
    read_required(framework);
    read_required(version);
    read_optional_unless_empty(description);
    read_optional_unless_empty(tags);
    read_optional(metrics);
}
void to_json(nlohmann::json& j, const ModelInfo& x) {
    output_if_has_value(id);
    output(name);
    output(fullname);
    output(architecture);
    output(framework);
    output(version);
    output_unless_empty(description);
    output_unless_empty(tags);
    output(metrics);
}
