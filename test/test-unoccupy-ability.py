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

# 设置服务器的 URL
url = "http://127.0.0.1:8080/api/cr/{}/occupation".format("20d3ff60-8520-442f-b228-3d4ab25477d2")

# 设置请求的参数
data = {
    "occupy_id": "9ef71d93-f575-4cdb-8e6b-ec30d3dfb72c", 
    "mode": "shared"
}

data1 = {
    "occupy_id": "9ef71d93-f575-4cdb-8e6b-ec30d3dfb72d", 
    "mode": "shared"
}

data2 = {
    "occupy_id": "9ef71d93-f575-4cdb-8e6b-ec30d3dfb72c", 
    "mode": "unique"
}

# 发送 POST 请求
response = requests.delete(url, json=data)

# 打印响应内容
if response.status_code == 200:
    print("Success")  
else:
    print("Failed:", response.content)  
