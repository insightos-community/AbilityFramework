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

// 获取框架内置的 Ability CRD schema (用于验证 CR 结构)
const nlohmann::json& get_builtin_ability_crd_schema();

// 获取框架内置的 Service CRD schema (Phase 2)
const nlohmann::json& get_builtin_service_crd_schema();
