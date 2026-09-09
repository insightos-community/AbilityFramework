# Copyright 2026 InsightOS
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from datetime import datetime
from sys import argv
from time import sleep

import requests

ALERT = {
    "abilityInfo": {
        "IPCPort": 12345,
        "IPCProtocol": "http",
        "abilityName": "SomeAbility",
        "abilityPort": 54321,
        "id": "018f996c-9c24-435a-a463-d4860d8046b8",
        "instanceName": "SomeInstance",
        "state": "Init",
        "version": "0.1.0",
    },
    "code": -250,
    "context": {"env": "xxxxxx"},
    "detail": {"some_key": "some_value"},
    "error_operation": "some_op",
    "level": "anomaly",
    "location": {"file": "test/test-ability-alert.cpp", "function": "main", "line": 22},
    "message": "msg",
    "time": "2025-05-27 16:47:09.68",
}


def send_ability_alert():
    print("Sending JSON data:", ALERT)

    response = requests.post(f"http://127.0.0.1:8080/api/ability-alert", json=ALERT)

    if response.status_code == 200:
        print("AbilityAlert sent successfully!")
    else:
        print(f"Failed to send AbilityAlert. Status: {response.status_code}")
        print("Response content:", response.content)


if __name__ == "__main__":
    send_ability_alert()
