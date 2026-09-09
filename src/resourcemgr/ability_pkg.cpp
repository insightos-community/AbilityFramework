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

#include "resourcemgr/ability_pkg.hpp"
#include <algorithm>
#include <ranges>

auto AbilityPackage::ability_path(std::string_view ability_name) const -> Path {
    auto name_equal
        = [ability_name](const auto& pair) { return pair.second.metadata.name == ability_name; };
    if (std::ranges::any_of(abilities, name_equal)) { return path / "bin" / ability_name; }
    return {};
}
