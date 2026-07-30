// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#include "resourcemgr/builtin_crds.hpp"

// framework built-in Ability CRD schema
// define the common structure of all Ability CRs, excluding ability-specific content
// ability-specific config/debugOption/status schema are provided by AbilityManifest
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

// framework built-in Service CRD schema (Phase 2)
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
