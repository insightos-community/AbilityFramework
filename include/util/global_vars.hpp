// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

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

/// get config value; returns default if path not found or type mismatch
template <typename T>
T get_config(const std::string& path, const T& default_value);

/// get config value; throws ConfigException on error (path not found, type mismatch, read failure)
template <typename T>
T get_config(const std::string& path);

using std::filesystem::path;
/// initialize global variables
/// before this, any use of global variables is invalid
/// @throws std::filesystem::filesystem_error
/// if ABILITY_FRAMEWORK_HOME is not specified, and fs::current_directory also errors
void init();

void init(const std::string& _config_file);
///
/// path of the ability framework home directory
path home_path();
///
/// path of the ability framework log directory
path log_path();
///
/// path of the config file
path config_path();
///
/// directory of the ability framework packages
path packages_path();
///
/// framework id
uuids::uuid framework_id();
///
/// get the node at the specified path
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
