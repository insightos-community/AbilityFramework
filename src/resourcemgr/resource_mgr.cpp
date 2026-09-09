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

#include "resourcemgr/resource_mgr.hpp"
#include "databasemgr/database_mgr.hpp"
#include "json_schema_utils.hpp"
#include "lifecyclemgr/lifecycle_request.hpp"
#include "messagebus/message_client.hpp"
#include "prelude.hpp"
#include "resourcemgr/ability_manifest.hpp"
#include "task_factories/install_model.hpp"
#include "taskmgr/common_tasks.hpp"
#include "util/cpptrace_debug.hpp"
#include "util/expected.hpp"
#include "util/global_vars.hpp"
#include "util/parse_addr.hpp"
#include "util/scope.hpp"
#include "util/yaml_to_json.hpp"
#include <ctime>
#include <fstream>
#include <glog/logging.h>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <set>
#include <tuple>
#include <yaml-cpp/yaml.h>

namespace {

bool is_local_cr(const std::string& ip) {
    auto ip_list = get_local_ip_addresses();
    return ip == "localhost" || std::find(ip_list.begin(), ip_list.end(), ip) != ip_list.end();
}

} // namespace

using namespace std::views;
using Path = std::filesystem::path;
using std::filesystem::directory_iterator;

namespace {
bool is_semver(const std::string& str) {
    try {
        semver::version v(str);
        return true;
    }
    catch (...) {
        LOG(WARNING) << str << "doesn't look like a semver, ignore it";
        return false;
    }
}

bool is_package(const Path& p) {
    return exists(p / "package.yaml") && exists(p / "ability.manifest.yaml");
}
} // namespace

namespace {
expected<std::string, std::string> get_kind(const YAML::Node& crd) {
    if (!crd["kind"]) { return unexpected("needs /kind"); }
    if (!crd["kind"].IsScalar()) { return unexpected("needs /kind be scalar"); }
    return crd["kind"].as<std::string>();
}

expected<std::string, std::string> get_name(const YAML::Node& crd) {
    if (!crd["metadata"]["name"]) { return unexpected("needs /metadata/name"); }
    if (!crd["metadata"]["name"].IsScalar()) {
        return unexpected("needs /metadata/name be string");
    }
    return crd["metadata"]["name"].as<std::string>();
}

expected<std::string, std::string> get_spec_package(const YAML::Node& cr) {
    if (!cr["spec"]["package"]) { return unexpected("needs /spec/package"); }
    if (!cr["spec"]["package"].IsScalar()) { return unexpected("needs /spec/package be scalar"); }
    return cr["spec"]["package"].as<std::string>();
}

expected<std::string, std::string> get_crd_packageName(const YAML::Node& crd) {
    if (!crd["packageName"]) { return unexpected("needs /packageName"); }
    if (!crd["packageName"].IsScalar()) { return unexpected("needs /packageName be string"); }
    return crd["packageName"].as<std::string>();
}

bool is_ability(std::string_view kind) {
    return kind.find("Ability") != kind.npos;
}

expected<semver::version, std::string> read_version(const YAML::Node& y) {
    if (!y["version"] || (!y["version"].IsScalar())) { return unexpected{"need /version"}; }
    auto version_str = y["version"].as<std::string>();
    try {
        return semver::version{version_str};
    }
    catch (std::exception& e) {
        return make_unexpected("error parsing version: ", e.what());
    }
}

expected<semver::version, std::string> get_spec_version(const YAML::Node& cr) {
    if (!cr["spec"]["version"] || (!cr["spec"]["version"].IsScalar())) {
        return unexpected{"need /spec/version"};
    }
    auto version_str = cr["spec"]["version"].as<std::string>();
    try {
        return semver::version{version_str};
    }
    catch (std::exception& e) {
        return make_unexpected("error parsing version: ", e.what());
    }
}

expected<std::string, std::string> read_name(const YAML::Node& y) {
    if (!y["name"] || (!y["name"].IsScalar())) { return unexpected{"need /name"}; }
    auto name_str = y["name"].as<std::string>();
    return name_str;
}

bool name_is_semver(const Path& p) {
    return is_semver(p.filename());
}

// 发送post请求，成功则返回res->body，j["result"] == "succcess"表示目标机器执行动作成功
std::optional<std::string> send_post_request(
    const std::string& url, const std::string& endpoint, const nlohmann::json& j
) {
    try {
        const std::string url_ = "http://" + url + ":8080";
        httplib::Client cli(url_);
        cli.set_connection_timeout(5);
        std::string body = j.dump();
        auto res = cli.Post(endpoint, body, "application/json");
        if (!res) {
            LOG(ERROR) << "send_post_request: " << url_ << endpoint << " error: " << res.error();
            return std::nullopt;
        }
        if (res->status == 200) { return res->body; }
        LOG(ERROR) << "send_post_request: " << url_ << endpoint << " error: " << res->status;
        return std::nullopt;
    }
    catch (const std::exception& e) {
        std::cerr << "Exception occurred: " << e.what() << std::endl;
        return std::nullopt;
    }
}
constexpr char SQL_CREATE_ABILITY_CR_BASIC[] = R"sql(
CREATE TABLE IF NOT EXISTS AbilityCRBasic(
  instance_id TEXT PRIMARY KEY, -- 实例id(作为主键)
  ability_name TEXT NOT NULL, -- 能力类名称, 对应spec/abilityName
  ability_version TEXT NOT NULL, -- 能力版本
  instance_name TEXT NOT NULL, -- 实例名称, 对应metadata/name
  insert_time INTEGER NOT NULL, -- 创建时间, 最近更新时间, unix秒格式
  update_time INTEGER NOT NULL, -- 最近更新时间, unix秒格式
  autostart INTEGER, -- 0 或不存在表示假, 1表示真 (仅框架启动时自动拉起一次)
  auto_started INTEGER DEFAULT 0, -- 是否已经执行过 autoStart (0=未执行, 1=已执行)
  keep_alive INTEGER DEFAULT 0, -- 是否保活 (0=不保活, 1=退出后自动重启)
  singleton INTEGER DEFAULT 1, -- 是否单例 (1=单例/默认, 0=允许多实例)
  cr_detail TEXT NOT NULL, -- 原样存储json格式的CR
  cr_filepath TEXT -- 如果源于某一个文件, 那么记录该文件的路径, 否则为空
);
)sql";

constexpr char SQL_CREATE_ABILITY_CR_FILE_INFO[] = R"sql(
CREATE TABLE IF NOT EXISTS AbilityCRFileInfo(
  instance_id TEXT PRIMARY KEY,    -- cr 实例id
  cr_file_path TEXT NOT NULL,   -- 如果CR来源于文件, 则记录文件路径
  cr_file_digest TEXT NOT NULL, -- cr文件内容 sha256
  cr_file_detail BLOB NOT NULL, -- 具体的cr文件
  cr_file_extenstion TEXT NOT NULL -- cr文件扩展名, 目前只支持 "yaml"
);
)sql";

constexpr char SQL_CREATE_ABILITY_CR_LABEL[] = R"sql(
CREATE TABLE IF NOT EXISTS AbilityCRLabel(
  instance_id TEXT NOT NULL,
  label_key TEXT NOT NULL,
  label_value TEXT,
  PRIMARY KEY (instance_id, label_key)
);
)sql";

constexpr char SQL_CREATE_ABILITY_CR_ANNOTATION[] = R"sql(
CREATE TABLE IF NOT EXISTS AbilityCRAnnotation(
  instance_id TEXT NOT NULL,
  annotation_key TEXT NOT NULL,
  annotation_value TEXT,
  PRIMARY KEY (instance_id, annotation_key)
);
)sql";

constexpr char SQL_CREATE_DEVICE_CR_BASIC[] = R"sql(
CREATE TABLE IF NOT EXISTS DeviceCRBasic(
  instance_id TEXT PRIMARY KEY, -- 实例id(作为主键)
  device_name TEXT NOT NULL, -- 能力类名称, 对应spec/abilityName
  device_version TEXT NOT NULL, -- 能力版本
  instance_name TEXT NOT NULL, -- 实例名称, 对应metadata/name
  insert_time INTEGER NOT NULL, -- 创建时间, 最近更新时间, unix秒格式
  update_time INTEGER NOT NULL, -- 最近更新时间, unix秒格式
  cr_detail TEXT NOT NULL, -- 原样存储json格式的CR
  cr_filepath TEXT -- 如果源于某一个文件, 那么记录该文件的路径, 否则为空
);
)sql";

constexpr char SQL_CREATE_DEVICE_CR_FILE_INFO[] = R"sql(
CREATE TABLE IF NOT EXISTS DeviceCRFileInfo(
  instance_id TEXT PRIMARY KEY,    -- cr 实例id
  cr_file_path TEXT NOT NULL,   -- 如果CR来源于文件, 则记录文件路径
  cr_file_digest TEXT NOT NULL, -- cr文件内容 sha256
  cr_file_detail BLOB NOT NULL, -- 具体的cr文件
  cr_file_extenstion TEXT NOT NULL -- cr文件扩展名, 目前只支持 "yaml"
);
)sql";

constexpr char SQL_CREATE_DEVICE_CR_LABEL[] = R"sql(
CREATE TABLE IF NOT EXISTS DeviceCRLabel(
  instance_id TEXT NOT NULL,
  label_key TEXT NOT NULL,
  label_value TEXT,
  PRIMARY KEY (instance_id, label_key)
);
)sql";

constexpr char SQL_CREATE_DEVICE_CR_ANNOTATION[] = R"sql(
CREATE TABLE IF NOT EXISTS DeviceCRAnnotation(
  instance_id TEXT NOT NULL,
  annotation_key TEXT NOT NULL,
  annotation_value TEXT,
  PRIMARY KEY (instance_id, annotation_key)
);
)sql";

constexpr char SQL_CREATE_ABILITY_CRD_BASIC[] = R"sql(
CREATE TABLE IF NOT EXISTS AbilityCRDBasic(
  ability_name TEXT NOT NULL, -- 能力类名称, 对应spec/abilityName
  ability_version TEXT NOT NULL, -- 能力版本
  crd_detail TEXT NOT NULL, -- 原样存储json格式存储的CRD
  PRIMARY KEY (ability_name, ability_version)
);
)sql";

constexpr char SQL_CREATE_DEVICE_CRD_BASIC[] = R"sql(
CREATE TABLE IF NOT EXISTS DeviceCRDBasic(
  device_name TEXT NOT NULL, -- 能力类名称, 对应spec/abilityName
  device_version TEXT NOT NULL, -- 能力版本
  crd_detail TEXT NOT NULL, -- 原样存储json格式存储的CRD
  PRIMARY KEY (device_name, device_version)
);
)sql";

constexpr char SQL_CREATE_CR_OWNERSHIP[] = R"sql(
CREATE TABLE IF NOT EXISTS CROwnership(
  parent_id TEXT NOT NULL, -- 依赖者id
  parent_kind TEXT NOT NULL, -- "ability" 或 "device"
  parent_address TEXT NOT NULL, -- 依赖者的ip 地址
  child_id TEXT NOT NULL, -- 被依赖者id
  child_kind TEXT NOT NULL, -- 被依赖者类型, "ability" 或 "device"
  is_shared INTEGER NOT NULL, -- 0 表示共享, 1 表示独占
  PRIMARY KEY (parent_id, parent_kind, child_id, child_kind)
);
)sql";

// AbilityInstance: runtime row, one per running/starting instance.
// 与 AbilityCRBasic(模板)是 N:1 关系，通过 cr_id 关联。
// 实例退出后立即删除该行（无论 singleton/replica）。
// spec_snapshot 记录启动瞬间的 CR JSON，保证启动后即便模板被修改，
// 运行时的能力仍然按原 spec 工作。
constexpr char SQL_CREATE_ABILITY_INSTANCE[] = R"sql(
CREATE TABLE IF NOT EXISTS AbilityInstance(
  instance_id TEXT PRIMARY KEY,       -- 运行时实例 UUID（与 cr_id 不同）
  cr_id TEXT,                         -- 所属模板 (AbilityCRBasic.instance_id)
  cr_name TEXT NOT NULL,              -- 对应的 CR metadata.name
  instance_name TEXT NOT NULL,        -- 实例显示名（cr_name-<id 前 8 位>）
  ability_name TEXT NOT NULL,         -- 能力类名称
  ability_version TEXT NOT NULL,      -- 能力版本
  pid INTEGER DEFAULT 0,              -- 进程 PID（暂未使用）
  state TEXT DEFAULT 'Inactive',      -- 生命周期状态
  start_time INTEGER,                 -- 启动时间 (unix 秒)
  stop_time INTEGER,                  -- 停止时间
  spec_snapshot TEXT,                 -- 启动瞬间的 AbilityCR JSON 快照
  detail TEXT                         -- 额外信息 JSON（IPCPort/abilityPort 等）
);
)sql";

constexpr char SQL_CREATE_SERVICE_CR_BASIC[] = R"sql(
CREATE TABLE IF NOT EXISTS ServiceCRBasic(
  instance_id TEXT PRIMARY KEY,
  service_name TEXT NOT NULL,
  service_version TEXT NOT NULL,
  instance_name TEXT NOT NULL,
  insert_time INTEGER NOT NULL,
  update_time INTEGER NOT NULL,
  state TEXT NOT NULL DEFAULT 'Stopped',
  restart_count INTEGER NOT NULL DEFAULT 0,
  cr_detail TEXT NOT NULL
);
)sql";

void db_create_cr_crd_tables() {
    database_mgr::exec(SQL_CREATE_ABILITY_CR_BASIC);
    database_mgr::exec(SQL_CREATE_ABILITY_CR_FILE_INFO);
    database_mgr::exec(SQL_CREATE_ABILITY_CR_LABEL);
    database_mgr::exec(SQL_CREATE_ABILITY_CR_ANNOTATION);
    database_mgr::exec(SQL_CREATE_DEVICE_CR_BASIC);
    database_mgr::exec(SQL_CREATE_DEVICE_CR_FILE_INFO);
    database_mgr::exec(SQL_CREATE_DEVICE_CR_LABEL);
    database_mgr::exec(SQL_CREATE_DEVICE_CR_ANNOTATION);
    database_mgr::exec(SQL_CREATE_ABILITY_CRD_BASIC);
    database_mgr::exec(SQL_CREATE_DEVICE_CRD_BASIC);
    database_mgr::exec(SQL_CREATE_CR_OWNERSHIP);
    // AbilityInstance 是纯运行时的瞬时表，schema 又升级过（新增了 cr_id/spec_snapshot 列），
    // 直接重建即可，不需要迁移旧数据。
    database_mgr::exec("DROP TABLE IF EXISTS AbilityInstance");
    database_mgr::exec(SQL_CREATE_ABILITY_INSTANCE);
    database_mgr::exec(SQL_CREATE_SERVICE_CR_BASIC);
}
} // namespace

ResourceManager::ResourceManager()
    : framework_home{global_vars::home_path()}
    , framework_id{global_vars::framework_id()}
    , model_mgr(std::make_shared<ModelManager>()) {
    db_create_cr_crd_tables();
}

namespace {
std::optional<AbilityPackage> read_package(const Path& pkg_dir) {
    LOG(INFO) << "read package from " << pkg_dir;
    AbilityPackage pkg;
    auto pkg_desc = read_yaml_from_path(pkg_dir / "package.yaml");
    if (!pkg_desc) {
        LOG(WARNING) << pkg_dir << "doesn't have a package.yaml";
        return {};
    }
    auto name = read_name(*pkg_desc);
    if (!name) {
        LOG(ERROR) << "read name from " << pkg_dir << "failed:" << name.error();
        return {};
    }
    pkg.name = name.value();
    auto version = read_version(*pkg_desc);
    if (!version) {
        LOG(ERROR) << "read version from " << pkg_dir << "failed:" << version.error();
        return {};
    }
    pkg.path = pkg_dir;
    pkg.version = version.value();

    // 读取 ability.manifest.yaml
    auto manifest_yaml = read_yaml_from_path(pkg_dir / "ability.manifest.yaml");
    if (!manifest_yaml) {
        LOG(ERROR) << "ability.manifest.yaml in package " << pkg_dir << " is unreadable";
        return {};
    }
    nlohmann::json manifest_json = yaml_to_json(*manifest_yaml);
    try {
        AbilityManifest manifest = manifest_json;
        pkg.manifests.emplace(manifest.abilityName, manifest);
        // 从 manifest 合成 AbilityCRD 供下游模块使用
        AbilityCRD crd;
        crd.packageName = pkg.name;
        crd.version = pkg.version;
        crd.kind = manifest.kind;
        crd.metadata.name = manifest.abilityName;
        crd.spec.provides = manifest.provides;
        crd.spec.types = manifest.types;
        crd.spec.rpcMethods = manifest.rpcMethods;
        crd.spec.tasks = manifest.tasks;
        crd.spec.config = manifest.config;
        crd.spec.debugOption = manifest.debugOption;
        crd.spec.schema.constants = manifest.schema.constants;
        crd.spec.schema.openAPIV3Schema = manifest.schema.openAPIV3Schema;
        crd.depends = manifest.depends;
        crd.id = make_uuid(pkg.name, to_string(pkg.version), manifest.abilityName, manifest.kind);
        auto [it, emplaced] = pkg.abilities.emplace(crd.id, crd);
        if (emplaced) { VLOG(1) << "add ability resource " << manifest.abilityName; }
    }
    catch (std::exception& e) {
        LOG(ERROR) << "parse ability.manifest.yaml in " << pkg_dir << " failed: " << e.what();
        return {};
    }
    return pkg;
}

nlohmann::json extract_basic(const AbilityCR& cr) {
    nlohmann::json res = cr;
    res.erase("runInfo");
    res.erase("sharers");
    res.erase("owner");
    return res;
}

} // namespace
// 注意,在g++10,directory_iterator 还不能直接进行range操作
// 为了兼容,先一步一步来
generator<AbilityPackage> recursively_search_packages(const std::filesystem::path& start_path) {
    for (const auto& pkg_name_entry : directory_iterator(start_path)) {
        if (!is_directory(pkg_name_entry)) { continue; }
        for (const auto& v_entry : directory_iterator(pkg_name_entry)) {
            if (!is_directory(v_entry)) { continue; }
            if (!name_is_semver(v_entry)) { continue; }
            if (!is_package(v_entry)) {
                LOG(INFO) << v_entry << " does not seem like a package, skip it";
                continue;
            }
            auto pkg = read_package(v_entry);
            if (pkg.has_value()) { co_yield *pkg; }
        }
    }
}

#define FWK_DEFINE_DB_WRITE_LABELS(Table_Name, Item_Name)                                 \
    void db_write_##Item_Name##_labels(                                                   \
        const std::string& instance_id, const std::string& key, const std::string& value  \
    ) try {                                                                               \
        constexpr char SQL[] = "REPLACE INTO " Table_Name                                 \
                               " (instance_id, label_key, label_value) VALUES  (?,?,?);"; \
        auto stmt = database_mgr::statement(SQL);                                         \
        stmt.bind(1, instance_id);                                                        \
        stmt.bind(2, key);                                                                \
        stmt.bind(3, value);                                                              \
        stmt.exec();                                                                      \
    }                                                                                     \
    catch (std::exception & e) {                                                          \
        LOG(ERROR) << __func__ << " failed: " << e.what();                                \
        throw;                                                                            \
    }

FWK_DEFINE_DB_WRITE_LABELS("AbilityCRLabel", ability_cr)
FWK_DEFINE_DB_WRITE_LABELS("AbilityCRDLabel", ability_crd)
FWK_DEFINE_DB_WRITE_LABELS("DeviceCRLabel", device_cr)
FWK_DEFINE_DB_WRITE_LABELS("DeviceCRDLabel", device_crd)

#define FWK_DEFINE_DB_WRITE_ANNOTATIONS(Table_Name, Item_Name)                                     \
    void db_write_##Item_Name##_annotations(                                                       \
        const std::string& instance_id, const std::string& key, const std::string& value           \
    ) try {                                                                                        \
        constexpr char SQL[] = "REPLACE INTO " Table_Name                                          \
                               " (instance_id, annotation_key, annotation_value) VALUES (?,?,?);"; \
        auto stmt = database_mgr::statement(SQL);                                                  \
        stmt.bind(1, instance_id);                                                                 \
        stmt.bind(2, key);                                                                         \
        stmt.bind(3, value);                                                                       \
        stmt.exec();                                                                               \
    }                                                                                              \
    catch (std::exception & e) {                                                                   \
        LOG(ERROR) << __func__ << " failed: " << e.what();                                         \
        throw;                                                                                     \
    }
FWK_DEFINE_DB_WRITE_ANNOTATIONS("AbilityCRAnnotation", ability_cr)
FWK_DEFINE_DB_WRITE_ANNOTATIONS("AbilityCRDAnnotation", ability_crd)
FWK_DEFINE_DB_WRITE_ANNOTATIONS("DeviceCRAnnotation", device_cr)
FWK_DEFINE_DB_WRITE_ANNOTATIONS("DeviceCRDAnnotation", device_crd)

#define FWK_DEFINE_DB_CLEAR(Table_Name, Item_Name)                                  \
    void db_clear_##Item_Name(const std::string& instance_id) try {                 \
        auto& db = database_mgr::get_database();                                    \
        constexpr char SQL[] = "DELETE FROM " Table_Name " WHERE instance_id = ?;"; \
                                                                                    \
        auto stmt = database_mgr::statement(SQL);                                   \
        stmt.bind(1, instance_id);                                                  \
        stmt.exec();                                                                \
    }                                                                               \
    catch (std::exception & e) {                                                    \
        LOG(ERROR) << __func__ << " failed: " << e.what();                          \
        throw;                                                                      \
    }

FWK_DEFINE_DB_CLEAR("AbilityCRAnnotation", ability_cr_annotations)
FWK_DEFINE_DB_CLEAR("AbilityCRLabel", ability_cr_labels)
FWK_DEFINE_DB_CLEAR("AbilityCRDAnnotation", ability_crd_annotations)
FWK_DEFINE_DB_CLEAR("AbilityCRDLabel", ability_crd_labels)
FWK_DEFINE_DB_CLEAR("DeviceCRAnnotation", device_cr_annotations)
FWK_DEFINE_DB_CLEAR("DeviceCRLabel", device_cr_labels)
FWK_DEFINE_DB_CLEAR("DeviceCRDAnnotation", device_crd_annotations)
FWK_DEFINE_DB_CLEAR("DeviceCRDLabel", device_crd_labels)
// create table if not exists AbilityCRBasic(
// create table if not exists AbilityCRBasic(
//   instance_id TEXT PRIMARY KEY, -- 实例id(作为主键)
//   ability_name TEXT NOT NULL, -- 能力类名称, 对应spec/abilityName
//   ability_version TEXT NOT NULL, -- 能力版本
//   instance_name TEXT NOT NULL, -- 实例名称, 对应metadata/name
//   insert_time INTEGER NOT NULL, -- 创建时间, 最近更新时间, unix秒格式
//   update_time INTEGER NOT NULL, -- 最近更新时间, unix秒格式
//   autostart INTEGER, -- 0 或不存在表示假, 1表示真
//   cr_detail TEXT NOT NULL, -- 原样存储json格式的CR
//   cr_filepath TEXT -- 如果源于某一个文件, 那么记录该文件的路径, 否则为空
// );
bool exist_row(SQLite::Statement& stmt) {
    while (stmt.executeStep()) {
        return true;
    }
    return false;
}

bool exists_device_cr(const std::string& id) {
    constexpr char SQL[] = "SELECT insert_time FROM DeviceCRBasic WHERE instance_id = ?";
    auto stmt = database_mgr::statement(SQL);
    stmt.bind(1, id);
    return exist_row(stmt);
}

bool exists_ability_cr(const std::string& id) {
    constexpr char SQL[] = "SELECT insert_time FROM AbilityCRBasic WHERE instance_id = ?";
    auto stmt = database_mgr::statement(SQL);
    stmt.bind(1, id);
    bool res = exist_row(stmt);
    LOG_IF(INFO, res) << "ability exist in db: " << id;
    return res;
}

std::optional<std::string> db_exists_ability_cr_by_name(const AbilityCR& cr) {
    auto id_str = to_string(cr.id);
    constexpr char SQL[] = R"sql(SELECT instance_id FROM AbilityCRBasic
    WHERE ability_name = ? 
      AND ability_version = ? 
      AND instance_name = ?;)sql";
    auto stmt = database_mgr::statement(SQL);
    stmt.bind(1, cr.spec->abilityName);
    stmt.bind(2, to_string(cr.spec->version));
    stmt.bind(3, cr.metadata.name);
    while (stmt.executeStep()) {
        return stmt.getColumn(0).getString();
    }
    return {};
}

std::string ResourceManager::add_ability_cr(const AbilityCR& cr) {
    try {
        auto transaction = database_mgr::transaction();
        auto opt_id_str = db_exists_ability_cr_by_name(cr);
        auto update_time = time(nullptr);
        auto id_str = to_string(cr.id);
        uuids::uuid res_id;
        if (opt_id_str) {
            id_str = *opt_id_str;
            constexpr char SQL[] = R"sql(UPDATE AbilityCRBasic
    SET ability_name = ?, ability_version = ?,
        instance_name = ?, update_time = ?, autostart = ?, keep_alive = ?, singleton = ?, cr_detail = ?
    WHERE instance_id = ?;)sql";
            auto stmt = database_mgr::statement(SQL);
            stmt.bind(1, cr.spec->abilityName);
            stmt.bind(2, to_string(cr.spec->version));
            stmt.bind(3, cr.metadata.name);
            stmt.bind(4, update_time);
            int autostart = cr.spec->autoStart.value_or(false) ? 1 : 0;
            stmt.bind(5, autostart);
            int keep_alive = cr.spec->keepAlive.value_or(false) ? 1 : 0;
            stmt.bind(6, keep_alive);
            int singleton_val = cr.spec->singleton.value_or(true) ? 1 : 0;
            stmt.bind(7, singleton_val);
            std::string cr_detail = nlohmann::json(cr).dump();
            stmt.bind(8, cr_detail);
            stmt.bind(9, *opt_id_str);
            stmt.exec();

            constexpr char SQL_update_insert_time[] = R"sql(
    UPDATE AbilityCRBasic 
    SET insert_time = ?
    WHERE instance_id = ?;)sql";
            stmt = database_mgr::statement(SQL_update_insert_time);
            stmt.bind(1, update_time);
            stmt.bind(2, *opt_id_str);
            stmt.exec();
        }
        else {
            auto id_new = make_uuid();
            opt_id_str = to_string(id_new);
            id_str = *opt_id_str;
            constexpr char SQL[] = R"sql(
    REPLACE INTO AbilityCRBasic
      (instance_id, ability_name, ability_version, instance_name,
       insert_time, update_time, autostart, keep_alive, singleton, cr_detail)
    VALUES
      (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);)sql";
            auto stmt = database_mgr::statement(SQL);
            stmt.bind(1, id_str);
            stmt.bind(2, cr.spec->abilityName);
            stmt.bind(3, to_string(cr.spec->version));
            stmt.bind(4, cr.metadata.name);
            stmt.bind(5, update_time);
            stmt.bind(6, update_time);
            int autostart = cr.spec->autoStart.value_or(false) ? 1 : 0;
            stmt.bind(7, autostart);
            int keep_alive = cr.spec->keepAlive.value_or(false) ? 1 : 0;
            stmt.bind(8, keep_alive);
            int singleton_val = cr.spec->singleton.value_or(true) ? 1 : 0;
            stmt.bind(9, singleton_val);
            std::string cr_detail = nlohmann::json(cr).dump();
            stmt.bind(10, cr_detail);
            stmt.exec();
        }

        db_clear_ability_cr_annotations(id_str);
        for (auto& [k, v] : cr.metadata.annotations) {
            db_write_ability_cr_annotations(id_str, k, v);
        }

        db_clear_ability_cr_labels(id_str);
        for (auto& [k, v] : cr.metadata.labels) {
            db_write_ability_cr_labels(id_str, k, v);
        }

        transaction.commit();
        return id_str;
    }
    catch (std::exception& e) {
        LOG(ERROR) << "add cr " << cr.id << "failed: " << e.what();
        throw;
    }
}

void ResourceManager::add_device_cr(const DeviceCR& cr) {
    auto id_str = to_string(cr.id);
    try {
        auto transaction = database_mgr::transaction();

        db_clear_device_cr_annotations(id_str);
        for (auto& [k, v] : cr.metadata.annotations) {
            db_write_device_cr_annotations(id_str, k, v);
        }

        db_clear_device_cr_labels(id_str);
        for (auto& [k, v] : cr.metadata.labels) {
            db_write_device_cr_labels(id_str, k, v);
        }

        auto update_time = time(nullptr);
        if (exists_device_cr(id_str)) {
            constexpr char SQL[] = R"sql(
    UPDATE DeviceCRBasic 
    SET device_name = ?, device_version = ?, 
        instance_name = ?, update_time = ?,  cr_detail = ? 
    WHERE instance_id = ?;)sql";
            auto stmt = database_mgr::statement(SQL);
            stmt.bind(1, cr.spec.deviceName);
            stmt.bind(2, to_string(cr.spec.version));
            stmt.bind(3, cr.metadata.name);
            stmt.bind(4, update_time);
            std::string cr_detail = nlohmann::json(cr).dump();
            stmt.bind(5, cr_detail);
            stmt.bind(6, id_str);
            stmt.exec();
        }
        else {
            constexpr char SQL[] = R"sql(
    REPLACE INTO DeviceCRBasic 
      (instance_id, device_name, device_version, 
       instance_name, insert_time, update_time,  cr_detail) 
    VALUES 
      (?, ?, ?, ?, ?, ?, ?);)sql";
            auto stmt = database_mgr::statement(SQL);
            stmt.bind(1, id_str);
            stmt.bind(2, cr.spec.deviceName);
            stmt.bind(3, to_string(cr.spec.version));
            stmt.bind(4, cr.metadata.name);
            stmt.bind(5, update_time);
            stmt.bind(6, update_time);
            std::string cr_detail = nlohmann::json(cr).dump();
            stmt.bind(7, cr_detail);
            stmt.exec();
        }

        transaction.commit();
    }
    catch (std::exception& e) {
        LOG(ERROR) << "add device cr " << id_str << "failed: " << e.what();
        throw;
    }
}

// CREATE TABLE IF NOT EXISTS AbilityCRDBasic(
//   ability_name TEXT NOT NULL, -- 能力类名称, 对应spec/abilityName
//   ability_version TEXT NOT NULL, -- 能力版本
//   crd_detail TEXT NOT NULL, -- 原样存储json格式存储的CRD
//   PRIMARY KEY (ability_name, ability_version)
// );
expected<void, std::string> ResourceManager::add_ability_crd(const AbilityCRD& crd) {
    try {
        auto transaction = database_mgr::transaction();
        constexpr char SQL[] = R"sql(
        REPLACE INTO AbilityCRDBasic
          (ability_name, ability_version, crd_detail) 
        VALUES (?,?,?)
        )sql";
        auto stmt = database_mgr::statement(SQL);
        stmt.bind(1, crd.metadata.name);
        stmt.bind(2, crd.version.to_string());
        auto detail_str = nlohmann::json(crd).dump();
        stmt.bind(3, detail_str);
        stmt.exec();
        transaction.commit();
    }
    catch (std::exception& e) {
        LOG(ERROR) << "add ability crd " << crd.metadata.name << "failed: " << e.what();
        throw;
    };

    return {};
}

expected<void, std::string> ResourceManager::add_device_crd(const DeviceCRD& crd) {
    try {
        auto transaction = database_mgr::transaction();
        constexpr char SQL[] = R"sql(
        REPLACE INTO DeviceCRDBasic
          (device_name, device_version, crd_detail) 
        VALUES (?,?,?)
        )sql";
        auto stmt = database_mgr::statement(SQL);
        stmt.bind(1, crd.metadata.name);
        stmt.bind(2, crd.version.to_string());
        auto detail_str = nlohmann::json(crd).dump();
        stmt.bind(3, detail_str);
        stmt.exec();
        transaction.commit();
    }
    catch (std::exception& e) {
        LOG(ERROR) << "add device crd " << crd.metadata.name << "failed: " << e.what();
        throw;
    };

    return {};
}

void ResourceManager::remove_ability_cr(ID id) {
    auto id_str = to_string(id);
    auto transaction = database_mgr::transaction();
    db_clear_ability_cr_labels(id_str);
    db_clear_ability_cr_annotations(id_str);
    constexpr char SQL[] = "DELETE FROM AbilityCRBasic WHERE instance_id = ?;";
    database_mgr::exec(SQL, id_str);
    transaction.commit();
}

void ResourceManager::remove_device_cr(ID id) {
    auto id_str = to_string(id);
    auto transaction = database_mgr::transaction();
    db_clear_device_cr_labels(id_str);
    db_clear_device_cr_annotations(id_str);
    constexpr char SQL[] = "DELETE FROM DeviceCRBasic WHERE instance_id = ?;";
    database_mgr::exec(SQL, id_str);
    transaction.commit();
}

void ResourceManager::remove_ability_crd(ID id) {
    auto id_str = to_string(id);
    auto transaction = database_mgr::transaction();
    db_clear_ability_crd_labels(id_str);
    db_clear_ability_crd_annotations(id_str);
    constexpr char SQL[] = "DELETE FROM AbilityCRDBasic WHERE instance_id = ?;";
    database_mgr::exec(SQL, id_str);
    transaction.commit();
}

void ResourceManager::remove_device_crd(ID id) {
    auto id_str = to_string(id);
    auto transaction = database_mgr::transaction();
    db_clear_device_crd_labels(id_str);
    db_clear_device_crd_annotations(id_str);
    constexpr char SQL[] = "DELETE FROM DeviceCRDBasic WHERE instance_id = ?;";
    database_mgr::exec(SQL, id_str);
    transaction.commit();
    std::lock_guard _lk(m);
}

// 为组合能力和抽象能力添加子能力 cr 以及子能力项
expected<void, std::string> ResourceManager::add_subabilities() {
    LOG_FIRST_N(WARNING, 10) << "ResourceManager::add_subabilities" << " is not implemented;";
    return {};
    // for (auto& ability : compose_abstract_abilities) {
    //     ability.subabilities.clear();
    //     for (const auto& it : ability.spec->subabilities) {
    //         uuids::uuid id
    //             = make_uuid(it->package, to_string(it->version), it->abilityName, "AtomAbility");
    //         nlohmann::json json_cr;
    //         json_cr["spec"] = *it;
    //         json_cr["kind"] = "AtomAbility";
    //         json_cr["metadata"]["name"] = it->abilityName;

    //         AbilityCR::SubAbilityEntry sub{id, it->position};
    //         ability.subabilities.emplace_back(sub);
    //         json_cr["id"] = to_string(id);
    //         json_cr["metadata"]["labels"]["fwk.io/onNode/id"] = get_framework_id();
    //         json_cr["metadata"]["labels"]["fwk.io/onNode/name"]
    //             = global_vars::get_config<std::string>("/framework_name");
    //         json_cr["tag"]["source"] = ability.kind;
    //         json_cr["tag"]["parent"] = ability.metadata.name;
    //         if (json_cr["spec"].contains("status")) {
    //             json_cr["status"] = json_cr["spec"]["status"];
    //             json_cr["spec"].erase("status");
    //         }
    //         json_cr["runInfo"]["lifecycleState"] = "Inactive";
    //         json_cr["runInfo"]["lastUpdate"] = 0;
    //         json_cr["runInfo"]["lastConnect"] = 0;
    //         AbilityCR cr = json_cr;
    //         if (is_local_cr(it->position)) {
    //             abilities.insert(id);
    //             add_ability_cr(cr);
    //         }
    //         else {
    //             auto res
    //                 = send_post_request(it->position, "/api/resourcemgr/add_abilityCr", json_cr);
    //             if (!res.has_value()) { return unexpected("post error"); }
    //             nlohmann::json json_res = nlohmann::json::parse(res.value());
    //             if (json_res["result"] == "success") { return {}; }
    //             return unexpected("remote add subabilities error");
    //         }
    //     }
    //     nlohmann::json cr_basic = extract_basic(ability);
    //     redis.set("/ability/" + to_string(ability.id) + "/basic", cr_basic.dump());
    // }
}

namespace {
std::optional<DeviceCR> find_device(
    const std::string& deviceName,
    const std::string& deviceInstance_name,
    const std::vector<DeviceCR>& crs
) {
    for (const auto& cr : crs) {
        if (cr.kind == deviceName && cr.metadata.name == deviceInstance_name) return cr;
    }
    return std::nullopt;
}
} // namespace

// 为能力添加设备项
expected<void, std::string> add_device_entry_for_ability_cr(ResourceManager& mgr) {
    auto ability_crs = mgr.get_local_ability_crs();
    auto device_crs = mgr.get_all_device_cr();
    for (auto& cr : ability_crs) {
        if (cr.spec->devices.empty()) continue;
        cr.devices.clear();
        for (const auto& it : cr.spec->devices) {
            auto device = find_device(it.deviceName, it.instanceName, device_crs);
            if (device) {
                AbilityCR::DeviceEntry sub{device.value().id, device.value().spec.position};
                cr.devices.emplace_back(sub);
            }
        }
        mgr.add_ability_cr(cr);
    }
    return {};
}
// 为能力添加设备入口

std::optional<uuids::uuid> find_cr_parent(ResourceManager& mgr, uuids::uuid instance_id) {
    std::vector<AbilityCR> ability_instances = mgr.get_all_ability_cr();
    for (const auto& cr : ability_instances) {
        bool is_parent
            = std::any_of(cr.subabilities.begin(), cr.subabilities.end(), [&](const auto& entry) {
                  return entry.id == instance_id;
              });
        if (is_parent) { return cr.id; }
    }
    return {};
}

bool ResourceManager::judge_ability_exist(const uuids::uuid& abilityInstance_id) const {
    auto id_str = to_string(abilityInstance_id);
    // 查 CR 模板表 (AbilityCRBasic) + 运行时实例表 (AbilityInstance)
    // occupation 可以传 template id 也可以传 instance id, 都要能命中
    constexpr char SQL[] = R"sql(
        SELECT EXISTS(
            SELECT 1 FROM AbilityCRBasic WHERE instance_id = ?
            UNION ALL
            SELECT 1 FROM AbilityInstance WHERE instance_id = ?
        )
    )sql";
    auto stmt = database_mgr::statement(SQL);
    stmt.bind(1, id_str);
    stmt.bind(2, id_str);
    if (stmt.executeStep()) {
        return stmt.getColumn(0).getInt() != 0;
    }
    return false;
}

bool ResourceManager::judge_device_exist(const uuids::uuid& deviceInstance_id) const {
    const char SQL[]
        = "SELECT exists(SELECT 1 FROM DeviceCRBasic WHERE instance_id = ?) AS row_exists;";
    auto stmt = database_mgr::statement(SQL);
    stmt.bind(1, to_string(deviceInstance_id));
    while (stmt.executeStep()) {
        return true;
        int res = stmt.getColumn(0);
        return static_cast<bool>(res);
    }
    return false;
}
// create table if not exists AbilityCRBasic(
//   instance_id TEXT PRIMARY KEY, -- 实例id(作为主键)
//   ability_name TEXT NOT NULL, -- 能力类名称, 对应spec/abilityName
//   ability_version TEXT NOT NULL, -- 能力版本
//   instance_name TEXT NOT NULL, -- 实例名称, 对应metadata/name
//   insert_time INTEGER NOT NULL, -- 创建时间, 最近更新时间, unix秒格式
//   update_time INTEGER NOT NULL, -- 最近更新时间, unix秒格式
//   autostart INTEGER, -- 0 或不存在表示假, 1表示真
//   cr_detail TEXT NOT NULL, -- 原样存储json格式的CR
//   cr_filepath TEXT -- 如果源于某一个文件, 那么记录该文件的路径, 否则为空
// );
AbilityCR ability_cr_from_db_basic(SQLite::Statement& stmt) try {
    auto cr_detail_str = stmt.getColumn("cr_detail").getString();
    auto res = nlohmann::json::parse(cr_detail_str).get<AbilityCR>();
    res.metadata.name = stmt.getColumn("instance_name").getString();
    res.spec->autoStart = stmt.getColumn("autostart").getInt() != 0;
    res.spec->keepAlive = stmt.getColumn("keep_alive").getInt() != 0;
    res.spec->singleton = stmt.getColumn("singleton").getInt() != 0;
    res.id = uuids::uuid::from_string(stmt.getColumn("instance_id").getString()).value();
    return res;
}
catch (std::exception& e) {
    LOG(ERROR) << __func__ << " failed: " << e.what();
    throw;
}

#define FWK_DEFINE_DB_GET_LABEL_OR_ANNOTATION(Table_Name, Key_Prefix, Func_Suffix)         \
    std::unordered_map<std::string, std::string> db_get_##Func_Suffix(                     \
        const std::string& instance_id                                                     \
    ) {                                                                                    \
        const char SQL[]                                                                   \
            = "SELECT " Key_Prefix "_key, " Key_Prefix "_value FROM " Table_Name " WHERE " \
              "instance_id = ?;";                                                          \
        auto stmt = database_mgr::statement(SQL);                                          \
        stmt.bind(1, instance_id);                                                         \
        return execute_to_kvmap_unordered(stmt);                                           \
    }

FWK_DEFINE_DB_GET_LABEL_OR_ANNOTATION("AbilityCRLabel", "label", ability_cr_labels)
FWK_DEFINE_DB_GET_LABEL_OR_ANNOTATION("AbilityCRDLabel", "label", ability_crd_labels)
FWK_DEFINE_DB_GET_LABEL_OR_ANNOTATION("AbilityCRAnnotation", "annotation", ability_cr_annotations)
FWK_DEFINE_DB_GET_LABEL_OR_ANNOTATION("AbilityCRDAnnotation", "annotation", ability_crd_annotations)

FWK_DEFINE_DB_GET_LABEL_OR_ANNOTATION("DeviceCRLabel", "label", device_cr_labels)
FWK_DEFINE_DB_GET_LABEL_OR_ANNOTATION("DeviceCRDLabel", "label", device_crd_labels)
FWK_DEFINE_DB_GET_LABEL_OR_ANNOTATION("DeviceCRAnnotation", "annotation", device_cr_annotations)
FWK_DEFINE_DB_GET_LABEL_OR_ANNOTATION("DeviceCRDAnnotation", "annotation", device_crd_annotations)

void append_ability_basic_cr_with_labels_and_annotations(AbilityCR& cr, const std::string& id_str) {
    cr.metadata.labels = db_get_ability_cr_labels(id_str);
    cr.metadata.annotations = db_get_ability_cr_annotations(id_str);
}

std::optional<AbilityCR> db_get_ability_cr_basic(const std::string& id) {
    const char SQL[] = "SELECT * FROM AbilityCRBasic WHERE instance_id = ?;";
    auto stmt = database_mgr::statement(SQL);
    stmt.bind(1, id);
    while (stmt.executeStep()) {
        auto res = ability_cr_from_db_basic(stmt);
        res.id = uuids::uuid::from_string(id).value();

        append_ability_basic_cr_with_labels_and_annotations(res, id);
        return {std::move(res)};
    }
    return {};
}
std::optional<AbilityCR> ResourceManager::get_ability_cr(ID abilityInstance_id) const {
    auto id_str = to_string(abilityInstance_id);

    auto res = db_get_ability_cr_basic(id_str);
    if (!res) { return {}; }
    append_ability_basic_cr_with_labels_and_annotations(*res, id_str);
    return res;
}
// create table if not exists DeviceCRBasic(
//   instance_id TEXT PRIMARY KEY, -- 实例id(作为主键)
//   device_name TEXT NOT NULL, -- 能力类名称, 对应spec/abilityName
//   device_version TEXT NOT NULL, -- 能力版本
//   instance_name TEXT NOT NULL, -- 实例名称, 对应metadata/name
//   insert_time INTEGER NOT NULL, -- 创建时间, 最近更新时间, unix秒格式
//   update_time INTEGER NOT NULL, -- 最近更新时间, unix秒格式
//   cr_detail TEXT NOT NULL, -- 原样存储json格式的CR
//   cr_filepath TEXT -- 如果源于某一个文件, 那么记录该文件的路径, 否则为空
// );

DeviceCR device_cr_from_db_basic_01(SQLite::Statement& stmt) {
    auto cr_detail_str = stmt.getColumn(1).getString();
    auto res = nlohmann::json::parse(cr_detail_str).get<DeviceCR>();
    res.metadata.name = stmt.getColumn(0).getString();
    return res;
}
std::optional<DeviceCR> db_get_device_cr_basic(const std::string& id) {
    const char SQL[] = "SELECT instance_name, cr_detail FROM DeviceCRBasic WHERE instance_id = ?;";
    auto stmt = database_mgr::statement(SQL);
    stmt.bind(1, id);
    while (stmt.executeStep()) {
        return device_cr_from_db_basic_01(stmt);
    }
    return {};
}
void append_device_basic_cr_with_labels_and_annotations(DeviceCR& cr, const std::string& id_str) {
    cr.metadata.labels = db_get_device_cr_labels(id_str);
    cr.metadata.annotations = db_get_device_cr_annotations(id_str);
}

std::optional<DeviceCR> ResourceManager::get_device_cr(ID deviceInstance_id) {
    auto id_str = to_string(deviceInstance_id);
    auto res = db_get_device_cr_basic(id_str);
    if (!res) { return {}; }
    append_device_basic_cr_with_labels_and_annotations(*res, id_str);
    return res;
}

std::vector<AbilityCR> ResourceManager::get_all_ability_cr() {
    CPPTRACE_TRY {
        std::vector<AbilityCR> res;
        constexpr char SQL[] = "SELECT * FROM AbilityCRBasic;";
        auto stmt = database_mgr::statement(SQL);
        while (stmt.executeStep()) {
            res.push_back(ability_cr_from_db_basic(stmt));
            auto& cr = res.back();
        }
        for (auto& cr : res) {
            auto id_str = to_string(cr.id);
            append_ability_basic_cr_with_labels_and_annotations(cr, id_str);
        }
        return res;
    }
    CPPTRACE_CATCH(std::exception & e) {
        FWK_DUMP_STACKTRACE_TO_LOG(ERROR);
        throw;
    }
}

std::vector<AbilityCR> ResourceManager::get_local_ability_crs() {
    LOG_FIRST_N(WARNING, 10) << "ResourceManager::get_local_ability_crs() is currently equal to "
                                "ResourceManager::get_all_ability_cr()";
    return get_all_ability_cr();
}

std::vector<DeviceCR> ResourceManager::get_all_device_cr() {
    std::vector<DeviceCR> res;
    constexpr char SQL[] = "SELECT instance_name, cr_detail FROM DeviceCRBasic;";
    auto stmt = database_mgr::statement(SQL);
    while (stmt.executeStep()) {
        res.push_back(device_cr_from_db_basic_01(stmt));
        auto& cr = res.back();
    }
    for (auto& cr : res) {
        auto id_str = to_string(cr.id);
        append_device_basic_cr_with_labels_and_annotations(cr, id_str);
    }
    return res;
}

std::vector<DeviceCR> ResourceManager::get_local_device_crs() {
    LOG(WARNING) << "ResourceManager::get_local_device_crs() is currently equal to "
                    "ResourceManager::get_all_device_cr()";
    return get_all_device_cr();
    // std::vector<DeviceCR> res;
    // for (const auto& id : devices) {
    //     auto cr = get_device_cr(id);
    //     res.emplace_back(*cr);
    // }
    // return res;
}
std::vector<AbilityCRD> ResourceManager::get_all_ability_crd() const {
    std::vector<AbilityCRD> res;
    constexpr char SQL[] = "SELECT crd_detail FROM AbilityCRDBasic;";
    auto stmt = database_mgr::statement(SQL);
    while (stmt.executeStep()) {
        auto crd_str = stmt.getColumn(0).getString();
        auto crd = nlohmann::json::parse(crd_str).get<AbilityCRD>();
        res.push_back(std::move(crd));
    }
    return res;
}

std::vector<DeviceCRD> ResourceManager::get_all_device_crd() const {
    std::vector<DeviceCRD> res;
    constexpr char SQL[] = "SELECT crd_detail FROM DeviceCRDBasic;";
    auto stmt = database_mgr::statement(SQL);
    while (stmt.executeStep()) {
        auto crd_str = stmt.getColumn(0).getString();
        auto crd = nlohmann::json::parse(crd_str).get<DeviceCRD>();
        res.push_back(std::move(crd));
    }
    return res;
}

std::vector<std::string> ResourceManager::get_heartbeats_by_ability_name(
    const std::string& ability_name
) const {
    // 查询 AbilityInstance 表中该能力类名的运行中实例
    std::vector<std::string> result;
    constexpr char SQL[] =
        "SELECT instance_id FROM AbilityInstance WHERE ability_name = ? AND state NOT IN ('Inactive', 'Terminated')";
    auto stmt = database_mgr::statement(SQL);
    stmt.bind(1, ability_name);
    while (stmt.executeStep()) {
        result.push_back(stmt.getColumn(0).getString());
    }
    return result;
}

namespace {
ResourceManager::AbilityInstanceInfo instance_info_from_row(SQLite::Statement& stmt) {
    using Info = ResourceManager::AbilityInstanceInfo;
    Info info;
    auto id_str = stmt.getColumn("instance_id").getString();
    info.instance_id = uuids::uuid::from_string(id_str).value_or(uuids::uuid{});
    auto cr_id_col = stmt.getColumn("cr_id");
    if (!cr_id_col.isNull()) {
        if (auto cid = uuids::uuid::from_string(cr_id_col.getString())) { info.cr_id = *cid; }
    }
    info.cr_name = stmt.getColumn("cr_name").getString();
    info.instance_name = stmt.getColumn("instance_name").getString();
    info.ability_name = stmt.getColumn("ability_name").getString();
    info.ability_version = stmt.getColumn("ability_version").getString();
    info.state = stmt.getColumn("state").getString();
    {
        auto col = stmt.getColumn("start_time");
        info.start_time = col.isNull() ? 0 : col.getInt64();
    }
    {
        auto col = stmt.getColumn("stop_time");
        info.stop_time = col.isNull() ? 0 : col.getInt64();
    }
    {
        auto col = stmt.getColumn("detail");
        if (!col.isNull()) {
            try {
                info.detail = nlohmann::json::parse(col.getString());
            } catch (...) {}
        }
    }
    {
        auto col = stmt.getColumn("spec_snapshot");
        if (!col.isNull()) {
            try {
                info.spec_snapshot = nlohmann::json::parse(col.getString());
            } catch (...) {}
        }
    }
    return info;
}

void to_json(nlohmann::json& j, const ResourceManager::AbilityInstanceInfo& info) {
    j = nlohmann::json{
        {"instance_id", to_string(info.instance_id)},
        {"cr_id", info.cr_id ? to_string(*info.cr_id) : ""},
        {"cr_name", info.cr_name},
        {"instance_name", info.instance_name},
        {"ability_name", info.ability_name},
        {"ability_version", info.ability_version},
        {"state", info.state},
        {"start_time", info.start_time},
        {"stop_time", info.stop_time},
        {"detail", info.detail},
    };
}
} // namespace

uuids::uuid ResourceManager::create_ability_instance(const AbilityCR& cr_template) {
    auto instance_id = make_uuid();
    auto id_str = to_string(instance_id);
    auto cr_id_str = to_string(cr_template.id);
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();

    // 实例显示名：<模板名>-<id 前 8 位>
    std::string display_name = cr_template.metadata.name + "-" + id_str.substr(0, 8);

    // 存 spec 快照，后续 LifecycleMgr 查询时可用
    // 注意: 序列化时临时把 id 改成 instance_id，让下游一致
    AbilityCR snapshot = cr_template;
    snapshot.id = instance_id;
    if (!snapshot.runInfo) { snapshot.runInfo.emplace(); }
    snapshot.runInfo->lifecycleState = LifecycleState::Inactive;
    nlohmann::json snapshot_json = snapshot;

    constexpr char SQL[] = R"sql(
        INSERT INTO AbilityInstance
          (instance_id, cr_id, cr_name, instance_name,
           ability_name, ability_version, state, start_time, spec_snapshot)
        VALUES (?, ?, ?, ?, ?, ?, 'Inactive', ?, ?)
    )sql";
    auto stmt = database_mgr::statement(SQL);
    stmt.bind(1, id_str);
    stmt.bind(2, cr_id_str);
    stmt.bind(3, cr_template.metadata.name);
    stmt.bind(4, display_name);
    stmt.bind(5, cr_template.spec->abilityName);
    stmt.bind(6, cr_template.spec->version.to_string());
    stmt.bind(7, static_cast<int64_t>(now));
    stmt.bind(8, snapshot_json.dump());
    stmt.exec();
    LOG(INFO) << "create ability instance " << id_str << " from template "
              << cr_template.metadata.name << " (cr_id=" << cr_id_str << ")";
    return instance_id;
}

void ResourceManager::delete_ability_instance(ID instance_id) {
    constexpr char SQL[] = "DELETE FROM AbilityInstance WHERE instance_id = ?";
    auto stmt = database_mgr::statement(SQL);
    stmt.bind(1, to_string(instance_id));
    stmt.exec();
    LOG(INFO) << "delete ability instance " << to_string(instance_id);
}

std::optional<ResourceManager::AbilityInstanceInfo>
ResourceManager::get_ability_instance(ID instance_id) const {
    constexpr char SQL[] = "SELECT * FROM AbilityInstance WHERE instance_id = ?";
    auto stmt = database_mgr::statement(SQL);
    stmt.bind(1, to_string(instance_id));
    if (stmt.executeStep()) { return instance_info_from_row(stmt); }
    return {};
}

std::vector<ResourceManager::AbilityInstanceInfo>
ResourceManager::get_all_ability_instances() const {
    std::vector<AbilityInstanceInfo> res;
    constexpr char SQL[] = "SELECT * FROM AbilityInstance";
    auto stmt = database_mgr::statement(SQL);
    while (stmt.executeStep()) { res.push_back(instance_info_from_row(stmt)); }
    return res;
}

std::vector<ResourceManager::AbilityInstanceInfo>
ResourceManager::get_active_instances_by_ability_name(const std::string& ability_name) const {
    std::vector<AbilityInstanceInfo> res;
    constexpr char SQL[] =
        "SELECT * FROM AbilityInstance WHERE ability_name = ? "
        "AND state NOT IN ('Inactive', 'Terminated')";
    auto stmt = database_mgr::statement(SQL);
    stmt.bind(1, ability_name);
    while (stmt.executeStep()) { res.push_back(instance_info_from_row(stmt)); }
    return res;
}

std::optional<AbilityCR> ResourceManager::resolve_ability_cr_by_id(ID id) const {
    // 1) 优先按 instance_id 从 AbilityInstance 表取 spec_snapshot 还原
    constexpr char SQL[] = "SELECT spec_snapshot FROM AbilityInstance WHERE instance_id = ?";
    auto stmt = database_mgr::statement(SQL);
    stmt.bind(1, to_string(id));
    if (stmt.executeStep()) {
        auto col = stmt.getColumn(0);
        if (!col.isNull()) {
            try {
                auto j = nlohmann::json::parse(col.getString());
                AbilityCR cr = j.get<AbilityCR>();
                cr.id = id;
                return cr;
            } catch (const std::exception& e) {
                LOG(WARNING) << "failed to parse spec_snapshot for instance " << to_string(id)
                             << ": " << e.what();
            }
        }
    }
    // 2) 回退：按 CR id 从模板表取 (兼容旧路径 / 子能力 id)
    return get_ability_cr(id);
}

std::optional<AbilityManifest> ResourceManager::get_ability_manifest(
    const std::string& ability_name, const std::string& version
) const {
    std::lock_guard lock(m);
    for (const auto& pkg : packages) {
        if (to_string(pkg.version) != version) { continue; }
        auto it = pkg.manifests.find(ability_name);
        if (it != pkg.manifests.end()) { return it->second; }
    }
    return std::nullopt;
}

std::string ResourceManager::add_service_cr(const ServiceCR& cr) {
    std::lock_guard lock(m);
    auto id_str = to_string(cr.id);
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();
    nlohmann::json cr_json = cr;
    constexpr char SQL[] = R"sql(
        INSERT OR REPLACE INTO ServiceCRBasic
        (instance_id, service_name, service_version, instance_name, insert_time, update_time, state, restart_count, cr_detail)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
    )sql";
    auto stmt = database_mgr::statement(SQL);
    stmt.bind(1, id_str);
    stmt.bind(2, cr.spec->serviceName);
    stmt.bind(3, cr.spec->version.to_string());
    stmt.bind(4, cr.metadata.name);
    stmt.bind(5, static_cast<int64_t>(now));
    stmt.bind(6, static_cast<int64_t>(now));
    stmt.bind(7, std::string(to_string(cr.state)));
    stmt.bind(8, cr.restartCount);
    stmt.bind(9, cr_json.dump());
    stmt.exec();
    LOG(INFO) << "add service cr: " << cr.metadata.name << " id=" << id_str;
    return id_str;
}

void ResourceManager::remove_service_cr(uuids::uuid id) {
    std::lock_guard lock(m);
    constexpr char SQL[] = "DELETE FROM ServiceCRBasic WHERE instance_id = ?";
    auto stmt = database_mgr::statement(SQL);
    stmt.bind(1, to_string(id));
    stmt.exec();
    LOG(INFO) << "remove service cr: " << to_string(id);
}

std::optional<ServiceCR> ResourceManager::get_service_cr(uuids::uuid id) const {
    std::lock_guard lock(m);
    constexpr char SQL[] = "SELECT cr_detail FROM ServiceCRBasic WHERE instance_id = ?";
    auto stmt = database_mgr::statement(SQL);
    stmt.bind(1, to_string(id));
    if (stmt.executeStep()) {
        auto cr_str = stmt.getColumn(0).getString();
        return nlohmann::json::parse(cr_str).get<ServiceCR>();
    }
    return std::nullopt;
}

std::vector<ServiceCR> ResourceManager::get_all_service_cr() const {
    std::lock_guard lock(m);
    std::vector<ServiceCR> res;
    constexpr char SQL[] = "SELECT cr_detail FROM ServiceCRBasic";
    auto stmt = database_mgr::statement(SQL);
    while (stmt.executeStep()) {
        auto cr_str = stmt.getColumn(0).getString();
        res.push_back(nlohmann::json::parse(cr_str).get<ServiceCR>());
    }
    return res;
}

void ResourceManager::clear() {
    LOG(ERROR) << "ResourceManager::clear() is not implemented";
    return;
    // for (const auto& id : abilities) {
    //     remove_ability_cr(id);
    // }
    // for (const auto& id : devices) {
    //     remove_device_cr(id);
    // }
    // redis.remove("/framework/" + to_string(framework_id) + "/abilities");
    // redis.remove("/framework/" + to_string(framework_id) + "/devices");
    // remove_framework_id_from_redis();
}

std::optional<AbilityCRD> ResourceManager::get_ability_crd(
    const std::string& ability_name, const std::string& version
) const {
    std::lock_guard _lk(m);
    const char* SQL = R"sql(SELECT crd_detail FROM AbilityCRDBasic
      WHERE ability_name = ? AND ability_version = ?;)sql";
    auto stmt = database_mgr::statement(SQL);

    stmt.bind(1, ability_name);
    stmt.bind(2, version);
    while (stmt.executeStep()) {
        std::string cr_detail = stmt.getColumn(0);
        return nlohmann::json::parse(cr_detail).get<AbilityCRD>();
    }
    return std::nullopt;
}

std::optional<DeviceCRD> ResourceManager::get_device_crd(
    const std::string& device_name, const std::string& version
) const {
    std::lock_guard _lk(m);
    const char* SQL = R"sql(SELECT crd_detail FROM DeviceCRDBasic
      WHERE device_name = ? AND device_version = ?;)sql";
    auto stmt = database_mgr::statement(SQL);

    stmt.bind(1, device_name);
    stmt.bind(2, version);
    while (stmt.executeStep()) {
        std::string cr_detail = stmt.getColumn(0);
        return nlohmann::json::parse(cr_detail).get<DeviceCRD>();
    }
    return std::nullopt;
}

std::optional<AbilityCRD> ResourceManager::get_ability_crd_by_name(std::string_view crd_name
) const {
    std::lock_guard _lk(m);
    const char* SQL = R"sql(SELECT crd_detail FROM AbilityCRDBasic
      WHERE ability_name = ?;)sql";
    auto stmt = database_mgr::statement(SQL);

    std::string ability_name{crd_name};
    stmt.bind(1, ability_name);
    while (stmt.executeStep()) {
        std::string cr_detail = stmt.getColumn(0);
        return nlohmann::json::parse(cr_detail).get<AbilityCRD>();
    }
    return std::nullopt;
}

std::optional<DeviceCRD> ResourceManager::get_device_crd_by_name(std::string_view crd_name) const {
    std::lock_guard _lk(m);
    const char* SQL = R"sql(SELECT crd_detail FROM DeviceCRDBasic
      WHERE device_name = ?;)sql";
    auto stmt = database_mgr::statement(SQL);

    std::string device_name{crd_name};
    stmt.bind(1, device_name);
    while (stmt.executeStep()) {
        std::string cr_detail = stmt.getColumn(0);
        return nlohmann::json::parse(cr_detail).get<DeviceCRD>();
    }
    return std::nullopt;
}
namespace {
// 把 CR 校验失败信息写入 <home>/log/cr_validation.log，方便用户事后排查
// 与 glog 主日志独立，每条记录包含时间戳/文件路径/CR 名称/原因
// 文件在每次 read_crs_into_db 开始时清空，始终反映"当前一轮"的失败快照
std::filesystem::path cr_validation_log_path() {
    return global_vars::home_path() / "log" / "cr_validation.log";
}

void cr_validation_log_reset() {
    auto log_path = cr_validation_log_path();
    std::error_code ec;
    std::filesystem::create_directories(log_path.parent_path(), ec);
    // 截断文件
    std::ofstream ofs(log_path, std::ios::trunc);
}

void log_cr_validation_failure(
    const std::filesystem::path& cr_file,
    const std::string& cr_kind,
    const std::string& cr_name,
    const std::string& reason
) {
    auto log_path = cr_validation_log_path();
    std::error_code ec;
    std::filesystem::create_directories(log_path.parent_path(), ec);

    std::ofstream ofs(log_path, std::ios::app);
    if (!ofs) {
        LOG(WARNING) << "failed to open CR validation log: " << log_path;
        return;
    }
    auto t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    ofs << "[" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "] "
        << "file=" << cr_file.string() << " kind=" << cr_kind << " name=" << cr_name << "\n"
        << "  reason: " << reason << "\n\n";
}

std::variant<std::monostate, AbilityCR, DeviceCR> read_cr_from_yaml(
    const ResourceManager& mgr, const std::filesystem::path& filepath, const YAML::Node& cr_yaml
) {
    auto name = get_name(cr_yaml);
    if (!name) {
        LOG(WARNING) << "invalid cr in " << filepath << ", " << name.error();
        return {};
    }
    auto kind = get_kind(cr_yaml);
    if (!kind) {
        LOG(WARNING) << "invalid cr in " << filepath << ", " << kind.error();
        return {};
    }
    if (is_ability(*kind)) {
        auto packageName = get_spec_package(cr_yaml);
        if (!packageName) {
            LOG(WARNING) << "invalid ability cr in " << filepath << ", " << packageName.error();
            return {};
        }
        auto version = get_spec_version(cr_yaml);
        if (!version) {
            LOG(WARNING) << "invalid ability cr in " << filepath << ", " << version.error();
            return {};
        }
        uuids::uuid id = make_uuid(
            packageName.value(), to_string(version.value()), name.value(), kind.value()
        );
        nlohmann::json json_node = yaml_to_json(cr_yaml);
        json_node["id"] = id;
        json_node["tag"]["source"] = "file";
        json_node["tag"]["parent"] = "file";
        json_node["metadata"]["labels"]["fwk.io/onNode/id"] = mgr.get_framework_id();
        json_node["metadata"]["labels"]["fwk.io/onNode/name"]
            = global_vars::get_config<std::string>("/framework_name");
        json_node["runInfo"]["lifecycleState"] = "Inactive";
        json_node["runInfo"]["lastUpdate"] = 0;
        json_node["runInfo"]["lastConnect"] = 0;
        AbilityCR cr = json_node;
        auto val_res = mgr.check_ability_cr_validity(cr);
        if (!val_res) {
            LOG(ERROR) << "ability cr: " << cr.metadata.name << " is invalid: " << val_res.error();
            log_cr_validation_failure(filepath, *kind, cr.metadata.name, val_res.error());
            return {};
        }
        // 强制检查 spec.tasks 字段（避免 CR 被意外裁剪导致任务字段丢失）
        auto manifest_opt = mgr.get_ability_manifest(cr.spec->abilityName, to_string(cr.spec->version));
        if (!manifest_opt) {
            std::string reason = "manifest not found for "
                               + cr.spec->abilityName + " " + to_string(cr.spec->version);
            LOG(ERROR) << "ability cr: " << cr.metadata.name << " task validation skipped: " << reason;
            log_cr_validation_failure(filepath, *kind, cr.metadata.name, reason);
            return {};
        }
        auto task_res = validate_cr_tasks_field(json_node, *manifest_opt);
        if (!task_res) {
            LOG(ERROR) << "ability cr: " << cr.metadata.name
                       << " task validation failed: " << task_res.error();
            log_cr_validation_failure(filepath, *kind, cr.metadata.name, task_res.error());
            return {};
        }
        return cr;
    }
    else if (*kind == "Device") {
        // 是设备CR
        auto packageName = get_spec_package(cr_yaml);
        if (!packageName) {
            LOG(WARNING) << "invalid device cr in " << filepath << ", " << packageName.error();
            return {};
        }
        auto version = get_spec_version(cr_yaml);
        if (!version) {
            LOG(WARNING) << "invalid device cr in " << filepath << ", " << version.error();
            return {};
        }
        uuids::uuid id = make_uuid(
            packageName.value(), to_string(version.value()), name.value(), kind.value()
        );
        nlohmann::json json_node = yaml_to_json(cr_yaml);
        json_node["id"] = id;
        json_node["metadata"]["labels"]["fwk.io/onNode/id"] = mgr.get_framework_id();
        json_node["metadata"]["labels"]["fwk.io/onNode/name"]
            = global_vars::get_config<std::string>("/framework_name");
        DeviceCR cr = json_node;
        auto val_res = mgr.check_device_cr_validity(cr);
        if (!val_res) {
            LOG(ERROR) << "device cr: " << cr.metadata.name << " is invalid: " << val_res.error();
            log_cr_validation_failure(filepath, *kind, cr.metadata.name, val_res.error());
            return {};
        }
        LOG(INFO) << "valid device cr: " << cr.metadata.name;
        return cr;
    }
    else {
        LOG(ERROR) << "cr is neither ability nor device";
        return {};
    }
}

} // namespace

void ResourceManager::read_crs_into_db(const std::filesystem::path& path_crs) {
    compose_abstract_abilities.clear();
    // 每轮 CR 加载前清空 cr_validation.log，使其反映当前一轮的失败快照
    cr_validation_log_reset();

    // 收集本轮成功加载的 (ability_name, version, instance_name) 三元组,
    // 加载完后用它来 reconcile，把不在 yaml 里的孤儿 CR 行清掉。
    // 设计契约：CR 只能由能力包/yaml 注入，运行时不通过 API 增删模板。
    std::set<std::tuple<std::string, std::string, std::string>> ability_keys;
    std::set<std::tuple<std::string, std::string, std::string>> device_keys;

    // 递归扫描，使包内嵌 CR 可以放在 crs/_packages/<pkg>/<version>/ 等子目录下
    for (const auto& entry : std::filesystem::recursive_directory_iterator(path_crs)) {
        if (!is_regular_file(entry)) { continue; }
        if (!extension_is_yaml(entry.path())) { continue; }
        auto op_yaml = read_yaml_from_path(entry);
        if (!op_yaml) { continue; }
        const auto& cr_yaml = *op_yaml;
        auto maybe_cr = read_cr_from_yaml(*this, entry, cr_yaml);
        if (auto ability = std::get_if<AbilityCR>(&maybe_cr)) {
            (void)add_ability_cr(*ability);
            ability_keys.emplace(
                ability->spec->abilityName, to_string(ability->spec->version), ability->metadata.name
            );
            if (ability->kind == "ComposeAbility" || ability->kind == "AbstractAbility") {
                compose_abstract_abilities.emplace_back(*ability);
            }
        }
        else if (auto device = std::get_if<DeviceCR>(&maybe_cr)) {
            add_device_cr(*device);
            device_keys.emplace(
                device->spec.deviceName, to_string(device->spec.version), device->metadata.name
            );
        }
        else {
            LOG(ERROR) << "Failed to parse YAML: entry=" << entry << ", cr_yaml is:\n" << cr_yaml;
        }
    }

    add_subabilities();

    // Reconcile: 删除所有不在本轮 yaml 中的 ability CR 行
    {
        constexpr char SQL[] =
            "SELECT instance_id, ability_name, ability_version, instance_name FROM AbilityCRBasic";
        std::vector<std::string> stale_ids;
        auto stmt = database_mgr::statement(SQL);
        while (stmt.executeStep()) {
            std::tuple<std::string, std::string, std::string> key{
                stmt.getColumn(1).getString(),
                stmt.getColumn(2).getString(),
                stmt.getColumn(3).getString()
            };
            if (!ability_keys.contains(key)) {
                stale_ids.push_back(stmt.getColumn(0).getString());
            }
        }
        for (auto& id : stale_ids) {
            LOG(INFO) << "reconcile: drop orphan ability CR row " << id;
            constexpr char DEL[] = "DELETE FROM AbilityCRBasic WHERE instance_id = ?";
            auto del = database_mgr::statement(DEL);
            del.bind(1, id);
            del.exec();
            // 顺手清掉关联的 labels/annotations/file info
            for (const char* tbl :
                 {"AbilityCRLabel", "AbilityCRAnnotation", "AbilityCRFileInfo"}) {
                std::string sql_drop = std::string("DELETE FROM ") + tbl + " WHERE instance_id = ?";
                auto s = database_mgr::statement(sql_drop);
                s.bind(1, id);
                s.exec();
            }
        }
    }
    // Reconcile: 同样清理 device CR 孤儿行
    {
        constexpr char SQL[] =
            "SELECT instance_id, device_name, device_version, instance_name FROM DeviceCRBasic";
        std::vector<std::string> stale_ids;
        auto stmt = database_mgr::statement(SQL);
        while (stmt.executeStep()) {
            std::tuple<std::string, std::string, std::string> key{
                stmt.getColumn(1).getString(),
                stmt.getColumn(2).getString(),
                stmt.getColumn(3).getString()
            };
            if (!device_keys.contains(key)) {
                stale_ids.push_back(stmt.getColumn(0).getString());
            }
        }
        for (auto& id : stale_ids) {
            LOG(INFO) << "reconcile: drop orphan device CR row " << id;
            constexpr char DEL[] = "DELETE FROM DeviceCRBasic WHERE instance_id = ?";
            auto del = database_mgr::statement(DEL);
            del.bind(1, id);
            del.exec();
            for (const char* tbl : {"DeviceCRLabel", "DeviceCRAnnotation", "DeviceCRFileInfo"}) {
                std::string sql_drop = std::string("DELETE FROM ") + tbl + " WHERE instance_id = ?";
                auto s = database_mgr::statement(sql_drop);
                s.bind(1, id);
                s.exec();
            }
        }
    }
}

expected<void, std::string> ResourceManager::read_cr_in_framework_cr_dir(
    const std::filesystem::path& path
) {
    if (!std::filesystem::exists(path)) {
        LOG(ERROR) << path << "does not exists";
        return unexpected{"path does not exists"};
    }
    if (!std::filesystem::is_directory(path)) {
        LOG(ERROR) << path << "is not a directory";
        return unexpected{"path is not a directory"};
    }
    read_crs_into_db(path);
    return {};
}

expected<void, std::string> ResourceManager::check_local_ability_cr_validity(const AbilityCR& cr
) const {
    auto manifest = get_ability_manifest(cr.spec->abilityName, to_string(cr.spec->version));
    if (!manifest) {
        return unexpected("manifest not found for " + cr.spec->abilityName + " " + to_string(cr.spec->version));
    }
    // 使用两级验证
    nlohmann::json cr_json = cr;
    auto fw_res = validate_cr_framework_level(cr_json);
    if (!fw_res) { return fw_res; }
    return validate_cr_manifest_level(cr, *manifest);
}

namespace {
expected<void, std::string> check_remote_ability_cr_validity(const AbilityCR& cr) {
    // 发送post请求
    nlohmann::json payload = cr;
    auto res = send_post_request(
        cr.spec->position, "/api/resourcemgr/check_ability_cr_validity", payload
    );
    if (!res.has_value()) { return unexpected("post error"); }
    nlohmann::json json_res = nlohmann::json::parse(res.value());
    if (json_res.value("result", "") == "success") { return {}; }
    return unexpected("remote check cr validity error");
}
} // namespace

expected<void, std::string> ResourceManager::check_ability_cr_validity(const AbilityCR& cr) const {
    CHECK_NOTNULL(cr.spec);
    if (cr.spec->position.empty() || is_local_cr(cr.spec->position)) {
        return check_local_ability_cr_validity(cr);
    }
    return check_remote_ability_cr_validity(cr);
}

namespace {
expected<void, std::string> check_remote_device_cr_validity(const DeviceCR& cr) {
    // 发送post请求
    nlohmann::json payload = cr;
    auto res
        = send_post_request(cr.spec.position, "/api/resourcemgr/check_device_cr_validity", payload);
    if (!res.has_value()) { return unexpected("post error"); }
    nlohmann::json json_res = nlohmann::json::parse(res.value());
    if (json_res["result"] == "success") { return {}; }
    return unexpected("remote check cr validity error");
}

expected<std::filesystem::path, ErrorMsg> get_device_crd_path(const DeviceCR& cr) {
    std::filesystem::path crd_path = global_vars::home_path() / "packages" / cr.spec.package
                                   / to_string(cr.spec.version) / "crds"
                                   / (cr.spec.deviceName + ".crd.yaml");
    if (!exists(crd_path)) {
        LOG(ERROR) << "device: " << cr.metadata.name << " is not supported, file "
                   << crd_path.string() << " does not exist";
        return make_unexpected(
            "device: ", cr.metadata.name, " is not supported, file", crd_path.string(),
            " does not exist"
        );
    }

    return crd_path;
}
} // namespace

expected<void, std::string> ResourceManager::check_local_device_cr_validity(const DeviceCR& cr
) const {
    using namespace nlohmann;
    return get_device_crd_path(cr)
        .and_then(read_yaml_from_path)
        .transform(yaml_to_json)
        .and_then([&cr](const json& crd_schema) {
            json cr_json = cr;
            return validate_to_expected(cr_json, crd_schema);
        });
}

expected<void, std::string> ResourceManager::check_device_cr_validity(const DeviceCR& cr) const {
    if (cr.spec.position.empty() || is_local_cr(cr.spec.position)) {
        return check_local_device_cr_validity(cr);
    }
    return check_remote_device_cr_validity(cr);
}

void ResourceManager::show() const {}

template <typename T>
T decay_copy(T&& x) {
    return std::forward<T>(x);
}

void ResourceManager::update() {
    LOG(INFO) << "ResourceManager::update()";
    std::lock_guard _lk(m);
    auto packages_path = framework_home / "packages";
    packages.clear();
    for (auto&& pkg : recursively_search_packages(packages_path)) {
        packages.push_back(pkg);
        for (auto& [id, ability_crd] : pkg.abilities) {
            add_ability_crd(ability_crd);
        }
        for (auto& [id, device_crd] : pkg.devices) {
            add_device_crd(device_crd);
        }
        // 幂等地把包内嵌的 CR 和 skill 镜像出来。install 路径已经在 add_package /
        // extract_package 里调过, 但对于 framework 启动时已经存在的旧包 (没走过
        // install 路径), 这里补一次, 让 packages 目录是 source of truth。
        StoreManager::mirror_package_crs(pkg.path, pkg.name, to_string(pkg.version));
        StoreManager::mirror_package_skills(pkg.path, pkg.name, to_string(pkg.version));
    }
    read_cr_in_framework_cr_dir(global_vars::home_path() / "crs");

    auto_start_ability();
}

template <typename T, typename F>
auto operator>>=(std::optional<T>&& op, F&& f) {
    if (op.has_value()) { return std::invoke(std::forward<F>(f), std::move(op).value()); }
    return std::nullopt;
}

constexpr char KIND_ABILITY[] = "ability";
constexpr char KIND_DEVICE[] = "device";

// CREATE TABLE IF NOT EXISTS CROwnership(
//   parent_id TEXT NOT NULL, -- 依赖者id
//   parent_kind TEXT NOT NULL, -- "ability" 或 "device"
//   parent_address TEXT NOT NULL, -- 依赖者地址, 默认为 localhost
//   child_id TEXT NOT NULL, -- 被依赖者id
//   child_kind TEXT NOT NULL, -- 被依赖者类型, "ability" 或 "device"
//   is_shared INTEGER NOT NULL, -- 0 表示共享, 1 表示独占
//   PRIMARY KEY (parent_id, parent_kind, child_id, child_kind)
// );
std::unordered_map<uuids::uuid, std::string> db_execute_to_id_str_map(SQLite::Statement& stmt) {
    std::unordered_map<uuids::uuid, std::string> res;
    while (stmt.executeStep()) {
        std::string par_id_str = stmt.getColumn(0);
        std::string addr = stmt.getColumn(1);
        auto opt_id = uuids::uuid::from_string(par_id_str);
        if (!opt_id) { continue; }
        res[*opt_id] = std::move(addr);
    }
    return res;
}
std::unordered_map<uuids::uuid, std::string> ResourceManager::get_ability_sharers(
    const uuids::uuid& abilityInstance_id
) {
    constexpr char SQL[] = R"sql(SELECT parent_id, parent_address FROM CROwnership 
      WHERE parent_kind = ?
        and child_id = ? 
        and child_kind = ? 
        and is_shared = ?;)sql";
    auto stmt = database_mgr::statement(SQL);
    auto id_str = to_string(abilityInstance_id);
    stmt.bind(1, KIND_ABILITY);
    stmt.bind(2, id_str);
    stmt.bind(3, KIND_ABILITY);
    stmt.bind(4, static_cast<int>(OwnershipMode::shared));
    return db_execute_to_id_str_map(stmt);
}

std::unordered_map<uuids::uuid, std::string> ResourceManager::get_device_sharers(
    const uuids::uuid& deviceInstance_id
) {
    constexpr char SQL[] = R"sql(SELECT parent_id, parent_address FROM CROwnership 
      WHERE parent_kind = ?
        and child_id = ? 
        and child_kind = ? 
        and is_shared = ?;)sql";
    auto stmt = database_mgr::statement(SQL);
    auto id_str = to_string(deviceInstance_id);
    stmt.bind(1, KIND_DEVICE);
    stmt.bind(2, id_str);
    stmt.bind(3, KIND_DEVICE);
    stmt.bind(4, static_cast<int>(OwnershipMode::shared));
    return db_execute_to_id_str_map(stmt);
}

auto ResourceManager::get_ability_owner(const uuids::uuid& abilityInstance_id
) -> std::optional<OwnerInfo> {
    constexpr char SQL[] = R"sql(SELECT parent_id, parent_address FROM CROwnership 
      WHERE parent_kind = ?
        and child_id = ? 
        and child_kind = ? 
        and is_shared = ?;)sql";
    auto stmt = database_mgr::statement(SQL);
    auto id_str = to_string(abilityInstance_id);
    stmt.bind(1, KIND_ABILITY);
    stmt.bind(2, id_str);
    stmt.bind(3, KIND_ABILITY);
    stmt.bind(4, static_cast<int>(OwnershipMode::unique));
    while (stmt.executeStep()) {
        std::string par_id_str = stmt.getColumn(0);
        std::string addr = stmt.getColumn(1);
        auto opt_id = uuids::uuid::from_string(par_id_str);
        if (!opt_id) { continue; }
        return OwnerInfo{*opt_id, std::move(addr)};
    }
    return {};
}
auto ResourceManager::get_device_owner(const uuids::uuid& deviceInstance_id
) -> std::optional<OwnerInfo> {
    constexpr char SQL[] = R"sql(SELECT parent_id, parent_address FROM CROwnership 
      WHERE parent_kind = ?
        and child_id = ? 
        and child_kind = ? 
        and is_shared = ?;)sql";
    auto stmt = database_mgr::statement(SQL);
    auto id_str = to_string(deviceInstance_id);
    stmt.bind(1, KIND_DEVICE);
    stmt.bind(2, id_str);
    stmt.bind(3, KIND_DEVICE);
    stmt.bind(4, static_cast<int>(OwnershipMode::unique));
    while (stmt.executeStep()) {
        std::string par_id_str = stmt.getColumn(0);
        std::string addr = stmt.getColumn(1);
        auto opt_id = uuids::uuid::from_string(par_id_str);
        if (!opt_id) { continue; }
        return OwnerInfo{*opt_id, std::move(addr)};
    }
    return {};
}

// CREATE TABLE IF NOT EXISTS CROwnership(
//   parent_id TEXT NOT NULL, -- 依赖者id
//   parent_kind TEXT NOT NULL, -- "ability" 或 "device"
//   parent_address TEXT NOT NULL, -- 依赖者地址, 默认为 localhost
//   child_id TEXT NOT NULL, -- 被依赖者id
//   child_kind TEXT NOT NULL, -- 被依赖者类型, "ability" 或 "device"
//   is_shared INTEGER NOT NULL, -- 0 表示共享, 1 表示独占
//   PRIMARY KEY (parent_id, parent_kind, child_id, child_kind)
// );
bool ResourceManager::add_ability_sharers(
    const uuids::uuid& id, const uuids::uuid& abilityInstance_id, const std::string& position
) {
    constexpr char SQL[] = R"sql(REPLACE INTO CROwnership 
      (parent_id, parent_kind, parent_address, child_id, child_kind, is_shared) 
    VALUES (?, ?, ?, ?, ?, ?);)sql";
    auto stmt = database_mgr::statement(SQL);
    stmt.bind(1, to_string(id));
    stmt.bind(2, KIND_ABILITY);
    stmt.bind(3, position);
    auto id_str = to_string(abilityInstance_id);
    stmt.bind(4, id_str);
    stmt.bind(5, KIND_ABILITY);
    stmt.bind(6, static_cast<int>(OwnershipMode::shared));
    stmt.exec();
    return true;
}

bool ResourceManager::add_device_sharers(
    const uuids::uuid& id, const uuids::uuid& deviceInstance_id, const std::string& position
) {
    constexpr char SQL[] = R"sql(REPLACE INTO CROwnership 
      (parent_id, parent_kind, parent_address, child_id, child_kind, is_shared) 
    VALUES (?, ?, ?, ?, ?, ?);)sql";
    auto stmt = database_mgr::statement(SQL);
    stmt.bind(1, to_string(id));
    stmt.bind(2, KIND_ABILITY);
    stmt.bind(3, position);
    auto id_str = to_string(deviceInstance_id);
    stmt.bind(4, id_str);
    stmt.bind(5, KIND_DEVICE);
    stmt.bind(6, static_cast<int>(OwnershipMode::shared));
    stmt.exec();
    return true;
}

bool ResourceManager::add_ability_owner(
    const uuids::uuid& id, const uuids::uuid& abilityInstance_id, const std::string& position
) {
    constexpr char SQL[] = R"sql(REPLACE INTO CROwnership 
      (parent_id, parent_kind, parent_address, child_id, child_kind, is_shared) 
    VALUES (?, ?, ?, ?, ?, ?);)sql";
    auto stmt = database_mgr::statement(SQL);
    stmt.bind(1, to_string(id));
    stmt.bind(2, KIND_ABILITY);
    stmt.bind(3, position);
    auto id_str = to_string(abilityInstance_id);
    stmt.bind(4, id_str);
    stmt.bind(5, KIND_ABILITY);
    stmt.bind(6, static_cast<int>(OwnershipMode::unique));
    stmt.exec();
    return true;
}

bool ResourceManager::add_device_owner(
    const uuids::uuid& id, const uuids::uuid& deviceInstance_id, const std::string& position
) {
    constexpr char SQL[] = R"sql(REPLACE INTO CROwnership 
      (parent_id, parent_kind, parent_address, child_id, child_kind, is_shared) 
    VALUES (?, ?, ?, ?, ?, ?);)sql";
    auto stmt = database_mgr::statement(SQL);
    stmt.bind(1, to_string(id));
    stmt.bind(2, KIND_ABILITY);
    stmt.bind(3, position);
    auto id_str = to_string(deviceInstance_id);
    stmt.bind(4, id_str);
    stmt.bind(5, KIND_DEVICE);
    stmt.bind(6, static_cast<int>(OwnershipMode::unique));
    stmt.exec();
    return true;
}
namespace {

void _impl_remove_ownership(
    const uuids::uuid& parent_id,
    const uuids::uuid& child_id,
    const char* child_kind,
    OwnershipMode mode
) {
    constexpr char SQL[] = R"sql(DELETE FROM CROwnership 
      WHERE parent_id = ?  AND child_id = ? 
        AND child_kind = ? AND is_shared = ?;)sql";
    auto stmt = database_mgr::statement(SQL);
    stmt.bind(1, to_string(parent_id));
    auto id_str = to_string(child_id);
    stmt.bind(2, id_str);
    stmt.bind(3, child_kind);
    stmt.bind(4, static_cast<int>(mode));
    stmt.exec();
}
} // namespace

bool ResourceManager::remove_ability_sharer(
    const uuids::uuid& id, const uuids::uuid& abilityInstance_id
) {
    _impl_remove_ownership(id, abilityInstance_id, KIND_ABILITY, OwnershipMode::shared);
    return true;
}

bool ResourceManager::remove_device_sharer(
    const uuids::uuid& id, const uuids::uuid& deviceInstance_id
) {
    _impl_remove_ownership(id, deviceInstance_id, KIND_DEVICE, OwnershipMode::shared);
    return true;
}

bool ResourceManager::remove_ability_owner(
    const uuids::uuid& id, const uuids::uuid& abilityInstance_id
) {
    _impl_remove_ownership(id, abilityInstance_id, KIND_ABILITY, OwnershipMode::unique);
    return true;
}

bool ResourceManager::remove_device_owner(
    const uuids::uuid& id, const uuids::uuid& deviceInstance_id
) {
    _impl_remove_ownership(id, deviceInstance_id, KIND_DEVICE, OwnershipMode::unique);
    return true;
}

namespace {
expected<void, std::string> send_http_occupy_request(
    const std::string position,
    uuids::uuid occupy_ability_id,
    std::string occupy_ability_position,
    uuids::uuid occupied_ability_id,
    OwnershipMode mode
) {
    nlohmann::json payload;
    payload["occupy_ability_id"] = occupy_ability_id;
    payload["occupy_ability_position"] = occupy_ability_position;
    payload["occupied_ability_id"] = occupied_ability_id;
    payload["OwnershipMode"] = mode;
    auto res = send_post_request(position, "/api/resourcemgr/occupy_ability", payload);
    if (!res.has_value()) { return unexpected("post error"); }
    nlohmann::json json_res = nlohmann::json::parse(res.value());
    if (json_res["result"] == "success") { return {}; }
    return unexpected("remote occupy ability error");
}
} // namespace

expected<void, std::string> ResourceManager::occupy_ability(
    uuids::uuid occupy_ability_id,
    std::string position,
    uuids::uuid occupied_ability_id,
    OwnershipMode mode
) {
    if (!judge_ability_exist(occupied_ability_id))
        return unexpected{"can't occupy an ability that does not exist"};
    // resolve 同时查 AbilityInstance.spec_snapshot 和 AbilityCRBasic, 支持 instance_id
    auto cr_res = resolve_ability_cr_by_id(occupied_ability_id);
    if (!cr_res || !cr_res->spec) {
        return unexpected{"ability cr spec not available for " + to_string(occupied_ability_id)};
    }
    auto& cr = *cr_res;
    if (!is_local_cr(cr.spec->position)) {
        return send_http_occupy_request(
            cr.spec->position, occupy_ability_id, position, occupied_ability_id, mode
        );
    }
    // 是本地能力
    auto it = get_ability_owner(occupied_ability_id);
    if (it.has_value()) {
        if (it->id == occupy_ability_id) { return {}; }
        if (mode == OwnershipMode::shared) {
            return unexpected{"can't add sharers to unique object"};
        }
        else if (mode == OwnershipMode::unique) {
            return unexpected{"can't add owner to unique object, it is already occupied"};
        }
        else { return unexpected{"invalid ownership mode" + std::to_string((int)mode)}; }
    }
    auto sharers = get_ability_sharers(occupied_ability_id);
    if (!sharers.empty() && mode == OwnershipMode::unique) {
        return unexpected{"can't add owner to shared object"};
    }

    if (mode == OwnershipMode::shared) {
        if (sharers.find(occupy_ability_id) != sharers.end()) { return {}; }
        add_ability_sharers(occupy_ability_id, occupied_ability_id, position);
        return {};
    }
    add_ability_owner(occupy_ability_id, occupied_ability_id, position);
    return {};
}

expected<void, std::string> occupy_remote_device(
    const DeviceCR& cr,
    uuids::uuid occupy_device_id,
    const std::string& position,
    uuids::uuid occupied_device_id,
    OwnershipMode mode
) {
    // 对于远程设备,向对应框架发送请求
    nlohmann::json payload;
    payload["occupy_device_id"] = occupy_device_id;
    payload["occupy_device_position"] = position;
    payload["occupied_device_id"] = occupied_device_id;
    payload["OwnershipMode"] = mode;
    auto res = send_post_request(cr.spec.position, "/api/resourcemgr/occupy_device", payload);
    if (!res.has_value()) { return unexpected("post error"); }
    nlohmann::json json_res = nlohmann::json::parse(res.value());
    if (json_res["result"] == "success") { return {}; }
    return unexpected("remote occupy device error");
}

expected<void, std::string> ResourceManager::occupy_device(
    uuids::uuid occupy_device_id,
    std::string position,
    uuids::uuid occupied_device_id,
    OwnershipMode mode
) {
    if (!judge_device_exist(occupied_device_id)) {
        return unexpected{"can't occupy a device that does not exist"};
    }
    auto cr_res = get_device_cr(occupied_device_id);
    auto& cr = cr_res.value();
    if (!is_local_cr(cr.spec.position)) {
        return occupy_remote_device(cr, occupy_device_id, position, occupied_device_id, mode);
    }
    auto it = get_device_owner(occupied_device_id);
    if (it.has_value()) {
        if (it->id == occupy_device_id) { return {}; }
        if (mode == OwnershipMode::shared) {
            return unexpected{"can't add sharers to unique object"};
        }
        else if (mode == OwnershipMode::unique) {
            return unexpected{"can't add owner to unique object, it is already occupied"};
        }
        else { return unexpected{"invalid ownership mode" + std::to_string((int)mode)}; }
    }
    auto sharers = get_ability_sharers(occupied_device_id);
    if (!sharers.empty() && mode == OwnershipMode::unique) {
        return unexpected{"can't add owner to shared object"};
    }

    if (mode == OwnershipMode::shared) {
        if (sharers.find(occupy_device_id) != sharers.end()) { return {}; }
        add_ability_sharers(occupy_device_id, occupied_device_id, position);
        return {};
    }
    add_ability_owner(occupy_device_id, occupied_device_id, position);
    return {};
}

bool ResourceManager::unoccupy_ability(
    uuids::uuid occupy_ability_id, uuids::uuid occupied_ability_id, OwnershipMode mode
) {
    if (!judge_ability_exist(occupied_ability_id)) return false;
    auto cr_res = resolve_ability_cr_by_id(occupied_ability_id);
    if (!cr_res || !cr_res->spec) { return false; }
    auto& cr = *cr_res;
    if (is_local_cr(cr.spec->position)) {
        if (mode == OwnershipMode::shared)
            return remove_ability_sharer(occupy_ability_id, occupied_ability_id);
        else
            return remove_ability_owner(occupy_ability_id, occupied_ability_id);
    }
    // 是远程CR
    nlohmann::json payload;
    payload["occupy_ability_id"] = occupy_ability_id;
    payload["occupied_ability_id"] = occupied_ability_id;
    payload["mode"] = mode;
    auto res = send_post_request(cr.spec->position, "/api/resourcemgr/unoccupy_ability", payload);
    if (!res.has_value()) { return false; }
    nlohmann::json json_res = nlohmann::json::parse(res.value());
    return json_res["result"] == "success";
}

bool ResourceManager::unoccupy_device(
    uuids::uuid occupy_device_id, uuids::uuid occupied_device_id, OwnershipMode mode
) {
    if (!judge_device_exist(occupied_device_id)) { return false; }
    auto cr_res = get_device_cr(occupied_device_id);
    auto& cr = cr_res.value();
    if (is_local_cr(cr.spec.position)) {
        if (mode == OwnershipMode::shared)
            return remove_device_sharer(occupy_device_id, occupied_device_id);
        else
            return remove_device_owner(occupy_device_id, occupied_device_id);
    }
    nlohmann::json payload;
    payload["occupy_device_id"] = occupy_device_id;
    payload["occupied_device_id"] = occupied_device_id;
    payload["mode"] = mode;
    auto res = send_post_request(cr.spec.position, "/api/resourcemgr/unoccupy_device", payload);
    if (!res.has_value()) { return false; }
    nlohmann::json json_res = nlohmann::json::parse(res.value());
    return json_res["result"] == "success";
}

std::string ResourceManager::module_name() const {
    return "ResourceMgr";
}

expected<msg_params::AbilityStorageInfo, std::string> ResourceManager::find_ability_storage_info(
    uuids::uuid instance_id
) {
    // 优先从 AbilityInstance.spec_snapshot 解析 (实例已经创建，CR 是模板拷贝)
    // 回退到模板表（兼容旧路径 / 子能力 id）
    auto it = resolve_ability_cr_by_id(instance_id);
    if (!it) return unexpected{"no storage info for ability " + to_string(instance_id)};

    const auto& abilityCR = *it;
    auto package_path = framework_home / "packages" / abilityCR.spec->package
                      / to_string(abilityCR.spec->version);

    return msg_params::AbilityStorageInfo{
        .package_path = package_path,
        .executable_path = package_path / "bin" / "ability",
        .controller_path = ""
    };
}

bool is_matched(const AbilityCR& cr, const AbilityCRD& crd) {
    return crd.metadata.name == cr.spec->abilityName && crd.packageName == cr.spec->package
        && crd.version == cr.spec->version;
}

std::optional<msg_params::AbilityClassInfo> ResourceManager::find_ability_id(ID abilityInstance_id
) {
    auto it = resolve_ability_cr_by_id(abilityInstance_id);
    if (!it) { return {}; }
    std::lock_guard _lk(m);
    auto& cr = *it;
    auto crd = get_ability_crd(cr.spec->abilityName, to_string(cr.spec->version));
    if (!crd) { return {}; }
    return msg_params::AbilityClassInfo{
        .abilityPackageName = crd->packageName, .abilityVersion = crd->version.to_string()
    };
}

bool ResourceManager::on_event_heartbeat(const Heartbeat& hb) {
    // 同步更新 AbilityInstance 表
    auto id_str = to_string(hb.id);
    auto state_str = std::string(to_string(hb.state));

    // 实例退出语义: 进入 Terminated 即销毁行（不论 singleton 或 replica）
    if (hb.state == LifecycleState::Terminated) {
        delete_ability_instance(hb.id);
        return true;
    }

    nlohmann::json detail;
    detail["abilityPort"] = hb.abilityPort;
    detail["IPCPort"] = hb.IPCPort;
    auto detail_str = detail.dump();

    // 只接受已在 AbilityInstance 表里注册的实例的心跳。
    //
    // 历史上这里有一条 fallback: UPDATE 命中 0 行时 INSERT 一条 "孤儿心跳"
    // 占位行。那条 fallback 在 framework 重启后会帮上一代 python 僵尸进程
    // (被 reparent 到 init, PR_SET_PDEATHSIG 已经过期不再 fire) 造出 ghost
    // 实例 — 用户看到一个与任何 CR 模板都不相关、spec_snapshot 为 null 的
    // Running 行, 调试噩梦。
    //
    // 修法: 直接 drop orphan 心跳, 返回 false 让 LifecycleMgr 的 HTTP 层回
    // 410 Gone, SDK 侧连续收到 410 后自毁 (见 ability-py-sdk heartbeat.py)。
    constexpr char SQL_UPDATE[] =
        "UPDATE AbilityInstance SET state = ?, detail = ? WHERE instance_id = ?";
    auto upd = database_mgr::statement(SQL_UPDATE);
    upd.bind(1, state_str);
    upd.bind(2, detail_str);
    upd.bind(3, id_str);
    if (upd.exec() > 0) { return true; }

    LOG_FIRST_N(WARNING, 20)
        << "dropping orphan heartbeat from unknown instance " << id_str
        << " ability=" << hb.abilityName
        << " state=" << state_str
        << "; sender will be told to self-terminate";
    return false;
}

expected<nlohmann::json, std::string> ResourceManager::on_heartbeat_event(Heartbeat hb) {
    bool accepted = on_event_heartbeat(hb);
    return nlohmann::json{{"accepted", accepted}};
}

bool is_ability_running(uuids::uuid id) {
    scope_fail _p([id]() {
        LOG(WARNING) << "when checking whether ability " << id << " is running";
    });
    auto res = send_sync(
        make_message("ResourceMgr", "LifecycleMgr", "get_heartbeat/id", nlohmann::json(id))
    );
    if (!res) {
        LOG(ERROR) << "failed to send message to LifecycleMgr: " << res.error();
        return false;
    }
    if (!res->success()) {
        LOG(ERROR) << "error response: " << res->view();
        return false;
    }
    auto j = nlohmann::json::parse(res->view());
    if (j.is_null()) { return false; }
    Heartbeat hb = j.get<Heartbeat>();
    return is_active(hb.state);
};

TaskPtr ResourceManager::task_download_package(PackageSpec pkg_spec) {
    return tasks::atomic("download-package", [this, pkg_spec]() {
        auto res_download = store_mgr.download_package(pkg_spec);
        if (res_download.empty()) { throw std::runtime_error("download package failed"); }
        auto res_extract = store_mgr.extract_package(pkg_spec, res_download);
        if (!res_extract) { throw make_error("extract file failed: ", res_extract.error()); }
        update();
    });
}

bool db_get_ability_cr_is_autostart(const std::string& id) {
    constexpr char SQL[] = "SELECT autostart FROM AbilityCRBasic WHERE instance_id = ?";
    auto stmt = database_mgr::statement(SQL);
    stmt.bind(1, id);
    while (stmt.executeStep()) {
        int res = stmt.getColumn(0);
        return res == 1;
    }
    return false;
}

// autoStart: 仅框架启动时自动拉起一次（auto_started=0 的）
std::vector<std::string> db_get_ability_cr_need_autostart() {
    std::vector<std::string> res;
    constexpr char SQL[] =
        "SELECT instance_id FROM AbilityCRBasic WHERE autostart = 1 AND (auto_started IS NULL OR auto_started = 0)";
    auto stmt = database_mgr::statement(SQL);
    while (stmt.executeStep()) {
        res.push_back(stmt.getColumn(0).getString());
    }
    return res;
}

// keepAlive: 持续保活（能力退出后自动重启）
std::vector<std::string> db_get_ability_cr_keep_alive() {
    std::vector<std::string> res;
    constexpr char SQL[] = "SELECT instance_id FROM AbilityCRBasic WHERE keep_alive = 1";
    auto stmt = database_mgr::statement(SQL);
    while (stmt.executeStep()) {
        res.push_back(stmt.getColumn(0).getString());
    }
    return res;
}

void db_mark_auto_started(const std::string& instance_id) {
    constexpr char SQL[] = "UPDATE AbilityCRBasic SET auto_started = 1 WHERE instance_id = ?";
    auto stmt = database_mgr::statement(SQL);
    stmt.bind(1, instance_id);
    stmt.exec();
}

void ResourceManager::auto_start_ability() {
    VLOG(1) << "auto_start_ability";

    // 从模板派生一个新实例并发起 lifecycle start
    // template_id: AbilityCRBasic.instance_id (模板)
    // 返回新创建的 instance_id, 失败时为空
    auto spawn_instance_from_template = [&](ID template_id, const char* reason) -> std::optional<ID> {
        scope_fail _p([template_id]() {
            LOG(WARNING) << "when starting from template " << template_id;
        });

        auto template_opt = get_ability_cr(template_id);
        if (!template_opt || !template_opt->spec) {
            LOG(WARNING) << reason << ": template " << to_string(template_id) << " not found";
            return std::nullopt;
        }
        auto& tmpl = *template_opt;

        // 单例约束: 同 ability_name 已有 active 实例 → 跳过
        if (tmpl.spec->singleton.value_or(true)) {
            auto active = get_active_instances_by_ability_name(tmpl.spec->abilityName);
            if (!active.empty()) {
                LOG_IF(WARNING, std::string{reason} == "autoStart")
                    << reason << " skipped: singleton " << tmpl.spec->abilityName
                    << " already has running instance(s)";
                return std::nullopt;
            }
        }

        // 重试限流（按模板 id 计数，避免单模板崩溃后无限重试）
        auto now = std::chrono::steady_clock::now();
        auto& rec = start_records[template_id];
        if (rec.retry_count >= 3) {
            LOG(WARNING) << "template " << to_string(template_id) << " reached max retry, skip";
            return std::nullopt;
        }
        if (rec.retry_count > 0 && now - rec.last_start < std::chrono::seconds(20)) {
            return std::nullopt;
        }
        rec.last_start = now;
        rec.retry_count++;

        auto instance_id = create_ability_instance(tmpl);
        LOG(INFO) << reason << " spawn instance " << to_string(instance_id) << " from template "
                  << tmpl.metadata.name;

        auto task_1 = tasks::atomic("start-ability", [instance_id]() {
            LifecycleRequest payload{.abilityInstanceId = instance_id, .command = "start"};
            auto res = send_sync(
                make_message("ResourceMgr", "LifecycleMgr", "lifecycle_request", payload)
            );
            on_error(res, [](std::string_view err_msg) {
                LOG(ERROR) << "lifecycle_request start failed: " << err_msg;
            });
        });
        auto task_2 = tasks::wait_once(
            "wait-heartbeat",
            [instance_id]() { return is_ability_running(instance_id); },
            std::chrono::seconds(15),
            [instance_id]() {
                std::ostringstream oss;
                oss << "wait instance heartbeat " << to_string(instance_id) << " timeout ";
                LOG(ERROR) << oss.str();
                return oss.str();
            }
        );
        auto task_3 = tasks::atomic("connect-ability", [instance_id]() {
            LifecycleRequest payload{.abilityInstanceId = instance_id, .command = "connect"};
            auto res = send_sync(
                make_message("ResourceMgr", "LifecycleMgr", "lifecycle_request", payload)
            );
            on_error(res, [](std::string_view err_msg) {
                LOG(ERROR) << "lifecycle_request connect failed: " << err_msg;
            });
        });
        auto total_task = tasks::sequence("lifecycle-adjust", task_1, task_2, task_3);
        submit_task(total_task, module_name());
        return instance_id;
    };

    // autoStart: 框架启动后仅一次
    for (const auto& id_str : db_get_ability_cr_need_autostart()) {
        std::optional id = uuids::uuid::from_string(id_str);
        if (!id) { continue; }
        db_mark_auto_started(id_str);
        spawn_instance_from_template(*id, "autoStart");
    }

    // keepAlive: 持续保活；当模板没有任何 active 实例时派生一条
    for (const auto& id_str : db_get_ability_cr_keep_alive()) {
        std::optional id = uuids::uuid::from_string(id_str);
        if (!id) { continue; }
        auto template_opt = get_ability_cr(*id);
        if (!template_opt || !template_opt->spec) { continue; }
        auto active = get_active_instances_by_ability_name(template_opt->spec->abilityName);
        if (!active.empty()) { continue; }
        spawn_instance_from_template(*id, "keepAlive");
    }
}

void build_api_service(std::shared_ptr<ResourceManager> mgr, httplib::Server& server);
void build_api_instance(std::shared_ptr<ResourceManager> mgr, httplib::Server& server);

void build_api(std::shared_ptr<ResourceManager> mgr, httplib::Server& server) {
    CHECK_NOTNULL(mgr);
    build_api_cr(mgr, server);
    build_api_crd(mgr, server);
    build_api_occupation(mgr, server);
    build_api_service(mgr, server);
    build_api_instance(mgr, server);
}

#define CHECK_TASK_PARAM(_p_Name, _p_Pred)                                                    \
    if (!params.contains(_p_Name)) { throw std::invalid_argument("missing param " _p_Name); } \
    if (!params.at(_p_Name)._p_Pred()) {                                                      \
        throw std::invalid_argument(strjoin(                                                  \
            "invalid param, expected it " #_p_Pred ", but get " + params.at(_p_Name).dump()   \
        ));                                                                                   \
    }

bool exists_package(const std::string& package_name, const std::string& version) {
    auto path = global_vars::packages_path() / package_name / version;
    return exists(path);
}
TaskPtr task_remove_package(PackageSpec pkg) {
    std::string task_name = strjoin("remove old package ", pkg.package, ' ', pkg.version);
    return tasks::atomic(task_name, [pkg]() {
        auto dir = global_vars::packages_path() / pkg.package;
        if (!pkg.version.empty()) { dir /= pkg.version; }
        remove_all(dir);
    });
}

struct InstallPackageTaskFactory {
    std::shared_ptr<ResourceManager> mgr;
    TaskPtr operator()(const nlohmann::json& params) const {
        CHECK_TASK_PARAM("package_name", is_string);
        CHECK_TASK_PARAM("package_version", is_string);
        std::string package_name = params.at("package_name");
        std::string package_version = params.at("package_version");
        if (params.contains("install_debug_version")) {
            LOG(WARNING) << "install_debug_version param is currently ignored";
        }
        const bool override_local_package = params.contains("override_local_package")
                                         && static_cast<bool>(params.at("override_local_package"));
        const bool exist_pkg = exists_package(package_name, package_version);
        if (exist_pkg) {
            LOG(WARNING) << "already exist package " << package_name << ' ' << package_version;
            if (!override_local_package) {
                throw std::invalid_argument(strjoin(
                    "package ", package_name, ' ', package_version,
                    " already exists, add param 'override_local_package = true' to force reinstall "
                    "it"
                ));
            }
            LOG(WARNING) << "force reinstall package " << package_name << ' ' << package_version;
        }
        PackageSpec pkg{package_name, package_version};
        if (!exist_pkg) { return mgr->task_download_package(pkg); }
        std::vector<TaskPtr> tasks;
        // 1 停止相关能力
        // TODO
        LOG(WARNING) << "during install package " << pkg.package << ' ' << pkg.version
                     << ", step of stopping relevant abilities is not implemented";
        // 2 卸载原有包
        tasks.push_back(task_remove_package(pkg));
        // 3 安装包
        tasks.push_back(mgr->task_download_package(pkg));
        auto task_name = strjoin("override install package ", package_name, ' ', package_version);
        return tasks::sequence(task_name, tasks);
    }
};

struct UninstallPackageTaskFactory {
    TaskPtr operator()(const nlohmann::json& params) const {
        CHECK_TASK_PARAM("package_name", is_string);
        std::string package_name = params.at("package_name");
        std::string package_version = params.value("package_version", "");
        PackageSpec pkg{package_name, package_version};
        return task_remove_package(pkg);
    }
};

void ResourceManager::on_register() {
    Super::on_register();
    LOG(INFO) << module_name() << "::on_register";
    Super::on_register();
    task_mgr::add_task_factory(
        "package.install_package", InstallPackageTaskFactory(shared_from_this())
    );
    task_mgr::add_task_factory("model.install_model", InstallModelByIdTaskFactory(model_mgr));
    task_mgr::add_task_factory("package.install_package", UninstallPackageTaskFactory());

    // 框架启动时清理 AbilityInstance 表中的非终止状态遗留行。
    // 框架重启后无法验证旧实例是否存活（PR_SET_PDEATHSIG + SubprocessMgr 已经
    // 杀掉了实际的孤儿进程），所以应当将所有非 Terminated 的行标记为
    // Terminated，避免:
    //   1. 单例检查 get_heartbeats_by_ability_name 误以为有运行中实例
    //   2. WebUI '运行实例' 面板显示陈旧的 Running 行
    {
        std::lock_guard _lk(m);
        auto now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch()).count();
        constexpr char SQL[] = R"sql(
            UPDATE AbilityInstance
            SET state = 'Terminated', stop_time = ?
            WHERE state NOT IN ('Terminated', 'Inactive')
        )sql";
        try {
            auto stmt = database_mgr::statement(SQL);
            stmt.bind(1, static_cast<int64_t>(now));
            stmt.exec();
            LOG(INFO) << "cleaned up stale AbilityInstance rows on startup";
        } catch (const std::exception& e) {
            LOG(WARNING) << "failed to clean stale AbilityInstance rows: " << e.what();
        }

        // 框架启动时重置 auto_started 标志: autoStart 的语义是"每次框架启动
        // 时自动拉起一次"，而不是"该 CR 整个生命周期内只拉起一次"。crash
        // 防护由 in-memory start_records 的 retry_count<3 在本会话内提供。
        try {
            auto stmt = database_mgr::statement(
                "UPDATE AbilityCRBasic SET auto_started = 0 WHERE autostart = 1"
            );
            stmt.exec();
            LOG(INFO) << "reset auto_started flag for autostart CRs";
        } catch (const std::exception& e) {
            LOG(WARNING) << "failed to reset auto_started flag: " << e.what();
        }
    }
}

bool is_ability_cr_occupied(ResourceManager& mgr, uuids::uuid instance_id) {
    auto it = mgr.get_ability_owner(instance_id);
    return it.has_value();
}

bool is_device_cr_occupied(ResourceManager& mgr, uuids::uuid instance_id) {
    auto it = mgr.get_ability_owner(instance_id);
    return it.has_value();
}
bool is_cr_occupied(ResourceManager& mgr, uuids::uuid instance_id) {
    return is_ability_cr_occupied(mgr, instance_id) || is_device_cr_occupied(mgr, instance_id);

    auto it = mgr.get_ability_owner(instance_id);
    return it.has_value();
}
