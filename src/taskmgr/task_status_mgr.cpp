// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#include "taskmgr/task_status_mgr.hpp"
#include "messagebus/handler_util.hpp"
#include "prelude.hpp"
#include "util/global_vars.hpp"
#include "util/scope.hpp"
#include "util/sqlite_types.hpp"
#include <glog/logging.h>
#include <httplib.h>

namespace {

#define GUARD_OK(msg)                                                           \
    if (rc != SQLITE_OK) {                                                      \
        LOG(ERROR) << "sqlite3 " << (msg) << " failed: " << sqlite3_errmsg(db); \
        return rc;                                                              \
    }

#define GUARD_OK_OR_RETURN_NULLOPT(msg)                                         \
    if (rc != SQLITE_OK) {                                                      \
        LOG(ERROR) << "sqlite3 " << (msg) << " failed: " << sqlite3_errmsg(db); \
        return {};                                                              \
    }

///@return rc is the sqlite3 error code; OK means success, otherwise failure
int insert(sqlite3* db, const TaskStatus& ts) {
    const char* sql = R"(
        REPLACE INTO TaskStatus (id, executor_id, executor_type, state, start_time, end_time, input_time, timeout, payload,message)
        VALUES (?, ?, ?, ?, ?, ?, datetime('now'), ?, ?, ?);
    )";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    GUARD_OK("repare_statement");
    scope_exit clean_stmt([&stmt, &db]() {
        int rc = sqlite3_finalize(stmt);
        LOG_IF(ERROR, rc != SQLITE_OK) << "sqlite3 finalize" << " failed: " << sqlite3_errmsg(db);
    });

    auto id_str = to_string(ts.id);
    auto executor_id_str = to_string(ts.executor_id);
    // Binding the parameters
    rc = sqlite3_bind_text(stmt, 1, id_str.c_str(), -1, SQLITE_STATIC);
    GUARD_OK("bind id");
    rc = sqlite3_bind_text(stmt, 2, executor_id_str.c_str(), -1, SQLITE_STATIC);
    GUARD_OK("bind executor id");

    auto exec_type_str = to_string(ts.executor_type);
    rc = sqlite3_bind_text(stmt, 3, exec_type_str.c_str(), -1, SQLITE_STATIC);
    GUARD_OK("bind executor type");

    auto state_str = to_string(ts.state);
    rc = sqlite3_bind_text(stmt, 4, state_str.c_str(), -1, SQLITE_STATIC);
    GUARD_OK("bind state");

    rc = sqlite3_bind_text(
        stmt, 5, ts.start_time.empty() ? nullptr : ts.start_time.c_str(), -1, SQLITE_STATIC
    );
    GUARD_OK("bind start time");

    rc = sqlite3_bind_text(
        stmt, 6, ts.end_time.empty() ? nullptr : ts.end_time.c_str(), -1, SQLITE_STATIC
    );
    GUARD_OK("bind end time");

    rc = sqlite3_bind_int64(stmt, 7, ts.timeout.count());
    GUARD_OK("bind timeout");

    auto payload_str = ts.payload.dump();
    rc = sqlite3_bind_text(stmt, 8, payload_str.c_str(), -1, SQLITE_STATIC);
    GUARD_OK("bind payload");

    rc = sqlite3_bind_text(stmt, 9, ts.message.c_str(), -1, SQLITE_STATIC);
    GUARD_OK("bind message");

    // step never returns SQLITE_OK; on success it returns SQLITE_DONE
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        LOG(ERROR) << "sqlite3 " << "insert entry" << " failed: " << sqlite3_errmsg(db);
        return rc;
    }
    return rc;
}

uuids::uuid parse_id(std::string_view sv, std::string_view msg) {
    auto opt_id = uuids::uuid::from_string(sv);
    if (!opt_id) { throw std::invalid_argument(strjoin("invalid ", msg, " ", std::string(sv))); }
    return *opt_id;
}

std::string to_string_unless_null(auto* p) {
    return p ? reinterpret_cast<const char*>(p) : "";
};
void from_db_row(sqlite3_stmt* stmt, TaskStatus& ts) {
    CHECK_NOTNULL(stmt);

    ts.id = parse_id(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)), "id");
    ts.executor_id
        = parse_id(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)), "executor_id");
    ts.executor_type
        = executor_type_from_string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
    ts.state = task_state_from_string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
    ts.start_time = to_string_unless_null(sqlite3_column_text(stmt, 4));

    ts.end_time = to_string_unless_null(sqlite3_column_text(stmt, 5));
    ts.timeout = std::chrono::seconds(sqlite3_column_int64(stmt, 6));
    ts.payload = nlohmann::json::parse(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7)));
    ts.message = to_string_unless_null(sqlite3_column_text(stmt, 8));
}

void from_db_row_except_id(sqlite3_stmt* stmt, TaskStatus& ts) {
    CHECK_NOTNULL(stmt);

    ts.executor_id
        = parse_id(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)), "executor_id");
    ts.executor_type
        = executor_type_from_string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
    ts.state = task_state_from_string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
    ts.start_time = to_string_unless_null(sqlite3_column_text(stmt, 3));

    ts.end_time = to_string_unless_null(sqlite3_column_text(stmt, 4));
    ts.timeout = std::chrono::seconds(sqlite3_column_int64(stmt, 5));
    ts.payload = nlohmann::json::parse(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6)));
    ts.message = to_string_unless_null(sqlite3_column_text(stmt, 7));
}

std::optional<TaskStatus> query_task_status(sqlite3* db, uuids::uuid id) {
    CHECK_NOTNULL(db);
    const char* sql = R"(
    SELECT executor_id, executor_type, state, start_time, 
           end_time, timeout, payload, message
    FROM TaskStatus 
    WHERE id = ?
    ORDER BY input_time DESC
    LIMIT 1;
    )";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    GUARD_OK_OR_RETURN_NULLOPT("prepare stmt");
    scope_exit clean_stmt([&stmt, &db]() {
        int rc = sqlite3_finalize(stmt);
        LOG_IF(ERROR, rc != SQLITE_OK) << "sqlite3 finalize" << " failed: " << sqlite3_errmsg(db);
    });
    auto id_str = to_string(id);
    // Bind the ID parameter
    rc = sqlite3_bind_text(stmt, 1, id_str.c_str(), -1, SQLITE_STATIC);
    GUARD_OK_OR_RETURN_NULLOPT("bind id");

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        TaskStatus ts;
        // Extract values from the result set
        ts.id = id;
        from_db_row_except_id(stmt, ts);
        return ts; // Return the populated TaskStatus
    }
    return std::nullopt; // No result found
}

void init_db(sqlite3* db) {
    CHECK_NOTNULL(db);
    LOG(INFO) << "initializing task_status.db";
    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS TaskStatus (
            id TEXT NOT NULL,
            executor_id TEXT NOT NULL,
            executor_type TEXT NOT NULL,
            state TEXT NOT NULL,
            start_time TEXT,
            end_time TEXT,
            input_time TEXT NOT NULL, -- Data write time
            timeout INTEGER,
            payload TEXT, -- JSON serialized storage
            message TEXT,
            PRIMARY KEY(id,input_time)
        );
    )";

    char* errorMessage = nullptr;
    int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errorMessage);
    if (rc != SQLITE_OK) {
        LOG(ERROR) << "sqlite3 create table TaskStatus failed: " << errorMessage;
        sqlite3_free(errorMessage); // Free the error message
        throw SqliteError(db, rc);
    }
    LOG(INFO) << "Table TaskStatus created or already exists.";
}

} // namespace

TaskStatusManager::TaskStatusManager() {
    auto db_path = global_vars::home_path() / "databases" / "task_status.db";
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

void TaskStatusManager::push_back(const TaskStatus& ts) {
    CHECK_NOTNULL(db);
    int rc = insert(db.get(), ts);
    if (rc != SQLITE_OK && rc != SQLITE_DONE) { throw SqliteError(db.get(), rc); }
}

std::optional<TaskStatus> TaskStatusManager::get(uuids::uuid task_id) {
    CHECK_NOTNULL(db);
    return query_task_status(db.get(), task_id);
}
std::vector<TaskStatus> range_query(sqlite3* db, const TaskStatusManager::GetTaskParam& params) {
    CHECK_NOTNULL(db);
    const char* sql = R"(
    SELECT id, executor_id, executor_type, state, start_time, 
           end_time, timeout, payload, message
    FROM TaskStatus 
    ORDER BY input_time DESC
    LIMIT ?;
    )";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    GUARD_OK_OR_RETURN_NULLOPT("prepare stmt");
    scope_exit clean_stmt([&stmt, &db]() {
        int rc = sqlite3_finalize(stmt);
        LOG_IF(ERROR, rc != SQLITE_OK) << "sqlite3 finalize" << " failed: " << sqlite3_errmsg(db);
    });
    rc = sqlite3_bind_int64(stmt, 1, params.limit);
    GUARD_OK_OR_RETURN_NULLOPT("bind id");
    std::vector<TaskStatus> res;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        TaskStatus ts;
        from_db_row(stmt, ts);
        res.push_back(std::move(ts));
    }
    return res;
}

std::vector<TaskStatus> TaskStatusManager::query(const GetTaskParam& params) {
    return range_query(db.get(), params);
}

expected<void, std::string> TaskStatusManager::on_receive(const message_bus::Message& message) {
    FWK_MSGBUS_REGISTER_EVENT_HANDLER("TaskStatusEvent", TaskStatus, &TaskStatusManager::push_back);

    return err_invalid_operation(message);
}

std::optional<int> parse_limit(const httplib::Request& req) try {
    auto it = req.params.find("limit");
    if (it == req.params.end()) { return {}; }
    return std::stoi(it->second);
}
catch (...) {
    return {};
}

void build_api(std::shared_ptr<TaskStatusManager> mgr, httplib::Server& svr) {
    LOG(INFO) << "TaskStatusMgr build api";
    CHECK_NOTNULL(mgr);
    using namespace httplib;
    svr.Post("/api/task-status", [mgr](const Request& req, Response& res) {
        auto j = nlohmann::json::parse(req.body);
        auto ts = j.get<TaskStatus>();
        mgr->push_back(ts);
        res.set_content("OK", "text/plain");
    });
    svr.Get("/api/task-status", [mgr](const Request& req, Response& res) {
        TaskStatusManager::GetTaskParam params;
        if (std::optional limit = parse_limit(req); limit.has_value() && *limit > 0) {
            params.limit = *limit;
        }
        auto ts_arr = mgr->query(params);
        res.set_content(nlohmann::json(ts_arr).dump(), "application/json");
    });
    svr.Get("/api/task/:id/status", [mgr](const Request& req, Response& res) {
        auto id_str = req.path_params.at("id");
        auto opt_id = uuids::uuid::from_string(id_str);
        if (!opt_id) { throw std::invalid_argument("invalid id str: " + id_str); }
        auto opt_res = mgr->get(*opt_id);
        if (!opt_res) {
            res.status = 404;
            res.set_content("null", "application/json");
            return;
        }
        auto payload = nlohmann::json(*opt_res);
        res.set_content(payload.dump(), "application/json");
    });
}
