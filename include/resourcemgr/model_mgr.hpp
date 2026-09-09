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
#include "resourcemgr/model_info.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

class ModelRepoClient {
public:
    using Path = std::filesystem::path;
    struct UserAuth {
        std::string username;
        std::string password;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(UserAuth, username, password);
    };
    struct FileInfo {
        int id;
        std::string filename;
        /// 字节
        int64_t size;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(FileInfo, id, filename, size);
    };
    /// 假如有文件f1.pt,f2.json,f3.json, 那么先创建target_dir,然后将文件依次置于其中
    void download_model_by_id(std::string model_id, Path target_dir);
    std::vector<FileInfo> list_model_files(std::string model_id);
    ModelInfo get_model_info(const std::string& model_id);

    ModelRepoClient(std::string hostname, int port, UserAuth user);
    ModelRepoClient(std::string server_url, UserAuth user);
    static bool check_server_availability(std::string hostname, int port);

private:
    UserAuth user;
    std::string token;
    std::string repo_hostname;
    int repo_port;
};

// 管理框架本地安装的模型,其元信息和文件存储
// 模型存储在 FWK_HOME/data/model/{模型id}, 每项一个模型文件
class ModelManager {
public:
    struct ModelStorageInfo {
        std::string id;
        std::string fullname;
        std::string path;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(ModelStorageInfo, id, fullname, path);
    };

    // 列举已安装的模型及id
    std::vector<std::string> list_model_names() const;
    ModelManager();

    // manager 是否处于在线模式, 如果是, 那么可以使用client
    bool is_online() const { return client.has_value(); }
    static std::filesystem::path model_home_dir();
    std::optional<ModelStorageInfo> get_model_info(const std::string& id) const;
    void download_model_by_id(const std::string& model_id);

private:
    std::optional<ModelRepoClient> client;
};
