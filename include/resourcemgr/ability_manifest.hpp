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
#include "resourcemgr/ability_crd.hpp"
#include <nlohmann/json.hpp>
#include <string>

// 能力清单: 描述一个具体能力的接口、schema、依赖等信息
// 从原 AbilityCRD 中提取的能力特有字段, 随能力包发布
// CRD 是框架内置的资源类型规范, Manifest 是能力开发者编写的能力描述
struct AbilityManifest {
    std::string abilityName;
    std::string kind; // AtomAbility | ComposeAbility | AbstractAbility

    // 能力特有字段 (从 AbilityCRD::Spec 迁移)
    nlohmann::json provides;
    nlohmann::json types;
    nlohmann::json rpcMethods;
    nlohmann::json tasks;
    nlohmann::json config;      // 默认配置值
    nlohmann::json debugOption; // 默认调试选项

    // Schema 用于验证 CR 中的对应字段
    struct Schema {
        nlohmann::json constants;       // 为上级能力提供常量
        nlohmann::json openAPIV3Schema; // 验证 config/status/debugOption
        friend void from_json(const nlohmann::json&, Schema&);
        friend void to_json(nlohmann::json&, const Schema&);
    };
    Schema schema;

    // 依赖声明 (从 AbilityCRD::Deps 迁移, 复用已有类型)
    AbilityCRD::Deps depends;

    // 框架处理状态
    AbilityCRD::FwkDetail fwk;

    friend void from_json(const nlohmann::json&, AbilityManifest&);
    friend void to_json(nlohmann::json&, const AbilityManifest&);
};
