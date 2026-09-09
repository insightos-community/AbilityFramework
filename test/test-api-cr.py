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

import requests
import json

# 能力CR示例
ability_cr = {
    "kind": "AtomAbility",
    "metadata": {
        "name": "MockAdd"
    },
    "spec": {
        "package": "add.example.org",
        "version": "0.1.0",
        "abilityName": "MockAdd.example.org",
        "position": "localhost",
    },
}

# API地址
url = "http://127.0.0.1:8080/api/cr" 

# 发送POST请求
response = requests.post(
    url,
    data=json.dumps(ability_cr),
    headers={"Content-Type": "application/json"}
)

# 打印结果
print("Status code:", response.status_code)
try:
    print("Response JSON:", response.json())
except Exception:
    print("Response Text:", response.text)