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

#include "util/global_vars.hpp"
#include "prelude.hpp"
#include <filesystem>
#include <fstream>
#include <glog/logging.h>
#include <iostream>
#include <mutex>
#include <shared_mutex>

namespace global_vars {

std::string config_file = "";

namespace {
std::filesystem::path read_workspace_path() {
    const char* home_path = getenv("ABILITY_FRAMEWORK_HOME");
    if (!home_path) {
        // doesn't provide home path, use current directory
        try {
            return std::filesystem::current_path();
        }
        catch (std::filesystem::filesystem_error& err) {
            std::cerr << "failed to get framework home path: " << err.what();
            throw;
        }
    }
    return home_path;
}

bool global_variables_is_initialized = false;
path HomePath;

static YAML::Node cached_config = YAML::Node();
std::size_t cached_hash = 0;
std::mutex config_mutex;

std::size_t hash_config_content(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return 0;

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return std::hash<std::string>{}(content);
}

bool is_path(const std::string& str) {
    if (str.empty()) return false;
    // 1. 绝对路径（Linux: /abc/...）
    if (str.front() == '/') return true;
    // 2. 相对路径（./ 或 ../）
    if (str.rfind("./", 0) == 0 || str.rfind("../", 0) == 0) return true;
    // 3. 包含路径分隔符
    if (str.find('/') != std::string::npos || str.find('\\') != std::string::npos) return true;
    return false;
}

} // namespace

YAML::Node load_config() {
    path config_path;
    if (config_file.empty()) { config_path = HomePath / "config.yaml"; }
    else {
        if (is_path(config_file))
            config_path = config_file;
        else
            config_path = HomePath / config_file;
    }
    if (!exists(config_path)) {
        LOG(ERROR) << "config file doesn't exist at " << config_path;
        return YAML::Node();
    }
    if (!is_regular_file(config_path)) {
        LOG(ERROR) << "config file " << config_path << " is not regular file";
        return YAML::Node();
    }
    std::unique_lock lock(config_mutex);
    // 未更改时直接返回缓存
    std::size_t current_hash = hash_config_content(config_path);
    if (cached_hash == current_hash) { return YAML::Clone(cached_config); }
    try {
        YAML::Node config = YAML::LoadFile(config_path.string());
        cached_config = YAML::Clone(config);
        cached_hash = current_hash;
        return config;
    }
    catch (const YAML::Exception& e) {
        LOG(ERROR) << "Failed to load config file " << config_path << ": " << e.what();
        return YAML::Node();
    }
}

YAML::Node get_node_by_path(const std::string& path) {
    const char delimiter = '/';
    YAML::Node current = load_config();
    if (!current) throw ConfigException("load config failed");
    size_t pos = 0, next;
    while ((next = path.find(delimiter, pos)) != std::string::npos) {
        std::string key = path.substr(pos, next - pos);
        if (!key.empty()) current = current[key];
        if (!current) throw ConfigException("Path not found: " + path);
        pos = next + 1;
    }
    std::string final_key = path.substr(pos);
    if (!final_key.empty()) current = current[final_key];
    if (!current) throw ConfigException("Path not found: " + path);
    return current;
}

void init() {
    HomePath = read_workspace_path();
    global_variables_is_initialized = true;
}

void init(const std::string& _config_file) {
    HomePath = read_workspace_path();
    config_file = _config_file;
    global_variables_is_initialized = true;
}
path home_path() {
    CHECK(global_variables_is_initialized);
    return HomePath;
}
path log_path() {
    CHECK(global_variables_is_initialized);
    std::string log_dir = get_config<std::string>("/log/glog/log_dir", "log");
    return HomePath / log_dir;
}
path config_path() {
    if (config_file.empty()) { return HomePath / "config.yaml"; }
    else {
        if (is_path(config_file))
            return config_file;
        else
            return HomePath / config_file;
    }
}
path packages_path() {
    CHECK(global_variables_is_initialized);
    return HomePath / "packages";
}
uuids::uuid framework_id() {
    CHECK(global_variables_is_initialized);
    auto random_name = strjoin("fwk-tmp-", rand() % 1000);
    std::string framework_name = get_config<std::string>("/framework_name", random_name);
    return make_uuid(framework_name);
}
bool has_config(const std::string& path) try {
    YAML::Node node = get_node_by_path(path);
    return !node.IsNull();
}
catch (...) {
    return false;
}
} // namespace global_vars
