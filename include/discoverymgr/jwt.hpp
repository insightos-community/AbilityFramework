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

#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/nlohmann-json/traits.h>

// 生成JWT令牌,永久有效
std::string generate_jwt_token(const std::string& secret, const std::string& host_id);
// 验证JWT令牌
bool is_valid_jwt_token(const std::string& token, const std::string& secret);
// 检查JWT令牌是否过期
bool is_expired_jwt_token(const std::string& token, const std::string& secret);
// 生成JWT令牌,可指定有效期（秒制）
std::string generate_jwt_with_expiration(const std::string& secret, const std::string& host_id, int time_in_seconds);
// 计算JWT的失效时间（秒制）
int check_token_expiration(const std::string& token);