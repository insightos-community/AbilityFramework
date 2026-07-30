// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

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
        .set_payload_claim("Permanent", jwt::basic_claim<jwt::traits::nlohmann_json>(std::string("true"))) // add a permanent-validity field
        .sign(jwt::algorithm::hs256{ secret }); // sign using HS256 algorithm and secret
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
            .sign(jwt::algorithm::hs256{secret}); // sign using HS256 algorithm and secret
        return token; // return the generated JWT
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to generate JWT: ") + e.what());
    }
}

int check_token_expiration(const std::string& token) {
    try {
        auto decoded = jwt::decode<jwt::traits::nlohmann_json>(token);
        if (decoded.has_payload_claim("Permanent") &&
            decoded.get_payload_claim("Permanent").as_string() == "true") {
            return -1; // return -1 means long-term valid
        }
        else {
            auto exp = decoded.get_expires_at();
            auto now = std::chrono::system_clock::now();
            auto remaining_seconds = std::chrono::duration_cast<std::chrono::seconds>(exp - now).count();
            if (remaining_seconds <= 0) {
                std::cerr << "Token has expired\n";
                return 0; // return 0 means expired
            }
            return remaining_seconds; // return remaining validity time (seconds)
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 0; 
    }
}