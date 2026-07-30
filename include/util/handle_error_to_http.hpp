// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <httplib.h>

void handle_error_to_http(
    const httplib::Request& req, httplib::Response& res, std::exception_ptr ex
) noexcept;
