// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

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
