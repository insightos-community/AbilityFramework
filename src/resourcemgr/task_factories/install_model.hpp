// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "resourcemgr/resource_mgr.hpp"
#include "taskmgr/task.hpp"

struct InstallModelByIdTaskFactory {
    std::shared_ptr<ModelManager> mgr;
    TaskPtr operator()(const nlohmann::json& params) const;
};
