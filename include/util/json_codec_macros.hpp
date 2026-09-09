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
