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
#include "messagebus.hpp"
#include "util/make_uuid.hpp"
#include <string_view>

template <message_bus::ToJson T>
    requires(!std::same_as<std::remove_cvref_t<T>, std::string>)
inline message_bus::Message make_message(
    std::string_view from, std::string_view to, std::string_view operation, T&& data
) {
    std::string serialized_data = nlohmann::json(std::forward<T>(data)).dump();
    return message_bus::Message{
        .header{
            .id = make_uuid(),
            .parent_id{},
            .timestamp{std::chrono::system_clock::now()},
            .is_synchronous = false
        },
        .router{
            .source = std::string{from},
            .destination = std::string{to},
            .operation = std::string{operation}
        },
        .content{serialized_data.begin(), serialized_data.end()}
    };
}

inline message_bus::Message make_message(
    std::string_view from, std::string_view to, std::string_view operation
) {
    return message_bus::Message{
        .header{
            .id = make_uuid(),
            .parent_id{},
            .timestamp{std::chrono::system_clock::now()},
            .is_synchronous = false
        },
        .router{
            .source = std::string{from},
            .destination = std::string{to},
            .operation = std::string{operation}
        },
        .content{}
    };
}

namespace message_bus {
/**
 * @brief 工具函数,用于检查发送过程是否成功,常与 send_sync搭配使用
 *  如果消息发送过程失败,或消息本身不是success(),都会以对应的错误消息调用f
 *  @param exp_msg 要检查的结果
 *  @param f 对应的函数,要求它至少能接受string_view参数
 */
void on_error(const expected<Message, std::string>& exp_msg, auto&& f) {
    if (!exp_msg) { f(exp_msg.error()); }
    else if (!exp_msg->success()) { f(exp_msg->view()); }
}

} // namespace message_bus
