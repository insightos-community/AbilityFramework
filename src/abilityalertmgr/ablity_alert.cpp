// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#include "abilityalertmgr/ability_alert.hpp"

std::string_view to_string(AbilityAlertLevel l) {
    using namespace std::literals;
#define _entry(Name) \
    case AbilityAlertLevel::Name: return #Name##sv;
    switch (l) {
        _entry(unknown);
        _entry(info);
        _entry(anomaly);
        _entry(error);
        _entry(fatal);
    default: return "<unknown level>"sv;
    }
}

#undef _entry

template <>
AbilityAlertLevel from_sv(std::string_view s) {
    using namespace std::literals;
#define _entry(Name) \
    if (s == #Name##sv) { return AbilityAlertLevel ::Name; };

    _entry(unknown);
    _entry(info);
    _entry(anomaly);
    _entry(error);
    _entry(fatal);

    throw std::invalid_argument("invalid alert level " + std::string(s));
}
