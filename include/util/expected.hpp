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
// 这是一个模仿 std::expected的文件,用于在前C++23时代提供一个expected接口.
#include <functional>
#include <optional>
#if __cplusplus > 202002L
// 标准库已经实现了expected,不用我们再实现了
#include <expected>
namespace semantic_expected = std;
#else
#include <type_traits>
#include <utility>
// 我们的expected 是用variant模拟的
#include <variant>
namespace draft {
using std::move, std::forward, std::invoke;
struct bad_expected_access : public std::exception {
    bool is_err = true;
    bad_expected_access(bool _is_err = true)
        : is_err(_is_err) {}
    const char* what() const noexcept {
        return is_err ? "expect value but found error" : "expected error but found value";
    };
};

template <typename T>
constexpr bool is_expected_v = false;
template <typename T>
concept is_expected = is_expected_v<T>;
struct unexpect_t {};
struct in_place_t {};

template <typename E>
struct unexpected {
    E inner;
};
// 补充推导指引(gcc 11 及以上不用)
template<class E> unexpected(E unexp) -> unexpected<E>;

template <typename T, typename E>
class expected {
    static_assert(!std::is_reference_v<T>);
    static_assert(!std::is_function_v<T>);
    static_assert(!std::is_same_v<std::remove_cv_t<T>, std::in_place_t>);

public:
    using value_type = T;
    using error_type = E;
    expected()
        requires std::is_default_constructible_v<T>
    {}
    expected(const expected&) = default;
    expected(expected&&) = default;
    expected& operator=(const expected&) = default;
    expected& operator=(expected&&) = default;
    expected(T&& t)
        : inner(std::in_place_index<0>, std::move(t)) {}
    expected(const T& t)
        : inner(std::in_place_index<0>, t) {}
    template <typename G>
    expected(unexpected<G>&& u)
        : inner(std::in_place_index<1>, move(u.inner)) {}
    template <typename G>
    expected(const unexpected<G>& u)
        : inner(std::in_place_index<1>, u.inner) {}
    expected(E&& e)
        requires(!std::is_same_v<T, E>)
        : inner(std::move(e)) {}
    expected(const E& e)
        requires(!std::is_same_v<T, E>)
        : inner(e) {}
    template <typename... Ts>
    expected(unexpect_t, Ts&&... args)
        : inner(std::in_place_index<1>, std::forward<Ts>(args)...) {}
    template <typename... Ts>
    expected(in_place_t, Ts&&... args)
        requires std::is_constructible_v<T, Ts...>
        : inner(std::in_place_index<0>, std::forward<Ts>(args)...) {}
    template <std::convertible_to<T> U>
    expected& operator=(U&& v) {
        inner.template emplace<0>(std::forward<U>(v));
        return *this;
    }
    template <std::convertible_to<E> G>
    expected& operator=(unexpected<G>&& v) {
        inner.template emplace<1>(static_cast<E>(std::move(v.inner)));
        return *this;
    }
    template <std::convertible_to<E> G>
    expected& operator=(const unexpected<G>& v) {
        inner.template emplace<1>(static_cast<E>(v.inner));
        return *this;
    }

private:
    T* value_ptr() {
        T* p = std::get_if<0>(&inner);
        return p;
    }
    const T* value_ptr() const {
        const T* p = std::get_if<0>(&inner);
        return p;
    }

public:
    T& value() & {
        auto p = value_ptr();
        if (!p) { throw bad_expected_access(); }
        return *p;
    }
    const T& value() const& {
        auto p = value_ptr();
        if (!p) { throw bad_expected_access(); }
        return *p;
    }
    T&& value() && {
        auto p = value_ptr();
        if (!p) { throw bad_expected_access(); }
        return move(*p);
    }
    E& error() & {
        E* p = std::get_if<1>(&inner);
        if (!p) { throw bad_expected_access(false); }
        return *p;
    }
    const E& error() const& {
        const E* p = std::get_if<1>(&inner);
        if (!p) { throw bad_expected_access(false); }
        return *p;
    }
    E&& error() && {
        E* p = std::get_if<1>(&inner);
        if (!p) { throw bad_expected_access(false); }
        return std::move(*p);
    }
    bool has_value() const { return inner.index() == 0; }
    operator bool() const { return has_value(); }
    T& operator*() { return value(); }
    const T& operator*() const { return value(); }
    T* operator->() {
        T* p = std::get_if<0>(&inner);
        if (!p) { throw bad_expected_access(); }
        return p;
    }
    const T* operator->() const {
        const T* p = std::get_if<0>(&inner);
        if (!p) { throw bad_expected_access(); }
        return p;
    }
    template <typename U>
    T value_or(U&& default_value) const& {
        static_assert(std::is_copy_constructible_v<T>);
        static_assert(std::is_convertible_v<U, T>);
        auto p = value_ptr();
        return p ? (*p) : static_cast<T>(forward<U>(default_value));
    }
    template <typename U>
    T value_or(U&& default_value) && {
        static_assert(std::is_move_constructible_v<T>);
        static_assert(std::is_convertible_v<U, T>);
        auto p = value_ptr();
        return p ? move(*p) : static_cast<T>(forward<U>(default_value));
    }
    template <typename U>
    E error_or(U&& default_value) const& {
        if (has_value()) { return static_cast<E>(std::forward<U>(default_value)); }
        return error();
    }
    template <typename U>
    E error_or(U&& default_value) && {
        if (has_value()) { return static_cast<E>(std::forward<U>(default_value)); }
        return std::move(error());
    }
    template <typename F>
    auto transform(F&& f) & {
        using R = std::remove_cvref_t<std::invoke_result_t<F, T>>;
        using Res = expected<R, E>;
        if (has_value()) { return Res(in_place_t{}, invoke(forward<F>(f), value())); }
        else { return Res(unexpect_t{}, error()); }
    }
    template <typename F>
    auto transform(F&& f) const& {
        using R = std::remove_cvref_t<std::invoke_result_t<F, const T>>;
        using Res = expected<R, E>;
        if (has_value()) { return Res(in_place_t{}, invoke(forward<F>(f), value())); }
        else { return Res(unexpect_t{}, error()); }
    }
    template <typename F>
    auto transform(F&& f) && {
        using R = std::remove_cvref_t<std::invoke_result_t<F, std::add_rvalue_reference_t<T>>>;
        using Res = expected<R, E>;
        if (has_value()) { return Res(in_place_t{}, invoke(std::forward<F>(f), value())); }
        else { return Res(unexpect_t{}, error()); }
    }
    template <typename F>
    auto transform_error(F&& f) & {
        using E2 = std::remove_cvref_t<std::invoke_result_t<F, E>>;
        using Res = expected<T, E2>;
        if (has_value()) { return Res(in_place_t{}, value()); }
        else { return Res(unexpect_t{}, invoke(forward<F>(f), error())); }
    }
    template <typename F>
    auto transform_error(F&& f) const& {
        using E2 = std::remove_cvref_t<std::invoke_result_t<F, const E>>;
        using Res = expected<T, E2>;
        if (has_value()) { return Res(in_place_t{}, value()); }
        else { return Res(unexpect_t{}, invoke(forward<F>(f), error())); }
    }
    template <typename F>
    auto transform_error(F&& f) && {
        using E2 = std::remove_cvref_t<std::invoke_result_t<F, std::add_rvalue_reference_t<E>>>;
        using Res = expected<T, E2>;
        if (has_value()) { return Res(in_place_t{}, std::move(value())); }
        else { return Res(unexpect_t{}, invoke(std::forward<F>(f), error())); }
    }
    template <typename F>
    auto and_then(F&& f) & {
        using R = std::remove_cvref_t<std::invoke_result_t<F, T>>;
        static_assert(is_expected_v<R>);
        if (has_value()) { return invoke(forward<F>(f), value()); }
        else { return R(unexpect_t{}, error()); }
    }
    template <typename F>
    auto and_then(F&& f) const& {
        using R = std::remove_cvref_t<std::invoke_result_t<F, const T>>;
        static_assert(is_expected_v<R>);
        if (has_value()) { return invoke(forward<F>(f), value()); }
        else { return R(unexpect_t{}, error()); }
    }
    template <typename F>
    auto and_then(F&& f) && {
        using R = std::remove_cvref_t<std::invoke_result_t<F, std::add_rvalue_reference_t<T>>>;
        static_assert(is_expected_v<R>);
        if (has_value()) { return invoke(std::forward<F>(f), std::move(value())); }
        else { return R(unexpect_t{}, error()); }
    }
    template <typename F>
    auto or_else(F&& f) & {
        using R = std::remove_cvref_t<std::invoke_result_t<F, E>>;
        static_assert(is_expected_v<R>);
        if (has_value()) { return R(in_place_t{}, value()); }
        else { return invoke(forward<F>(f), error()); }
    }
    template <typename F>
    auto or_else(F&& f) const& {
        using R = std::remove_cvref_t<std::invoke_result_t<F, const E>>;
        static_assert(is_expected_v<R>);
        if (has_value()) { return R(in_place_t{}, value()); }
        else { return invoke(forward<F>(f), error()); }
    }
    template <typename F>
    auto or_else(F&& f) && {
        using R = std::remove_cvref_t<std::invoke_result_t<F, std::add_rvalue_reference_t<E>>>;
        static_assert(is_expected_v<R>);
        if (has_value()) { return R(in_place_t{}, value()); }
        else { return invoke(forward<F>(f), error()); }
    }

private:
    std::variant<T, E> inner;
};
template <typename E>
class expected<void, E> {

public:
    using value_type = void;
    using error_type = E;
    expected() = default;
    expected(const expected&) = default;
    expected(expected&&) = default;
    expected& operator=(const expected&) = default;
    expected& operator=(expected&&) = default;

    template <typename G>
    expected(unexpected<G>&& u)
        : err(move(u.inner)) {}
    template <typename G>
    expected(const unexpected<G>& u)
        : err(u.inner) {}
    expected(E&& e)
        : err(std::move(e)) {}
    expected(const E& e)
        : err(e) {}
    template <typename... Ts>
    expected(unexpect_t, Ts&&... args)
        : err(std::forward<Ts>(args)...) {}

    void value() const {
        if (err.has_value()) { throw bad_expected_access(); }
    }

    E& error() & {
        if (!err) { throw bad_expected_access(false); }
        return *err;
    }
    const E& error() const& {
        if (!err) { throw bad_expected_access(false); }
        return *err;
    }
    E&& error() && {
        if (!err) { throw bad_expected_access(false); }
        return std::move(*err);
    }
    bool has_value() const { return !err; }
    operator bool() const { return has_value(); }
    void operator*() const { return; }

    template <typename U>
    E error_or(U&& default_value) const& {
        if (has_value()) { return static_cast<E>(std::forward<U>(default_value)); }
        return error();
    }
    template <typename U>
    E error_or(U&& default_value) && {
        if (has_value()) { return static_cast<E>(std::forward<U>(default_value)); }
        return std::move(error());
    }
    template <typename F>
    auto transform(F&& f) & {
        using R = std::remove_cvref_t<std::invoke_result_t<F>>;
        using Res = expected<R, E>;
        if (has_value()) { return Res(in_place_t{}, f()); }
        else { return Res(unexpect_t{}, error()); }
    }
    template <typename F>
    auto transform(F&& f) const& {
        using R = std::remove_cvref_t<std::invoke_result_t<F>>;
        using Res = expected<R, E>;
        if (has_value()) { return Res(in_place_t{}, invoke(forward<F>(f), value())); }
        else { return Res(unexpect_t{}, error()); }
    }
    template <typename F>
    auto transform(F&& f) && {
        using R = std::remove_cvref_t<std::invoke_result_t<F>>;
        using Res = expected<R, E>;
        if (has_value()) { return Res(in_place_t{}, f()); }
        else { return Res(unexpect_t{}, error()); }
    }
    template <typename F>
    auto transform_error(F&& f) & {
        using E2 = std::remove_cvref_t<std::invoke_result_t<F, E>>;
        using Res = expected<void, E2>;
        if (has_value()) { return Res(); }
        else { return Res(unexpect_t{}, invoke(std::forward<F>(f), error())); }
    }
    template <typename F>
    auto transform_error(F&& f) const& {
        using E2 = std::remove_cvref_t<std::invoke_result_t<F, const E>>;
        using Res = expected<void, E2>;
        if (has_value()) { return Res(); }
        else { return Res(unexpect_t{}, invoke(std::forward<F>(f), error())); }
    }
    template <typename F>
    auto transform_error(F&& f) && {
        using E2 = std::remove_cvref_t<std::invoke_result_t<F, std::add_rvalue_reference_t<E>>>;
        using Res = expected<void, E2>;
        if (has_value()) { return Res(); }
        else { return Res(unexpect_t{}, invoke(std::forward<F>(f), error())); }
    }
    template <typename F>
    auto and_then(F&& f) & {
        using R = std::remove_cvref_t<std::invoke_result_t<F>>;
        static_assert(is_expected_v<R>);
        if (has_value()) { return f(); }
        else { return R(unexpect_t{}, error()); }
    }
    template <typename F>
    auto and_then(F&& f) const& {
        using R = std::remove_cvref_t<std::invoke_result_t<F>>;
        static_assert(is_expected_v<R>);
        if (has_value()) { return f(); }
        else { return R(unexpect_t{}, error()); }
    }
    template <typename F>
    auto and_then(F&& f) && {
        using R = std::remove_cvref_t<std::invoke_result_t<F>>;
        static_assert(is_expected_v<R>);
        if (has_value()) { return f(); }
        else { return R(unexpect_t{}, error()); }
    }
    template <typename F>
    auto or_else(F&& f) & {
        using R = std::remove_cvref_t<std::invoke_result_t<F, E>>;
        static_assert(is_expected_v<R>);
        if (has_value()) { return R(in_place_t{}, value()); }
        else { return invoke(forward<F>(f), error()); }
    }
    template <typename F>
    auto or_else(F&& f) const& {
        using R = std::remove_cvref_t<std::invoke_result_t<F, const E>>;
        static_assert(is_expected_v<R>);
        if (has_value()) { return R(in_place_t{}, value()); }
        else { return invoke(forward<F>(f), error()); }
    }
    template <typename F>
    auto or_else(F&& f) && {
        using R = std::remove_cvref_t<std::invoke_result_t<F, std::add_rvalue_reference_t<E>>>;
        static_assert(is_expected_v<R>);
        if (has_value()) { return R(in_place_t{}, value()); }
        else { return invoke(forward<F>(f), error()); }
    }

private:
    std::optional<E> err;
};
template <typename T, typename E>
constexpr bool is_expected_v<expected<T, E>> = true;

} // namespace draft
namespace semantic_expected = draft;

using draft::expected;
using draft::is_expected;
using draft::is_expected_v;
using draft::unexpected;
#endif
