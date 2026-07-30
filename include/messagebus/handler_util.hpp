// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "messagebus/messagebus.hpp"
#include "prelude.hpp"
#include <utility>

template <typename InputType, typename F>
auto msgbus_event_handler(
    F&& inner_f, std::string_view handler_name, std::string_view input_type_name
) {
    return [f = std::forward<F>(inner_f), input_type_name]<class Obj>(
               Obj* self, const message_bus::Message& message
           ) mutable -> expected<void, std::string> {
        auto param = message.try_parse<InputType>();
        if (!param) {
            return make_unexpected(
                "invalid message format, expected ", input_type_name, ", but find: ", param.error()
            );
        }
        try {
            std::invoke(f, self, *param);
            return {};
        }
        catch (std::exception& e) {
            return make_unexpected("error performing TaskStatusManager::push_back : ", e.what());
        }
    };
}

template <typename InputType, typename F>
auto msgbus_event_handler_raw(
    F&& inner_f, std::string_view handler_name, std::string_view input_type_name
) {
    return [f = std::forward<F>(inner_f), input_type_name]<class Obj>(
               Obj* self, const message_bus::Message& message
           ) mutable -> expected<void, std::string> {
        auto param = message.get_extra<InputType>();
        if (!param) {
            return make_unexpected(
                "invalid message format, expected ", input_type_name, ", but find: ", param.error()
            );
        }
        try {
            std::invoke(f, self, *param);
            return {};
        }
        catch (std::exception& e) {
            return make_unexpected("error performing TaskStatusManager::push_back : ", e.what());
        }
    };
}
#define FWK_MSGBUS_REGISTER_EVENT_HANDLER(OperationName, InputType, HandlerFunction) \
    {                                                                                \
        static std::string_view op_name = OperationName;                             \
        static auto handler = msgbus_event_handler<InputType>(                       \
            HandlerFunction, "handler_for_" OperationName, #InputType                \
        );                                                                           \
        if (message.operation() == op_name) { return handler(this, message); }       \
    }
#define FWK_MSGBUS_REGISTER_EVENT_HANDLER_RAW(OperationName, InputType, HandlerFunction) \
    {                                                                                    \
        static std::string_view op_name = OperationName;                                 \
        static auto handler = msgbus_event_handler_raw<InputType>(                       \
            HandlerFunction, "handler_for_" OperationName, #InputType                    \
        );                                                                               \
        if (message.operation() == op_name) { return handler(this, message); }           \
    }
