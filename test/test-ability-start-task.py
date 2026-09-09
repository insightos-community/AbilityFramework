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

import argparse
from dataclasses import dataclass

import requests


@dataclass
class ParseRes:
    id: str
    task_type: int
    payload: dict[str, str]


def parse_arguments() -> ParseRes:
    parser = argparse.ArgumentParser(description="Process some arguments.")

    # 添加 ability_id 参数
    parser.add_argument("ability_id", type=str, help="The ability ID")

    # 添加 task_type 参数
    parser.add_argument(
        "-t", "--task-type", type=int, help="The task type as an integer"
    )

    # 添加其他 key=value 参数
    parser.add_argument(
        "params",
        nargs=argparse.REMAINDER,
        help="Additional parameters in key=value format",
    )

    # 解析参数
    args = parser.parse_args()

    # 处理 key=value 格式的参数
    param_dict = {}
    for param in args.params:
        if "=" in param:
            key, value = param.split("=", 1)
            param_dict[key] = value

    # 输出结果
    print(f"ability_id: {args.ability_id}")
    print(f"task_type: {args.task_type}")
    print("params:", param_dict)
    return ParseRes(id=args.ability_id, task_type=args.task_type, payload=param_dict)


def get_ability_port(ability_id):
    url = "http://localhost:8080/api/ability-heartbeat"

    try:
        # 发起 GET 请求
        response = requests.get(url)

        # 检查响应状态码
        response.raise_for_status()

        # 解析 JSON 响应
        data = response.json()

        # 查找对应的 abilityPort
        for ability in data:
            if ability.get("id") == ability_id:
                return ability.get("abilityPort")

        # 如果没有找到对应的 ability_id
        print(f"No ability found with id: {ability_id}")
        return None

    except requests.exceptions.RequestException as e:
        print(f"Error fetching data: {e}")
        return None


def post_start_task(port: int, params: ParseRes):
    url = f"http://localhost:{port}/api/task/start_task"

    # 构造要发送的 JSON 数据
    json_data = {"task_type": params.task_type, "payload": params.payload}

    try:
        # 发送 POST 请求
        response = requests.post(url, json=json_data)

        # 检查响应状态码
        response.raise_for_status()
        j = response.json()
        print(j)

        # 返回响应的 JSON 数据
        return response.json()

    except requests.exceptions.RequestException as e:
        print(f"Error sending POST request: {e}")
        return None


# 测试 python3 % ability123 -t 5 key1=value1 key2=value2

if __name__ == "__main__":
    args = parse_arguments()
    ability_port = get_ability_port(args.id)
    if ability_port is not None:
        print(f"Ability Port for {args.id}: {ability_port}")
    post_start_task(ability_port, args)
