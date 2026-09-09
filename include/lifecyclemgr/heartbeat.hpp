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
#include "prelude.hpp"
#include "util/enum_macros.hpp"
#include "util/uuid_json_convert.hpp"
#include <nlohmann/json.hpp>

enum class LifecycleState : char {
    Unknown = 65,
    Inactive = 0,
    Init = 1,
    Standby = 2,
    Running = 3,
    Suspend = 4,
    Terminated = 5,
    Error = 66,
};

std::string_view to_string(LifecycleState);

FWK_DECLARE_FROM_SV(LifecycleState);

inline bool is_active(LifecycleState s) {
    auto x = static_cast<char>(s);
    return x >= static_cast<char>(LifecycleState::Init)
        && x <= static_cast<char>(LifecycleState::Suspend);
}

#define _field(Name) \
    { LifecycleState::Name, #Name }
NLOHMANN_JSON_SERIALIZE_ENUM(
    LifecycleState,
    {_field(Inactive), _field(Init), _field(Standby), _field(Running), _field(Suspend),
     _field(Terminated), _field(Error), _field(Unknown)}
)

#undef _field

enum class ProtocolType : char { http, coap };

FWK_DEFINE_ENUM_OSTREAM_OUTPUT(ProtocolType, http, coap);

inline std::string_view to_string(ProtocolType pt) {
    switch (pt) {
    case ProtocolType::http: return "http";
    case ProtocolType::coap: return "coap";
    default: return "<unknown protocol>";
    }
}
FWK_DECLARE_FROM_SV(ProtocolType);

#define _field(Name) \
    { ProtocolType::Name, #Name }
NLOHMANN_JSON_SERIALIZE_ENUM(ProtocolType, {_field(http), _field(coap)})
#undef _field

struct Heartbeat {
    std::string abilityName;
    std::string version;
    std::string instanceName;
    uuids::uuid id;
    LifecycleState state;
    ProtocolType IPCProtocol;
    uint16_t IPCPort;
    uint16_t abilityPort;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(
        Heartbeat, abilityName, version, instanceName, id, state, IPCProtocol, IPCPort, abilityPort
    )
};
