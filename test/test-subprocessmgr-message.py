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
import uuid
import json


def send_message(path, args=[], envs={}, labels={}):
    task_payload = {"path": path, "args": args, "envs": envs, "labels": labels}
    message = {
        "source": "external",
        "destination": "SubprocessMgr",
        "operation": "start_process",
        "payload": json.dumps(task_payload),
        "synchronous": True,
    }

    response = requests.post(
        f"http://127.0.0.1:8080/api/internal/test-message", json=message
    )

    if response.status_code == 200:
        print("message sent successfully!")
    else:
        print(f"Failed to send message. Status: {response.status_code}")
        print("Response content:", response.content)


if __name__ == "__main__":
    send_message("ls")
    send_message("sleep", args=["3"])
