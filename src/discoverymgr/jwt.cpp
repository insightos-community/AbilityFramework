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

#include "discoverymgr/jwt.hpp"
#include <chrono>
#include <string>
#include <iostream>
#include <stdexcept>

std::string generate_jwt_token(const std::string& secret, const std::string& host_id) {
    try {
        auto token = jwt::create<jwt::traits::nlohmann_json>()
        .set_type("JWT")  
        .set_issuer( host_id )  
        .set_payload_claim("Permanent", jwt::basic_claim<jwt::traits::nlohmann_json>(std::string("true")))  // 添加长期有效字段
        .sign(jwt::algorithm::hs256{ secret }); // 使用 HS256 算法和密钥进行签名
        return token;
    } 
    catch (...) {
        throw std::runtime_error("Token creation failed");
    }
}

bool is_valid_jwt_token(const std::string& token, const std::string& secret) {
    try {
        auto decoded = jwt::decode<jwt::traits::nlohmann_json>(token);
        auto verifier = jwt::verify<jwt::traits::nlohmann_json>()
        .allow_algorithm(jwt::algorithm::hs256{secret});
        verifier.verify(decoded);
        return true;
    } 
    catch (...) {
        return false;
    }
}

bool is_expired_jwt_token(const std::string& token, const std::string& secret) {
    try {
        auto decoded = jwt::decode<jwt::traits::nlohmann_json>(token);
        auto exp = decoded.get_expires_at();
        auto now = std::chrono::system_clock::now();
        if (exp < now) {
            return true;
        } else {
            return false;
        }
    } 
    catch (...) {
        return false;
    }
}

std::string generate_jwt_with_expiration(const std::string& secret, const std::string& host_id, int time_in_seconds) {
    try {
        auto expires_at = std::chrono::system_clock::now() + std::chrono::seconds{time_in_seconds};
        auto token = jwt::create<jwt::traits::nlohmann_json>()
            .set_type("JWT")
            .set_issuer( host_id )
            .set_issued_at(std::chrono::system_clock::now())
            .set_expires_at(expires_at) 
            .sign(jwt::algorithm::hs256{secret});  // 使用 HS256 算法和密钥进行签名
        return token; // 返回生成的 JWT
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to generate JWT: ") + e.what());
    }
}

int check_token_expiration(const std::string& token) {
    try {
        auto decoded = jwt::decode<jwt::traits::nlohmann_json>(token);
        if (decoded.has_payload_claim("Permanent") &&
            decoded.get_payload_claim("Permanent").as_string() == "true") {
            return -1;  // 返回 -1 表示长期有效
        }
        else {
            auto exp = decoded.get_expires_at();
            auto now = std::chrono::system_clock::now();
            auto remaining_seconds = std::chrono::duration_cast<std::chrono::seconds>(exp - now).count();
            if (remaining_seconds <= 0) {
                std::cerr << "Token has expired\n";
                return 0;  // 返回 0 表示已过期
            }
            return remaining_seconds;  // 返回剩余有效时间（秒制）
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 0; 
    }
}