#!/usr/bin/env python
from time import sleep
import requests
import json


def send_heartbeat(controllerName, ipc_port, IPCProtocol):
    
    ''' // do not modify the heartbeat packet at
//controller heartbeat
 struct ControllerHeartbeat{
    int port; // port of the controller base server
    std::string controllerInstanceId;//controller instance id
    std::string package;//package name of the ability the controller corresponds to
    std::string version;//version name of the ability the controller corresponds to
    std::string abilityName;//type name of the ability the controller corresponds to
    std::string protocol;//protocol used by the controller
 };
    '''
    
    ## modify the heartbeat packet after
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

