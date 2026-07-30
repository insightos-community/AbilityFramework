// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#include "resourcemgr/model_mgr.hpp"
#include "databasemgr/database_mgr.hpp"
#include "util/ada_url.hpp"
#include "util/global_vars.hpp"
#include <glog/logging.h>
#include <httplib.h>
#include <string>
using Path = ModelRepoClient ::Path;

namespace {
template <typename... Ts>
std::string strjoin(Ts&&... args) {
    std::ostringstream oss;
    ((oss << args), ...);
    return oss.str();
}
std::string login(
    const std::string& repo_hostname, int repo_port, const ModelRepoClient::UserAuth& user
) {
    httplib::Client cli(repo_hostname, repo_port);
    nlohmann::json payload = user;
    auto post_res = cli.Post("/auth/login", payload.dump(), "application/json");
    if (!post_res) { throw std::runtime_error(strjoin("login failed: ", post_res.error())); }
    if (post_res->status != 200) {
        throw std::runtime_error(
            strjoin("login failed, status: ", post_res->status, ", info: ", post_res->body)
        );
    }
    nlohmann::json j = nlohmann::json::parse(post_res->body);
    return j.at("access_token").get<std::string>();
}
} // namespace

ModelRepoClient::ModelRepoClient(std::string hostname, int port, UserAuth user)
    : user(user)
    , repo_hostname(hostname)
    , repo_port(port) {
    token = login(repo_hostname, repo_port, user);
    LOG(INFO) << "model repo client login complete, token: " << token;
}

int sv_to_int(std::string_view sv) {
    int result;
    auto res = std::from_chars(sv.data(), sv.data() + sv.size(), result);

    if (res.ec == std::errc::invalid_argument || res.ptr != sv.data() + sv.size()) {
        throw std::invalid_argument("String could not be converted to int.");
    }

    return result;
}

ModelRepoClient::ModelRepoClient(std::string server_url, UserAuth user)
    : user(user) {
    auto url = ada::parse(server_url);
    CHECK(url.has_value()) << "parse model repo server_url failed: " << server_url;
    repo_port = sv_to_int(url->get_port());
    repo_hostname = url->get_host();
    token = login(repo_hostname, repo_port, user);
    LOG(INFO) << "model repo client login complete, token: " << token;
}

auto ModelRepoClient::list_model_files(std::string model_id) -> std::vector<FileInfo> {
    httplib::Client cli(repo_hostname, repo_port);
    auto get_res = cli.Get(strjoin("/models/", model_id, "/files"));
    if (!get_res) {
        throw std::runtime_error(strjoin("get model info ", model_id, " failed: ", get_res.error())
        );
    }
    if (get_res->status != 200) {
        throw std::runtime_error(strjoin(
            "get model info ",
            model_id,
            " failed, status: ",
            get_res->status,
            ", info: ",
            get_res->body
        ));
    }
    auto j = nlohmann::json::parse(get_res->body);
    if (j.empty()) { return {}; }
    return j.get<std::vector<FileInfo>>();
}

void download_file(httplib::Client& cli, int64_t file_id, const Path& target_path) {
    using namespace httplib;
    std::ofstream ofs(target_path);
    // set content data receiver, write result directly to file
    auto res = cli.Get(
        strjoin("/files/", file_id, "/download"), httplib::Headers(),
        [&](const Response& response) {
            return true; // return 'false' if you want to cancel the request.
        },
        [&](const char* data, size_t data_length) {
            ofs.write(data, data_length);
            return true; // return 'false' if you want to cancel the request.
        }
    );
    if (!res) { throw std::runtime_error(strjoin("get file ", file_id, " failed: ", res.error())); }
    if (res->status != 200) {
        throw std::runtime_error(
            strjoin("get file ", file_id, " failed, status: ", res->status, ", msg: ", res->body)
        );
    }
}

void ModelRepoClient::download_model_by_id(std::string model_id, Path target_dir) {
    httplib::Client cli(repo_hostname, repo_port);
    cli.set_default_headers({{"Authorization", "Bearer: " + token}});
    auto files = list_model_files(model_id);
    LOG(INFO) << "create dir " << target_dir.string();
    create_directories(target_dir);

    for (const auto& f : files) {
        auto target_path = target_dir / f.filename;
        download_file(cli, f.id, target_path);
    }
}

bool ModelRepoClient::check_server_availability(std::string hostname, int port) try {
    httplib::Client cli(hostname, port);
    auto res = cli.Get("/system/health");
    if (!res) {
        LOG(ERROR) << "check server health failed: " << res.error();
        return false;
    }
    if (res->status != 200) {
        LOG(ERROR) << "check server health failed, status=" << res->status << ", msg=" << res->body;
        return false;
    }
    LOG(INFO) << "server health res: " << res->body;
    return true;

    return true;
}
catch (std::exception& e) {
    LOG(ERROR) << __func__ << "failed: " << e.what();
    return false;
}
ModelInfo ModelRepoClient::get_model_info(const std::string& model_id) {
    httplib::Client cli(repo_hostname, repo_port);
    auto get_res = cli.Get(strjoin("/models/", model_id));
    if (!get_res) { throw std::runtime_error(strjoin(__func__, " failed: ", get_res.error())); }
    if (get_res->status != 200) {
        throw std::runtime_error(
            strjoin(__func__, " failed, status= ", get_res->status, ", body: ", get_res->body)
        );
    }
    return nlohmann::json::parse(get_res->body).get<ModelInfo>();
}

std::optional<ModelRepoClient> make_client_from_config() {
    using global_vars::get_config;
    auto hostname = get_config<std::string>("/model_repo/hostname", "localhost");
    auto port = get_config<int>("/model_repo/port", 8000);
    if (!ModelRepoClient::check_server_availability(hostname, port)) {
        LOG(ERROR) << "error make model repo client, " << hostname << ':' << port
                   << " is not anavailable repo url";
    }
    auto username = get_config<std::string>("/model_repo/user");
    auto password = get_config<std::string>("/model_repo/password");
    return ModelRepoClient(hostname, port, {username, password});
}

constexpr char SQL_CREATE_TABLE_MODEL_INFO[] = R"sql(CREATE TABLE IF NOT EXISTS ModelStorage(
id TEXT PRIMARY KEY,
storage_dir TEXT NOT NULL,
fullname TEXT NOT NULL  
);)sql";

ModelManager::ModelManager() {
    if (global_vars::has_config("/model_repo")) {
        LOG(INFO) << "init model repo client from config";
        try {
            client = make_client_from_config();
        }
        catch (std::exception& e) {
            LOG(WARNING) << "login failed: " << e.what() << ", using local mode";
        }
    }
    else { LOG(INFO) << "model repo has no client, using local mode"; }
    database_mgr::exec(SQL_CREATE_TABLE_MODEL_INFO);
}

std::filesystem::path ModelManager::model_home_dir() {
    return global_vars::home_path() / "data" / "model";
}

std::vector<std::string> ModelManager::list_model_names() const {
    constexpr char sql[] = R"sql(SELECT fullname FROM ModelBasic;)sql";
    auto stmt = database_mgr::statement(sql);
    std::vector<std::string> res;
    while (stmt.executeStep()) {
        std::string fullname = stmt.getColumn(0);
        res.push_back(fullname);
    }
    return res;
}

auto ModelManager::get_model_info(const std::string& id) const -> std::optional<ModelStorageInfo> {
    auto check_add_prefix = [](std::filesystem::path p) {
        if (p.is_absolute()) { return std::move(p); }
        return model_home_dir() / p;
    };
    constexpr char sql[] = R"sql(SELECT fullname, storage_dir FROM ModelStorage WHERE id = ?;)sql";
    auto stmt = database_mgr::statement(sql);
    stmt.bind(1, id);
    while (stmt.executeStep()) {
        std::string fullname = stmt.getColumn(0);
        std::string storage_dir = stmt.getColumn(1);
        auto storage_path = check_add_prefix(storage_dir);
        return ModelStorageInfo{.id = id, .fullname = fullname, .path = storage_path.string()};
    }
    return {};
}

namespace {
template <typename T>
std::string make_storage_path_name(const T& id, const std::string& fullname) {
    return strjoin(id, "-", fullname);
}
} // namespace

void ModelManager::download_model_by_id(const std::string& model_id) {
    auto local_model = get_model_info(model_id);
    if (local_model) {
        LOG(INFO) << "model " << model_id << "is locally available, stop downloading";
        return;
    }
    if (!is_online()) {
        LOG(ERROR) << "can't download model in local mode";
        throw std::runtime_error("can't download model in local mode");
    }
    auto info = client->get_model_info(model_id);
    auto storage_path_name = make_storage_path_name(info.id.value(), info.fullname);
    auto dest_dir = model_home_dir() / storage_path_name;
    client->download_model_by_id(model_id, dest_dir);

    constexpr char SQL[]
        = R"sql(INSERT INTO ModelStorage (id, storage_dir, fullname) VALUES (?, ?, ?))sql";
    auto stmt = database_mgr::statement(SQL);
    stmt.bind(1, model_id);
    stmt.bind(2, storage_path_name);
    stmt.bind(3, info.fullname);
    stmt.exec();
}
