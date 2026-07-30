// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "util/uuid_json_convert.hpp"
#include <nlohmann/json.hpp>
#include <uuid.h>
namespace msg_params {
// it has only one item
struct TaskIdWrapper {
    uuids::uuid taskId;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(TaskIdWrapper, taskId);
};

// it has only one item
struct ProcessIdWrapper {
    uuids::uuid processId;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ProcessIdWrapper, processId);
};
} // namespace msg_params
