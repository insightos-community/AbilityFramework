// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <nlohmann/json.hpp>
struct ProblemDetail {
    int status;
    std::string type;
    std::string title;
    std::string detail;
    std::string instance;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ProblemDetail, status, type, title, detail, instance)
};
