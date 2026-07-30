// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "util/expected.hpp"
#include <memory>
#include <string>

/// @brief base class of AbilityClient for performing various lifecycle operations
/// its different implementation is used to handle different ability server types
struct AbilityClient {
    struct Config {
        // url of the ability, including invocation method (http or coap), ip, port, etc.
        std::string url;
        std::string serialization = "json";
    };
    virtual ~AbilityClient() = default;
    ///@param command must be one of 'start', 'connect', 'disconnect', 'terminate'
    [[nodiscard]]
    expected<void, std::string> execute(std::string_view command);

    ///@brief factory method to construct an AbilityClient
    ///@param config contains this client's contact info, e.g. protocol, port, serialization, etc.
    ///@note returns nullptr if parameters are invalid
    static std::shared_ptr<AbilityClient> make(const Config& config);

protected:
    virtual expected<void, std::string> do_execute(std::string_view command) = 0;
};

using AbilityClientPtr = std::shared_ptr<AbilityClient>;
