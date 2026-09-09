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
