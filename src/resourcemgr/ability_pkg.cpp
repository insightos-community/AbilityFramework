// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#include "resourcemgr/ability_pkg.hpp"
#include <algorithm>
#include <ranges>

auto AbilityPackage::ability_path(std::string_view ability_name) const -> Path {
    auto name_equal
        = [ability_name](const auto& pair) { return pair.second.metadata.name == ability_name; };
    if (std::ranges::any_of(abilities, name_equal)) { return path / "bin" / ability_name; }
    return {};
}
