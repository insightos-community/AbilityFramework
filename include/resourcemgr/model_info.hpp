// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

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
