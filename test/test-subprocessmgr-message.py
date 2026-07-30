#!/usr/bin/env python
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
