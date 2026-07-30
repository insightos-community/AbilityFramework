// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "prelude.hpp"
#pragma once
#include <vector>

struct FtpClient {
    std::string base_url;
    std::string username, password;
    expected<std::vector<std::string>, ErrorMsg> list_files(std::string_view dir) const;
    expected<std::vector<char>, ErrorMsg> download_file(std::string_view file_path) const;
};
