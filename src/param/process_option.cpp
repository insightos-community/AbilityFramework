// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#include "param/process_option.hpp"
namespace msg_params {

#define read_required(Name) j.at(#Name).get_to(x.Name)

#define read_optional(Name)                        \
    {                                              \
        auto it = j.find(#Name);                   \
        if (it != j.end()) { it->get_to(x.Name); } \
    }

void from_json(const nlohmann::json& j, ProcessOption& x) {
    read_required(path);
    read_optional(args);
    read_optional(envs);
    read_optional(labels);
}

#define output(Name) (j[#Name] = x.Name)

#define output_unless_empty(Name) \
    if (!x.Name.empty()) { j[#Name] = x.Name; }

void to_json(nlohmann::json& j, const ProcessOption& x) {
    output(path);
    output_unless_empty(args);
    output_unless_empty(envs);
    output_unless_empty(labels);
}

} // namespace msg_params
