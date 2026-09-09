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

#include "abilityalertmgr/abilityalert_mgr.hpp"
#include "glog/logging.h"
#include "prelude.hpp"
#include "util/global_vars.hpp"

#define GUARD_OK(msg)                                                           \
    if (rc != SQLITE_OK) {                                                      \
        LOG(ERROR) << "sqlite3 " << (msg) << " failed: " << sqlite3_errmsg(db); \
        return rc;                                                              \
    }
namespace {

auto bind_str(sqlite3_stmt* stmt, int n, std::string_view sv) {
    return sqlite3_bind_text(stmt, n, sv.data(), sv.size(), SQLITE_STATIC);
}

int insert(sqlite3* db, const AbilityAlert& alert) {
    // SQL 插入语句
    constexpr char sql[] = R"(
        INSERT INTO AbilityAlert (
            code, id, message, error_operation, level, input_time,
            hb_ability_name, hb_instance_name, hb_version, hb_lifecycle_state, 
            hb_ability_port, hb_ipc_port, hb_ipc_protocol, detail, context, 
            location_file, location_line, location_function
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";

    sqlite3_stmt* stmt;
    int rc;
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    GUARD_OK("prepare_statement");

    // 绑定参数
    rc = sqlite3_bind_int(stmt, 1, alert.code);
    GUARD_OK("bind alert.code");

    auto id_str = to_string(alert.abilityInfo.id);
    rc = bind_str(stmt, 2, id_str);
    GUARD_OK("bind id");

    rc = bind_str(stmt, 3, alert.message);
    GUARD_OK("bind message");

    rc = bind_str(stmt, 4, alert.error_operation);
    GUARD_OK("bind error_operation");

    auto alert_level_str = to_string(alert.level);
    rc = bind_str(stmt, 5, alert_level_str);
    GUARD_OK("bind level");

    rc = bind_str(stmt, 6, alert.time);
    GUARD_OK("bind time");

    rc = bind_str(stmt, 7, alert.abilityInfo.abilityName);
    GUARD_OK("bind abilityName");

    rc = bind_str(stmt, 8, alert.abilityInfo.instanceName);
    GUARD_OK("bind instanceName");

    rc = bind_str(stmt, 9, alert.abilityInfo.version);
    GUARD_OK("bind version");

    auto state_str = to_string(alert.abilityInfo.state);
    rc = bind_str(stmt, 10, state_str);
    GUARD_OK("bind lifecycle state");

    rc = sqlite3_bind_int(stmt, 11, alert.abilityInfo.abilityPort);
    GUARD_OK("bind abilityPort");

    rc = sqlite3_bind_int(stmt, 12, alert.abilityInfo.IPCPort);
    GUARD_OK("bind IPCPort");

    auto protocol_type_str = to_string(alert.abilityInfo.IPCProtocol);
    rc = bind_str(stmt, 13, protocol_type_str);
    GUARD_OK("bind IPCPort");

    auto detail_str = alert.detail.dump();
    rc = bind_str(stmt, 14, detail_str);
    GUARD_OK("bind detail");

    auto context_str = alert.context.dump();
    rc = bind_str(stmt, 15, context_str);
    GUARD_OK("bind context");

    rc = bind_str(stmt, 16, alert.location.file);
    GUARD_OK("bind location file");

    rc = sqlite3_bind_int(stmt, 17, alert.location.line);
    GUARD_OK("bind location line");

    rc = bind_str(stmt, 18, alert.location.function);
    GUARD_OK("bind location function");
    // 执行插入
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        std::string msg = sqlite3_errmsg(db);
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to insert data: " + msg);
    }

    // 释放语句
    sqlite3_finalize(stmt);
    return SQLITE_DONE;
}
uuids::uuid parse_id(std::string_view sv, std::string_view msg) {
    auto opt_id = uuids::uuid::from_string(sv);
    if (!opt_id) { throw std::invalid_argument(strjoin("invalid ", msg, " ", std::string(sv))); }
    return *opt_id;
}

uuids::uuid id_from_db_row(sqlite3_stmt* stmt, int n, std::string_view msg) {
    return parse_id(reinterpret_cast<const char*>(sqlite3_column_text(stmt, n)), msg);
}
std::string to_string_unless_null(auto* p) {
    return p ? reinterpret_cast<const char*>(p) : "";
};
template <typename T>
T parse_from_db_row(sqlite3_stmt* stmt, int n) {
    std::string s = to_string_unless_null(sqlite3_column_text(stmt, n));
    return from_sv<T>(s);
}

void from_db_row(sqlite3_stmt* stmt, AbilityAlert& out) {
    out.code = sqlite3_column_int(stmt, 0);
    out.abilityInfo.id = id_from_db_row(stmt, 1, "instance id");
    out.message = to_string_unless_null(sqlite3_column_text(stmt, 2));
    out.error_operation = to_string_unless_null(sqlite3_column_text(stmt, 3));

    out.level = parse_from_db_row<AbilityAlertLevel>(stmt, 4); // 具体转换逻辑根据实际需要调整

    out.time = to_string_unless_null(sqlite3_column_text(stmt, 5));

    out.abilityInfo.abilityName = to_string_unless_null(sqlite3_column_text(stmt, 6));
    out.abilityInfo.instanceName = to_string_unless_null(sqlite3_column_text(stmt, 7));
    out.abilityInfo.version = to_string_unless_null(sqlite3_column_text(stmt, 8));
    out.abilityInfo.state = parse_from_db_row<LifecycleState>(stmt, 9);
    out.abilityInfo.abilityPort = sqlite3_column_int(stmt, 10);
    out.abilityInfo.IPCPort = sqlite3_column_int(stmt, 11);
    out.abilityInfo.IPCProtocol = parse_from_db_row<ProtocolType>(stmt, 12);

    out.detail = nlohmann::json::parse(to_string_unless_null(sqlite3_column_text(stmt, 13)));
    out.context = nlohmann::json::parse(to_string_unless_null(sqlite3_column_text(stmt, 14)));
    out.location.file = to_string_unless_null(sqlite3_column_text(stmt, 15));
    out.location.line = sqlite3_column_int(stmt, 16);
    out.location.function = to_string_unless_null(sqlite3_column_text(stmt, 17));
}

/**
 * @param abilityInstaceId ,如果为空,表示提取所有能力的alert信息,如果不为空,则指定能力id
 * @param num_entries 表示输出条目的最大数量
 */
std::vector<AbilityAlert> query_ability_alerts(
    sqlite3* db, uuids::uuid abilityInstanceId = {}, int num_entries = 20
) {
    std::vector<AbilityAlert> alerts;
    sqlite3_stmt* stmt;

    // SQL 查询语句
    const char* sql = R"(
        SELECT code, id, message, error_operation, level, input_time,
               hb_ability_name, hb_instance_name, hb_version, hb_lifecycle_state,
               hb_ability_port, hb_ipc_port, hb_ipc_protocol, detail, context,
               location_file, location_line, location_function
        FROM AbilityAlert
        WHERE (id = ? OR ? IS NULL)
        ORDER BY input_time DESC
        LIMIT ?;
    )";

    // 准备 SQL 语句
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(strjoin("Failed to prepare statement: ", sqlite3_errmsg(db)));
    }
    std::string id_str;
    // 绑定参数
    if (abilityInstanceId.is_nil()) {
        sqlite3_bind_null(stmt, 1); // 能力 ID 为空
        sqlite3_bind_null(stmt, 2); // 用于检查NULL
    }
    else {
        id_str = to_string(abilityInstanceId);
        sqlite3_bind_text(stmt, 1, id_str.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, id_str.c_str(), -1, SQLITE_STATIC);
    }
    sqlite3_bind_int(stmt, 3, num_entries);

    // 执行查询
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        AbilityAlert alert;
        from_db_row(stmt, alert);
        alerts.push_back(alert);
    }

    // 清理
    sqlite3_finalize(stmt);
    return alerts;
}

void init_db(sqlite3* db) {
    CHECK_NOTNULL(db);
    LOG(INFO) << "initializing ability_alert.db";
    const char* sql = R"(
       create table if not exists AbilityAlert(
           code INT NOT NULL,
           id TEXT NOT NULL,
           message TEXT NOT NULL,
           error_operation TEXT NOT NULL,
           level TEXT NOT NULL,
           input_time TEXT NOT NULL,

           hb_ability_name TEXT NOT NULL,
           hb_instance_name TEXT NOT NULL,
           hb_version TEXT NOT NULL,
           hb_lifecycle_state TEXT NOT NULL,
           hb_ability_port INT NOT NULL,
           hb_ipc_port INT NOT NULL,
           hb_ipc_protocol INT NOT NULL,

           detail TEXT, -- json序列化之后存储
           context TEXT, -- json序列化之后存储
           location_file TEXT NOT NULL,
           location_line INT NOT NULL,
           location_function INT NOT NULL,
           PRIMARY KEY(id,input_time)
       ); 
    )";

    char* errorMessage = nullptr;
    int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errorMessage);
    if (rc != SQLITE_OK) {
        LOG(ERROR) << "sqlite3 create table AbilityAlert failed: " << errorMessage;
        sqlite3_free(errorMessage); // Free the error message
        throw SqliteError(db, rc);
    }
    LOG(INFO) << "Table AbilityAlert created or already exists.";
}

} // namespace

AbilityAlertManager::AbilityAlertManager() {
    auto db_path = global_vars::home_path() / "databases" / "ability_alert.db";
    if (!exists(db_path)) {
        LOG(INFO) << db_path << " not found, initializing db";
        db = open_db(db_path);
        init_db(db.get());
    }
    else {
        LOG(INFO) << db_path << " found, opening it";
        db = open_db(db_path);
    }
}

void AbilityAlertManager::push_back(const AbilityAlert& alert) {
    CHECK_NOTNULL(db);
    std::lock_guard _lk(m);
    int rc = insert(db.get(), alert);
    if (rc != SQLITE_OK && rc != SQLITE_DONE) { throw SqliteError(db.get(), rc); }
}

std::vector<AbilityAlert> AbilityAlertManager::query(
    uuids::uuid abilityInstanceId, int num_entries
) {
    std::lock_guard _lk(m);
    return query_ability_alerts(db.get(), abilityInstanceId, num_entries);
}

constexpr int DEFAULT_ALERT_NUM = 20;

void build_api(std::shared_ptr<AbilityAlertManager> mgr, httplib::Server& svr) {
    LOG(INFO) << "AbilityAlertMgr build api";
    CHECK_NOTNULL(mgr);
    using namespace httplib;
    svr.Post("/api/ability-alert", [mgr](const Request& req, Response& res) {
        auto j = nlohmann::json::parse(req.body);
        auto alert = j.get<AbilityAlert>();
        mgr->push_back(alert);
        res.set_content("OK", "text/plain");
    });
    svr.Get("/api/ability-alert", [mgr](const Request& req, Response& res) {
        auto id_str = req.get_param_value("instanceid");
        uuids::uuid id{};
        if (!id_str.empty()) {
            auto mb_id = uuids::uuid::from_string(id_str);
            if (!mb_id) { throw std::invalid_argument("invalid instance id " + id_str); }
            id = *mb_id;
        }
        int latest_n = [req]() {
            auto s = req.get_param_value("latest");
            if (s.empty()) { return DEFAULT_ALERT_NUM; }
            try {
                return stoi(s);
            }
            catch (std::exception& e) {
                LOG(ERROR) << "invalid latest num " << e.what() << " : " << s;
                throw;
            }
        }();
        auto alerts = mgr->query(id, latest_n);
        nlohmann::json payload(alerts);
        res.set_content(payload.dump(), "application/json");
    });
}
