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
#include "messagebus/messagebus.hpp"
#include "sqlite3.h"
#include "task_status.hpp"
#include "util/sqlite_types.hpp"

namespace httplib {
class Server; // 前向声明
}

class TaskStatusManager : public message_bus::Module {
public:
    struct GetTaskParam {
        int limit = 20;
    };
    static std::filesystem::path default_db_path();
    /// 添加一个新任务项
    /// @throw SqliteError 如果执行失败
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
