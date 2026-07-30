#!/usr/bin/env python
from time import sleep
import requests
import uuid
import json


def send_heartbeat(ability_name, instance_name, state, ipc_port, ability_port):
    heartbeat = {
        "abilityName": ability_name,
        "instanceName": instance_name,
        "id": "d861b579-2058-4179-ac52-2b0f19e91145",
        "state": state,
        "IPCProtocol": "http",
        "IPCPort": ipc_port,
        "abilityPort": ability_port,
    }

    headers = {"Content-Type": "application/json"}
    data1 = json.dumps(heartbeat)
    print("Sending JSON data:", data1)

    response = requests.post(
        f"http://127.0.0.1:8080/api/ability-heartbeat", headers=headers, data=data1
    )

    if response.status_code == 200:
        print("Heartbeat sent successfully!")
    else:
        print(f"Failed to send heartbeat. Status: {response.status_code}")
        print("Response content:", response.content)


if __name__ == "__main__":
    send_heartbeat("SomeAbility", "some-ability-1", "Standby", 12345, 54321)
    sleep(2)
    send_heartbeat("SomeAbility", "some-ability-1", "Running", 12345, 54321)
    sleep(2)
    send_heartbeat("SomeAbility", "some-ability-1", "Running", 12345, 54321)
