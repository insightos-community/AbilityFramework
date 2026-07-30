// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#include "expected.hpp"
#include <concepts>
#include <coroutine>
#include <string>

namespace await_expected {
///@brief run expected containing a coroutine type
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
    void return_value(unexpected<E1> err) {
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

///@brief run expected containing a coroutine type
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
    // reaching this step, innermust beerrorstatus
    h.promise().result.emplace(std::move(inner).error());
}

template <typename T, typename E>
RunExpected<T, E> ExpectedPromise<T, E>::get_return_object() {
    return RunExpected<T, E>{std::coroutine_handle<ExpectedPromise<T, E>>::from_promise(*this)};
}

template <typename T, typename E>
expected<T, E> operator||(std::optional<T>&& opt, unexpected<E>&& e) {
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
