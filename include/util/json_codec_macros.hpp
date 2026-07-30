// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <nlohmann/json.hpp>
template <typename T>
inline T& ref_from_value(T& x) {
    return x;
}

template <typename T>
inline T& ref_from_value(std::optional<T>& x) {
    x.reset();
    x.emplace();
    return *x;
}
#define read_with_default(Name, DefaultValue)             \
    {                                                     \
        auto it = j.find(#Name);                          \
        if (it == j.end()) { x.position = DefaultValue; } \
        else { it->get_to(x.Name); }                      \
    }

#define read_required(Name) j.at(#Name).get_to(x.Name)
#define read_optional(Name)                                        \
    {                                                              \
        auto it = j.find(#Name);                                   \
        if (it != j.end()) { it->get_to(ref_from_value(x.Name)); } \
    }
#define read_optional_unless_empty(Name)                                             \
    {                                                                                \
        auto it = j.find(#Name);                                                     \
        if (it != j.end() && (!it->empty())) { it->get_to(ref_from_value(x.Name)); } \
    }

#define output(Name) (j[#Name] = x.Name)

#define output_unless_empty(Name) \
    if (!x.Name.empty()) { j[#Name] = x.Name; }

#define output_if_has_value(Name) \
    if (x.Name.has_value()) { j[#Name] = *(x.Name); }
