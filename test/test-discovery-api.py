import requests
import json

# serveraddress
server_url = "http://localhost:8080"

# test GET request
def test_get_discovery():
    url = f"{server_url}/api/discovery"
    response = requests.get(url)
    if response.status_code == 200:
        print("GET /api/discovery success")
        print("Response JSON:", response.json())
    else:
        print("GET /api/discovery failed")
        print("Status Code:", response.status_code)
        print("Response Text:", response.text)

# test POST request
def test_post_discovery(system_id):
    url = f"{server_url}/api/discovery"
    headers = {"Content-Type": "application/json"}
    data = {"id": system_id}
    response = requests.post(url, headers=headers, data=json.dumps(data))
    if response.status_code == 200:
        print("POST /api/discovery success")
        print("Response JSON:", response.json())
    else:
        print("POST /api/discovery failed")
        print("Status Code:", response.status_code)
        print("Response Text:", response.text)

if __name__ == "__main__":
    # test GET request
    test_get_discovery()

    # test POST request, replaceas the actual system ID
    test_post_discovery("aa8ae0fa-0258-582b-991f-24160cae8003")