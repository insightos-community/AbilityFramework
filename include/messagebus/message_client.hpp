// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

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
 * @brief utility function,used to check whether the send process succeeded,often with send_synccombineuse
 * if the message send process fails,or the message itself is not success(),will all be called with the corresponding error message
 * @param exp_msg the result to check
 * @param f correspondingfunction,requires it to at least acceptstring_viewparameter
 */
void on_error(const expected<Message, std::string>& exp_msg, auto&& f) {
    if (!exp_msg) { f(exp_msg.error()); }
    else if (!exp_msg->success()) { f(exp_msg->view()); }
}

} // namespace message_bus
