#!/usr/bin/env python
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

from time import sleep
import requests
import json


def send_heartbeat(controllerName, ipc_port, IPCProtocol):
    
    ''' //未修改时的心跳包
//控制器心跳
 struct ControllerHeartbeat{
    int port; // 控制器基础服务器对应的端口
    std::string controllerInstanceId;//控制器的实例id
    std::string package;//控制器所对应能力的包名称
    std::string version;//控制器所对应能力的版本名
    std::string abilityName;//控制器所对应能力的类型名
    std::string protocol;//控制器所使用的协议
 };
    '''
    
    ##修改后的心跳包
    heartbeat = {
        "port": 12345,
        "controllerInstanceId": '5527489887745441dad5',
        "package": 'musicAbility package',
        "version": '1.0',
        "abilityName": 'musicAbility',
        "protocol": 'http'
    }

    headers = {"Content-Type": "application/json"}

    data1 = json.dumps(heartbeat)
    print("Sending JSON data:", data1)

    response = requests.post(
        f"http://127.0.0.1:8080/api/controller-heartbeat", headers=headers, data=data1
    )

    if response.status_code == 200:
        print("Heartbeat sent successfully!")
    else:
        print(f"Failed to send heartbeat. Status: {response.status_code}")
        print("Response content:", response.content)


if __name__ == "__main__":
    send_heartbeat("SomeController", 12345, "http")
    sleep(2)
    send_heartbeat("SomeController", 12345, "http")
    sleep(2)
    send_heartbeat("SomeController", 12345, "http")

