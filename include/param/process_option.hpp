// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <nlohmann/json.hpp>
#include <string>
namespace msg_params {
struct ProcessOption {
    std::string path;
    std::vector<std::string> args;
    std::unordered_map<std::string, std::string> envs;
    std::unordered_map<std::string, std::string> labels;
    friend void from_json(const nlohmann::json&, ProcessOption&);
    friend void to_json(nlohmann::json&, const ProcessOption&);
};
} // namespace msg_params
