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

#include "lifecyclemgr/heartbeat.hpp"

std::string_view to_string(LifecycleState s) {
    using namespace std::literals;
#define _entry(Name) \
    case LifecycleState::Name: return #Name##sv;
    switch (s) {
        _entry(Unknown);
        _entry(Inactive);
        _entry(Init);
        _entry(Standby);
        _entry(Running);
        _entry(Suspend);
        _entry(Terminated);
        _entry(Error);
    default: return "<unknown state>"sv;
    }
}
#undef _entry

template <>
LifecycleState from_sv(std::string_view sv) {
    using namespace std::literals;
#define _entry(Name) \
    if (sv == #Name##sv) { return LifecycleState::Name; }

    _entry(Unknown);
    _entry(Inactive);
    _entry(Init);
    _entry(Standby);
    _entry(Running);
    _entry(Suspend);
    _entry(Terminated);
    _entry(Error);
    throw std::invalid_argument("invalid lifecycleState " + std::string(sv));
}

#undef _entry

template <>
ProtocolType from_sv(std::string_view sv) {
    using namespace std::literals;
#define _entry(Name) \
    if (sv == #Name##sv) { return ProtocolType::Name; }

    _entry(http);
    _entry(coap);

    throw std::invalid_argument("invalid ProtocolType " + std::string(sv));
}
