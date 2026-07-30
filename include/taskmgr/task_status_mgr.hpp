// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "messagebus/messagebus.hpp"
#include "sqlite3.h"
#include "task_status.hpp"
#include "util/sqlite_types.hpp"

namespace httplib {
class Server; // forward declaration
}

class TaskStatusManager : public message_bus::Module {
public:
    struct GetTaskParam {
        int limit = 20;
    };
    static std::filesystem::path default_db_path();
    /// add a new task item
    /// @throw SqliteError if execution fails
    void push_back(const TaskStatus&);
    std::optional<TaskStatus> get(uuids::uuid task_id);
    std::vector<TaskStatus> query(const GetTaskParam& params);
    friend void build_api(std::shared_ptr<TaskStatusManager>, httplib::Server&);

    TaskStatusManager();

    // impl begin message_bus::Module
    std::string module_name() const override { return "TaskStatusMgr"; }
    expected<void, std::string> on_receive(const message_bus::Message&) override;
    // impl end message_bus::Module

private:
    SqlitePtr db;
};
