#!/usr/bin/env python3
import json
import uuid
from datetime import datetime
from sys import argv
from time import sleep

import requests

TASK_ID = str(uuid.uuid4())
NOW = datetime.now()


def send_task_status():
    payload = {
        "id": TASK_ID,
        "executor_id": "4fb7d22a-8859-4be8-9570-b12d91c028b5",
        "executor_type": "ability",
        "state": "unstarted",
        "start_time": NOW.strftime("%Y-%m-%d %H:%M:%S.%f")[:-3],
        "end_time": None,
        "timeout": 100,
        "payload": {"pKey": "pValue"},
        "message": "some_message",
    }

    print("Sending JSON data:", payload)

    response = requests.post(f"http://127.0.0.1:8080/api/task-status", json=payload)

    if response.status_code == 200:
        print("TaskStatus sent successfully!")
    else:
        print(f"Failed to send TaskStatus. Status: {response.status_code}")
        print("Response content:", response.content)


def query_task_status():
    response = requests.get(f"http://127.0.0.1:8080/api/task/{TASK_ID}/status")

    if response.status_code == 200:
        print("TaskStatus query successfully!")
        print("Response content:", response.content)
    else:
        print(f"Failed to query TaskStatus. Status: {response.status_code}")
        print("Response content:", response.content)


if __name__ == "__main__":
    send_task_status()
    query_task_status()
