// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

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
        /// bytes
        int64_t size;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(FileInfo, id, filename, size);
    };
    /// e.g. if there are files f1.pt, f2.json, f3.json, then create target_dir first and place the files in it
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

// manage locally installed models, their metadata and file storage
// models are stored in FWK_HOME/data/model/{model_id}, one file per model
class ModelManager {
public:
    struct ModelStorageInfo {
        std::string id;
        std::string fullname;
        std::string path;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(ModelStorageInfo, id, fullname, path);
    };

    // list installed models and ids
    std::vector<std::string> list_model_names() const;
    ModelManager();

    // manager whether in online mode, if it is, then can useclient
    bool is_online() const { return client.has_value(); }
    static std::filesystem::path model_home_dir();
    std::optional<ModelStorageInfo> get_model_info(const std::string& id) const;
    void download_model_by_id(const std::string& model_id);

private:
    std::optional<ModelRepoClient> client;
};
