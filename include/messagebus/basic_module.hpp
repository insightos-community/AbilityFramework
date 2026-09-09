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

namespace message_bus {

// 基础模板，默认情况下返回 false
template <typename T>
constexpr bool is_optional = false;

// 特化模板，用于 std::optional
template <typename T>
constexpr bool is_optional<std::optional<T>> = true;
namespace detail {
template <typename Res>
inline void write_response(Res&& res, const Message& message) {
    if constexpr (is_expected<Res>) {
        if (!res) {
            message.respond_error(res.error());
            return;
        }
        message.respond(*res);
        return;
    }
    if constexpr (is_optional<Res>) {
        if (!res) {
            message.respond(nlohmann::json{});
            return;
        }
        message.respond(*res);
    }
    else {
        message.respond(res);
        return;
    }
}
} // namespace detail

template <typename Sub>
struct BasicModule
    : std::enable_shared_from_this<Sub>
    , virtual public Module {
    using SubType = Sub;
    using HandlerFunc = std::function<expected<void, std::string>(const Message&)>;
    void on_register() override { add_message_handlers(); };
    template <FromJson T>
    using EventHandlerFptr = void (Sub::*)(T);
    template <typename T>
    void add_event_handler(std::string_view event_name, void (Sub::*f)(T)) {
        _impl_add_event_handler<T>(event_name, f);
    }
    template <typename T>
    void add_event_handler(std::string_view event_name, void (Sub::*f)(T) const) {
        _impl_add_event_handler<T>(event_name, f);
    }
    template <typename R, typename T>
    using QueryHandlerFptr = R (Sub::*)(T);
    template <ToJson R, FromJson T>
    void add_query_handler(std::string_view query_name, R (Sub::*f)(T)) {
        _impl_add_query_handler<R, T>(query_name, f);
    }

    template <ToJson R, FromJson T>
    void add_query_handler(std::string_view query_name, R (Sub::*f)(T) const) {
        _impl_add_query_handler<R, T>(query_name, f);
    }
    template <ToJson R>
    void add_query_handler(std::string_view query_name, R (Sub::*f)()) {
        _impl_add_query_handler_empty<R>(query_name, f);
    }
    template <ToJson R>
    void add_query_handler(std::string_view query_name, R (Sub::*f)() const) {
        _impl_add_query_handler_empty<R>(query_name, f);
    }
    template <typename R, typename T>
    using QueryHandlerFptrExpected = expected<R, std::string> (Sub::*)(T);
    template <typename R, typename T>
    using QueryHandlerFptrExpectedConst = expected<R, std::string> (Sub::*)(T) const;
    template <typename R, typename T>
    void add_query_handler(std::string_view query_name, QueryHandlerFptrExpected<R, T> f) {
        _impl_add_query_handler<R, T>(query_name, f);
    }
    template <typename R, typename T>
    void add_query_handler(std::string_view query_name, QueryHandlerFptrExpectedConst<R, T> f) {
        add_query_handler(query_name, reinterpret_cast<QueryHandlerFptrExpected<R, T>>(f));
    }
    template <typename R, typename T>
    using QueryHandlerFptrOptional = std::optional<R> (Sub::*)(T);
    template <typename R, typename T>
    void add_query_handler(std::string_view query_name, QueryHandlerFptrOptional<R, T> f) {
        _impl_add_query_handler<R, T>(query_name, f);
    }
    template <typename R, typename T>
    using QueryHandlerFptrOptionalConst = std::optional<R> (Sub::*)(T) const;
    template <typename R, typename T>
    void add_query_handler(std::string_view query_name, QueryHandlerFptrOptionalConst<R, T> f) {
        add_query_handler(query_name, reinterpret_cast<QueryHandlerFptrOptional<R, T>>(f));
    }

    virtual void add_message_handlers() = 0;

    expected<void, std::string> on_receive(const Message& message) final {
        auto it = handlers.find(message.router.operation);
        if (it == handlers.end()) { return err_invalid_operation(message); }
        return it->second(message);
    }

private:
    std::map<std::string, HandlerFunc> handlers;
    std::mutex m_msg_handler;
    template <typename T, typename F>
    void _impl_add_event_handler(std::string_view event_name, F func) {
        using U = std::remove_cvref_t<T>;
        handlers[std::string(event_name)]
            = [f = std::move(func), this](const Message& message) -> expected<void, std::string> {
            auto value = message.parse_as<U>();
            {
                std::lock_guard _lk(m_msg_handler);
                std::invoke(f, (reinterpret_cast<Sub*>(this)), std::move(value));
            }
            return {};
        };
    }
    template <typename R, typename T, typename F>
    void _impl_add_query_handler(std::string_view query_name, F func) {
        using U = std::remove_cvref_t<T>;
        handlers[std::string(query_name)]
            = [f = std::move(func), this](const Message& message) -> expected<void, std::string> {
            auto payload = message.parse_as<U>();
            {
                std::lock_guard _lk(m_msg_handler);
                auto res = std::invoke(f, reinterpret_cast<Sub*>(this), std::move(payload));
                detail::write_response(res, message);
            }
            return {};
        };
    }
    template <typename R, typename F>
    void _impl_add_query_handler_empty(std::string_view query_name, F func) {
        handlers[std::string(query_name)]
            = [f = std::move(func), this](const Message& message) -> expected<void, std::string> {
            std::lock_guard _lk(m_msg_handler);
            auto res = std::invoke(f, reinterpret_cast<Sub*>(this));
            detail::write_response(res, message);
            return {};
        };
    }
};

#define ON_EVENT(EventName, HandlerFunc) add_event_handler(EventName, &SubType::HandlerFunc);
#define ON_QUERY(QueryName, HandlerFunc) add_query_handler(QueryName, (&SubType::HandlerFunc));
} // namespace message_bus
