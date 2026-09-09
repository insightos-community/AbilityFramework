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
