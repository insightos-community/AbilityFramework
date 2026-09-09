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

#include "resourcemgr/ability_cr.hpp"
#include <iostream>

int main() {
    AbilityCR cr;
    std::string jsonStr = R"({
        "id": "123e4567-e89b-12d3-a456-426614174000",
        "kind": "ComposedAbility",
        "metadata": {
            "name": "camera1",
            "labels": {
                "position": "right",
                "color": "red"
            }
        },
        "status": {},
        "spec": {
            "package": "example.package",
            "version": "0.0.1",
            "abilityName": "ExampleAbility",
            "position": "localhost",
            "config": {},
            "subabilities": [
                {
                    "package": "sub.package1",
                    "version": "0.0.1",
                    "abilityName": "SubAbility1",
                    "position": "localhost",
                    "config": {},
                    "devices": [
                        {
                            "deviceName": "Device1",
                            "instanceName": "Instance1",
                            "ownership": "unique",
                            "bind": "local"
                        },
                        {
                            "deviceName": "Device4",
                            "instanceName": "Instance4",
                            "ownership": "unique",
                            "bind": "local"
                        }
                    ]
                },
                {
                    "package": "sub.package2",
                    "version": "0.0.1",
                    "abilityName": "SubAbility2",
                    "position": "localhost",
                    "config": {}
                }
            ],
            "devices": [
                {
                    "deviceName": "Device2",
                    "instanceName": "Instance2",
                    "ownership": "unique",
                    "bind": "local"
                },
                {
                    "deviceName": "Device3",
                    "instanceName": "Instance3",
                    "ownership": "unique",
                    "bind": "local"
                }
            ]
        }
    })";
    nlohmann::json j = nlohmann::json::parse(jsonStr);
    from_json(j, cr);
    nlohmann::json j2;
    to_json(j2, cr);
    std::cout << j2.dump(4) << std::endl;
    return 0;
}
