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

# 服务器地址
server_url = "http://localhost:8080"

# 测试 GET 请求
def test_get_discovery():
    url = f"{server_url}/api/discovery"
    response = requests.get(url)
    if response.status_code == 200:
        print("GET /api/discovery success")
        print("Response JSON:", response.json())
    else:
        print("GET /api/discovery failed")
        print("Status Code:", response.status_code)
        print("Response Text:", response.text)

# 测试 POST 请求
def test_post_discovery(system_id):
    url = f"{server_url}/api/discovery"
    headers = {"Content-Type": "application/json"}
    data = {"id": system_id}
    response = requests.post(url, headers=headers, data=json.dumps(data))
    if response.status_code == 200:
        print("POST /api/discovery success")
        print("Response JSON:", response.json())
    else:
        print("POST /api/discovery failed")
        print("Status Code:", response.status_code)
        print("Response Text:", response.text)

if __name__ == "__main__":
    # 测试 GET 请求
    test_get_discovery()

    # 测试 POST 请求，替换为实际的系统 ID
    test_post_discovery("aa8ae0fa-0258-582b-991f-24160cae8003")