from datetime import datetime
from sys import argv
from time import sleep

import requests

ALERT = {
    "abilityInfo": {
        "IPCPort": 12345,
        "IPCProtocol": "http",
        "abilityName": "SomeAbility",
        "abilityPort": 54321,
        "id": "018f996c-9c24-435a-a463-d4860d8046b8",
        "instanceName": "SomeInstance",
        "state": "Init",
        "version": "0.1.0",
    },
    "code": -250,
    "context": {"env": "xxxxxx"},
    "detail": {"some_key": "some_value"},
    "error_operation": "some_op",
    "level": "anomaly",
    "location": {"file": "test/test-ability-alert.cpp", "function": "main", "line": 22},
    "message": "msg",
    "time": "2025-05-27 16:47:09.68",
}


def send_ability_alert():
    print("Sending JSON data:", ALERT)

    response = requests.post(f"http://127.0.0.1:8080/api/ability-alert", json=ALERT)

    if response.status_code == 200:
        print("AbilityAlert sent successfully!")
    else:
        print(f"Failed to send AbilityAlert. Status: {response.status_code}")
        print("Response content:", response.content)


if __name__ == "__main__":
    send_ability_alert()
