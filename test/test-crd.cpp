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

#include "resourcemgr/ability_crd.hpp"
#include <iostream>

int main() {
    AbilityCRD crd;
    std::string jsonStr = R"({
        "package": "example.package",
        "kind": "ExampleKind",
        "metadata": {
            "name": "ExampleAbility"
        },
        "depends": {
            "abilities": [
                {
                    "package": "dep.package",
                    "abilityName": "DepAbility",
                    "ownership": "unique",
                    "bind": "local",
                    "description": "Primary dependency"
                }
            ],
            "additionalAbilities": [
                {
                    "package": "extra.package",
                    "abilityName": "ExtraAbility",
                    "ownership": "shared",
                    "bind": "remote",
                    "description": "Additional capability dependency"
                }
            ],
            "num_ability_is_arbitrary": false,
            "devices": [
                {
                    "package": "device.package",
                    "deviceName": "Device1",
                    "ownership": "unique",
                    "bind": "local",
                    "description": "Primary device dependency"
                }
            ],
            "additionalDevices": [
                {
                    "package": "device.package",
                    "deviceName": "Device2",
                    "ownership": "shared",
                    "bind": "remote",
                    "description": "Secondary device dependency"
                }
            ]
        },
        "spec": {
            "provides": {},
            "schema": {
                "constants": {
                    "exampleKey": "exampleValue"
                },
                "openAPIV3Schema": {
                    "type": "object",
                    "properties": {
                        "name": { "type": "string" },
                        "status": { "type": "string" }
                    }
                }
            }
        },
        "fwk": {
            "complete": true,
            "ref_expanded": false
        }
    })";
    nlohmann::json j = nlohmann::json::parse(jsonStr);
    from_json(j, crd);
    nlohmann::json j2;
    to_json(j2, crd);
    std::cout << j2.dump(4) << std::endl;
    return 0;
}