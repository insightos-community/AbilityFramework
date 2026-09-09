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
enum class BindMode { local, any, remote };

#define _field(Name) {BindMode::Name, #Name}

NLOHMANN_JSON_SERIALIZE_ENUM(BindMode, {_field(local), _field(any), _field(remote)})

#undef _field

enum class OwnershipMode { unique, shared };

#define _field(Name) {OwnershipMode::Name, #Name}
NLOHMANN_JSON_SERIALIZE_ENUM(OwnershipMode, {_field(unique), _field(shared)})

#undef _field
