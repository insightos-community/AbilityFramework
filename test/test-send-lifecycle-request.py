#!/usr/bin/env python3
import json
import uuid
from sys import argv
from time import sleep

import requests


def send_lifecycle_request(instance_id: str, command: str):
    payload = {
        "abilityInstanceId": instance_id,
        "command": command,
    }

    print("Sending JSON data:", payload)

    response = requests.post(
        f"http://127.0.0.1:8080/api/lifecycle-request", json=payload
    )

    if response.status_code == 200:
        print("Heartbeat sent successfully!")
        print("Response content:", response.content)
    else:
        print(f"Failed to send heartbeat. Status: {response.status_code}")
        print("Response content:", response.content)


if __name__ == "__main__":
    if len(argv) < 3:
        print(f"usage: {__file__} <instance-id> <cmd>")
        exit()

    print(argv)
    send_lifecycle_request(argv[1], argv[2])
