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

#include "expected.hpp"
#include <concepts>
#include <coroutine>
#include <string>

namespace await_expected {
///@brief run expected 的包含协程类型
template <typename T, typename E>
struct RunExpected;

template <typename T, typename E>
struct ExpectedPromise {
    std::optional<expected<T, E>> result;
    RunExpected<T, E> get_return_object();
    std::suspend_always initial_suspend() { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    template <std::convertible_to<T> U>
    void return_value(U&& v) {
        result.emplace(std::forward<U>(v));
    }
    template <std::convertible_to<E> E1>
    void return_value(::semantic_expected::unexpected<E1> err) {
        result.emplace(std::move(err));
    }
    void unhandled_exception() {}
};

template <typename T, typename E>
struct ExpectedAwaitable {
    expected<T, E> inner;
    bool await_ready() { return inner.has_value(); }
    template <typename U, typename E1>
        requires std::convertible_to<U, T> && std::convertible_to<E, E1>
    void await_suspend(std::coroutine_handle<ExpectedPromise<U, E1>> h);
    T await_resume() {
        if constexpr (std::is_void_v<T>) { return; }
        else { return std::move(inner.value()); }
    }
};

template <typename T, typename E>
ExpectedAwaitable<T, E> operator co_await(expected<T, E>&& ev) {
    return {std::move(ev)};
}

///@brief run expected 的包含协程类型
template <typename T, typename E>
struct RunExpected {
    using promise_type = ExpectedPromise<T, E>;

private:
    std::coroutine_handle<promise_type> handle;

public:
    explicit RunExpected(std::coroutine_handle<promise_type> _h)
        : handle(_h) {}
    RunExpected(const RunExpected&) = delete;
    RunExpected(RunExpected&& r) noexcept
        : handle{std::exchange(r.handle, nullptr)} {};
    RunExpected& operator=(const RunExpected&) = delete;
    RunExpected& operator=(RunExpected&& r) noexcept { std::swap(handle, r.handle); }
    expected<T, E> run() && {
        handle.resume();
        return std::move(handle.promise().result.value());
    }
    ~RunExpected() {
        if (handle) {
            handle.destroy();
            handle = nullptr;
        }
    }
};

template <typename T, typename E>
template <typename U, typename E1>
    requires std::convertible_to<U, T> && std::convertible_to<E, E1>
inline void ExpectedAwaitable<T, E>::await_suspend(
    std::coroutine_handle<ExpectedPromise<U, E1>> h
) {
    // 能进到这一步的, inner一定是error状态
    h.promise().result.emplace(std::move(inner).error());
}

template <typename T, typename E>
RunExpected<T, E> ExpectedPromise<T, E>::get_return_object() {
    return RunExpected<T, E>{std::coroutine_handle<ExpectedPromise<T, E>>::from_promise(*this)};
}

template <typename T, typename E>
expected<T, E> operator||(std::optional<T>&& opt, ::semantic_expected::unexpected<E>&& e) {
    if (opt.has_value()) { return std::move(opt).value(); }
    return std::move(e);
}

template <typename T, typename E>
ExpectedAwaitable<T, E> operator co_await(RunExpected<T, E>&& f) {
    return {f.run()};
}

} // namespace await_expected

using await_expected::RunExpected;

using await_expected::operator co_await;
using await_expected::operator||;
