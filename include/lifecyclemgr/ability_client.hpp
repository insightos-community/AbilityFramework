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

#include "util/expected.hpp"
#include <memory>
#include <string>

/// @brief 用来执行各种生命周期操作的AbilityClient基类
/// 它的不同实现用来涉及不同的能力服务器类型
struct AbilityClient {
    struct Config {
        // 能力所在的url,包含了能力的调用方式(http 或coap),ip,端口等信息
        std::string url;
        std::string serialization = "json";
    };
    virtual ~AbilityClient() = default;
    ///@param command 必须为'start', 'connect', 'disconnect', 'terminate', 之一
    [[nodiscard]]
    expected<void, std::string> execute(std::string_view command);

    ///@brief 工厂方法,用来构造一个AbilityClient
    ///@param config 含有这个客户端的联系方式,如协议,端口,序列化方式等
    ///@note 如果参数不合法,则返回nullptr
    static std::shared_ptr<AbilityClient> make(const Config& config);

protected:
    virtual expected<void, std::string> do_execute(std::string_view command) = 0;
};

using AbilityClientPtr = std::shared_ptr<AbilityClient>;
