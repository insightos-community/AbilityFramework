// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ability_alert.hpp"
#include "httplib.h"
#include "util/sqlite_types.hpp"

class AbilityAlertManager {
public:
    // open the database
    AbilityAlertManager();

    void push_back(const AbilityAlert& alert);
    /**
     * @param abilityInstaceId,if empty,indicates extracting all abilities'alertinfo,if not empty,then specifyability id
     * @param num_entries indicates the max number of output entries
     */
    std::vector<AbilityAlert> query(uuids::uuid abilityInstanceId = {}, int num_entries = 20);

    friend void build_api(std::shared_ptr<AbilityAlertManager> mgr, httplib::Server& svr);

private:
    SqlitePtr db;
    std::recursive_mutex m;
};
