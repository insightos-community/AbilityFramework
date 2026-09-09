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
