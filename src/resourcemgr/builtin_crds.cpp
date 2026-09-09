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

#include "resourcemgr/builtin_crds.hpp"

// 框架内置 Ability CRD schema
// 定义所有 Ability CR 的通用结构, 不包含能力特有内容
// 能力特有的 config/debugOption/status schema 由 AbilityManifest 提供
static const nlohmann::json ability_crd_schema = nlohmann::json::parse(R"({
    "type": "object",
    "required": ["kind", "metadata", "spec"],
    "properties": {
        "kind": {
            "type": "string",
            "enum": ["AtomAbility", "ComposeAbility", "AbstractAbility"]
        },
        "metadata": {
            "type": "object",
            "required": ["name"],
            "properties": {
                "name": { "type": "string" },
                "labels": { "type": "object", "additionalProperties": { "type": "string" } },
                "annotations": { "type": "object", "additionalProperties": { "type": "string" } }
            }
        },
        "spec": {
            "type": "object",
            "required": ["package", "version", "abilityName"],
            "properties": {
                "package": { "type": "string" },
                "version": { "type": "string" },
                "abilityName": { "type": "string" },
                "position": { "type": "string", "default": "localhost" },
                "autoStart": { "type": "boolean" },
                "priority": { "type": "integer" },
                "activityCondition": {},
                "config": {},
                "debugOption": {},
                "subabilities": { "type": "array" },
                "devices": { "type": "array" },
                "models": { "type": "array" }
            }
        }
    }
})");

// 框架内置 Service CRD schema (Phase 2)
static const nlohmann::json service_crd_schema = nlohmann::json::parse(R"({
    "type": "object",
    "required": ["kind", "metadata", "spec"],
    "properties": {
        "kind": {
            "type": "string",
            "enum": ["Service"]
        },
        "metadata": {
            "type": "object",
            "required": ["name"],
            "properties": {
                "name": { "type": "string" },
                "labels": { "type": "object", "additionalProperties": { "type": "string" } },
                "annotations": { "type": "object", "additionalProperties": { "type": "string" } }
            }
        },
        "spec": {
            "type": "object",
            "required": ["package", "version", "serviceName"],
            "properties": {
                "package": { "type": "string" },
                "version": { "type": "string" },
                "serviceName": { "type": "string" },
                "position": { "type": "string", "default": "localhost" },
                "config": {},
                "restartPolicy": {
                    "type": "string",
                    "enum": ["always", "on-failure", "never"],
                    "default": "always"
                },
                "maxRestarts": { "type": "integer", "default": 5 },
                "restartBackoffSeconds": { "type": "integer", "default": 10 },
                "healthCheck": {
                    "type": "object",
                    "properties": {
                        "liveness": {
                            "type": "object",
                            "properties": {
                                "type": { "type": "string", "enum": ["http", "process"] },
                                "path": { "type": "string" },
                                "port": { "type": "integer" },
                                "intervalSeconds": { "type": "integer", "default": 10 },
                                "failureThreshold": { "type": "integer", "default": 3 }
                            }
                        },
                        "readiness": {
                            "type": "object",
                            "properties": {
                                "type": { "type": "string", "enum": ["http"] },
                                "path": { "type": "string" },
                                "port": { "type": "integer" },
                                "intervalSeconds": { "type": "integer", "default": 5 }
                            }
                        }
                    }
                },
                "devices": { "type": "array" },
                "models": { "type": "array" }
            }
        }
    }
})");

const nlohmann::json& get_builtin_ability_crd_schema() {
    return ability_crd_schema;
}

const nlohmann::json& get_builtin_service_crd_schema() {
    return service_crd_schema;
}
