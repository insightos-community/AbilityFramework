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
#include "make_uuid.hpp"
#include "util/yaml_to_json.hpp"
#include <filesystem>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <vector>
#include <yaml-cpp/yaml.h>

namespace global_vars {

class ConfigException : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// 获取配置值，若路径不存在或类型不匹配，则返回默认值
template <typename T>
T get_config(const std::string& path, const T& default_value);

/// 获取配置值，若出错（路径不存在、类型错误、读取失败）则抛出 ConfigException
template <typename T>
T get_config(const std::string& path);

using std::filesystem::path;
/// 初始化全局变量
/// 在此之前任何对全局变量的使用均非法
/// @throws std::filesystem::filesystem_error
///   如果ABILITY_FRAMEWORK_HOME没有注明,并且 fs::current_directory也出错
void init();

void init(const std::string& _config_file);
///
/// 能力框架主目录所在的路径
path home_path();
///
/// 能力框架日志所在的路径
path log_path();
///
/// 配置文件所在的路径
path config_path();
///
/// 能力框架包所在的目录
path packages_path();
///
/// 框架id
uuids::uuid framework_id();
///
/// 获取指定路径的节点
YAML::Node get_node_by_path(const std::string& path);

template <typename T>
concept FromYaml = requires(YAML::Node n, T& t) {
    YAML::convert<T>::decode(n, t);
    n.as<T>();
};

template <typename T>
concept FromJson = requires(nlohmann::json& j) { j.get<T>(); };

template <typename T>
T get_config(const std::string& path, const T& default_value) try {
    auto node = get_node_by_path(path);
    if constexpr (FromYaml<T>) { return node.as<T>(); }
    else if constexpr (FromJson<T>) {
        auto j = yaml_to_json(node);
        return j.get<T>();
    }
    else { static_assert(FromYaml<T> || FromJson<T>, "need type be deserializable"); }
}
catch (...) {
    return default_value;
}

template <typename T>
T get_config(const std::string& path) try {
    auto node = get_node_by_path(path);
    if constexpr (FromYaml<T>) { return node.as<T>(); }
    else if constexpr (FromJson<T>) {
        auto j = yaml_to_json(node);
        return j.get<T>();
    }
    else { static_assert(FromYaml<T> || FromJson<T>, "need type be deserializable"); }
}
catch (const YAML::BadConversion&) {
    throw ConfigException("Type mismatch at path: " + path);
}
catch (const YAML::Exception& e) {
    throw ConfigException("YAML error at path '" + path + "': " + std::string(e.what()));
}

bool has_config(const std::string& path);
}; // namespace global_vars
