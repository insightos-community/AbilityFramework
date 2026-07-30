// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/nlohmann-json/traits.h>

// generateJWTtoken,permanent
std::string generate_jwt_token(const std::string& secret, const std::string& host_id);
// verify JWT token
bool is_valid_jwt_token(const std::string& token, const std::string& secret);
// check whether the JWT token has expired
bool is_expired_jwt_token(const std::string& token, const std::string& secret);
// generateJWTtoken,validity period can be specified(in seconds)
std::string generate_jwt_with_expiration(const std::string& secret, const std::string& host_id, int time_in_seconds);
// compute JWT expiry time (seconds)
int check_token_expiration(const std::string& token);