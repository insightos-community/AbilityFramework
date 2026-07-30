// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <concepts>
#include <ranges>
#include <vector>
template <std::ranges::input_range R, typename F>
// requires std::invocable<F, std::ranges::range_reference_t<R>>
inline auto transform_to_vector(R&& r, F&& f) {
    using Res = std::invoke_result_t<F, std::ranges::range_reference_t<R>>;
    std::vector<Res> res;
    for (auto&& x : r) {
        using T = decltype(x);
        res.push_back(f(std::forward<T>(x)));
    }
    return res;
}

struct _to_vector_t {

    template <std::ranges::input_range R>
    auto operator()(R&& r) const {
        using Res = std::ranges::range_reference_t<R>;
        std::vector<Res> res;
        for (auto&& x : r) {
            using T = decltype(x);
            res.push_back(std::forward<T>(x));
        }
        return res;
    }
    template <std::ranges::input_range R>
    friend auto operator|(R&& r, _to_vector_t _) {
        using Res = std::ranges::range_reference_t<R>;
        std::vector<Res> res;
        for (auto&& x : r) {
            using T = decltype(x);
            res.push_back(std::forward<T>(x));
        }
        return res;
    }
};
constexpr _to_vector_t to_vector;
constexpr auto get_second = [](const auto& x) { return x.second; };
