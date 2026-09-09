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
