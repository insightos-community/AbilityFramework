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

#include <glog/logging.h>
#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>

namespace {
using nlohmann::json;

nlohmann::json convert_scalar(const YAML::Node& node) {
    if (node.as<std::string>() == "true" || node.as<std::string>() == "false") {
        return node.as<bool>();
    }
    // 尝试转换为整数
    try {
        int64_t intValue = node.as<int64_t>();
        return intValue;
    }
    catch (...) {
    }
    // 尝试转换为浮点数
    try {
        double doubleValue = node.as<double>();
        return doubleValue;
    }
    catch (...) {
    }
    // 默认为字符串
    return node.Scalar();
}
} // namespace

nlohmann::json yaml_to_json(const YAML::Node& ynode) {
    if (!ynode.IsDefined() || ynode.IsNull()) { return {}; }
    if (ynode.IsScalar()) { return convert_scalar(ynode); }

    if (ynode.IsSequence()) {
        nlohmann::json res;
        for (size_t i = 0; i < ynode.size(); ++i) {
            res.push_back(yaml_to_json(ynode[i]));
        }
        return res;
    }
    if (ynode.IsMap()) {
        nlohmann::json res;

        for (auto it = ynode.begin(); it != ynode.end(); ++it) {
            res[it->first.Scalar()] = yaml_to_json(it->second);
        }
        return res;
    }
    throw std::logic_error("a yaml is nether scalar, array, nor map");
}

// json转换为yaml
YAML::Node json_to_yaml(const nlohmann::json& j) {
    if (j.is_object()) {
        YAML::Node node(YAML::NodeType::Map);
        for (auto it = j.begin(); it != j.end(); ++it) {
            node[it.key()] = json_to_yaml(it.value());
        }
        return node;
    }
    else if (j.is_array()) {
        YAML::Node node(YAML::NodeType::Sequence);
        for (const auto& el : j) {
            node.push_back(json_to_yaml(el));
        }
        return node;
    }
    else if (j.is_string()) { return YAML::Node(j.get<std::string>()); }
    else if (j.is_boolean()) { return YAML::Node(j.get<bool>()); }
    else if (j.is_number_integer()) { return YAML::Node(j.get<int>()); }
    else if (j.is_number_unsigned()) { return YAML::Node(j.get<unsigned>()); }
    else if (j.is_number_float()) { return YAML::Node(j.get<double>()); }
    else if (j.is_null()) { return YAML::Node(); }
    return YAML::Node();
}