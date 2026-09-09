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

#include "nlohmann/json.hpp"
#include <optional>

struct ModelInfoMetric {
    std::optional<int> parameters;
    std::optional<std::string> quantization;
    std::optional<float> gpu_memory_gb;
    std::optional<float> storage_gb;
    bool empty() const;
    friend void from_json(const nlohmann::json& j, ModelInfoMetric& x);
    friend void to_json(nlohmann::json& j, const ModelInfoMetric& x);
};

struct ModelInfo {
    std::optional<int> id;
    std::string name;
    std::string fullname;
    std::string architecture;
    std::string framework;
    std::string version;
    std::string description;
    nlohmann::json tags;
    ModelInfoMetric metrics;
    friend void from_json(const nlohmann::json& j, ModelInfo& x);
    friend void to_json(nlohmann::json& j, const ModelInfo& x);
};
