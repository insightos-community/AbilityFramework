// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <string_view>
#include <vector>
bool is_valid_ipv4(std::string_view ip);
bool is_valid_ipv6(std::string_view ip);
// get local ipv4/ipv6 addresses, framework name, framework id
std::vector<std::string> get_local_ip_addresses();
