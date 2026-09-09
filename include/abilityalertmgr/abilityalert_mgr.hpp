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

#include "ability_alert.hpp"
#include "httplib.h"
#include "util/sqlite_types.hpp"

class AbilityAlertManager {
public:
    // 打开数据库
    AbilityAlertManager();

    void push_back(const AbilityAlert& alert);
    /**
     * @param abilityInstaceId ,如果为空,表示提取所有能力的alert信息,如果不为空,则指定能力id
     * @param num_entries 表示输出条目的最大数量
     */
    std::vector<AbilityAlert> query(uuids::uuid abilityInstanceId = {}, int num_entries = 20);

    friend void build_api(std::shared_ptr<AbilityAlertManager> mgr, httplib::Server& svr);

private:
    SqlitePtr db;
    std::recursive_mutex m;
};
