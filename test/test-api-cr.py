import requests
import json

# abilityCRexample
ability_cr = {
    "kind": "AtomAbility",
    "metadata": {
        "name": "MockAdd"
    },
    "spec": {
        "package": "add.example.org",
        "version": "0.1.0",
        "abilityName": "MockAdd.example.org",
        "position": "localhost",
    },
}

# APIaddress
url = "http://127.0.0.1:8080/api/cr" 

# sendPOSTrequest
response = requests.post(
    url,
    data=json.dumps(ability_cr),
    headers={"Content-Type": "application/json"}
)

# printresult
print("Status code:", response.status_code)
try:
    print("Response JSON:", response.json())
except Exception:
    print("Response Text:", response.text)