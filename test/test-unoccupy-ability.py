import requests

# setting service's URL
url = "http://127.0.0.1:8080/api/cr/{}/occupation".format("20d3ff60-8520-442f-b228-3d4ab25477d2")

# set request parameters
data = {
    "occupy_id": "9ef71d93-f575-4cdb-8e6b-ec30d3dfb72c", 
    "mode": "shared"
}

data1 = {
    "occupy_id": "9ef71d93-f575-4cdb-8e6b-ec30d3dfb72d", 
    "mode": "shared"
}

data2 = {
    "occupy_id": "9ef71d93-f575-4cdb-8e6b-ec30d3dfb72c", 
    "mode": "unique"
}

# send POST request
response = requests.delete(url, json=data)

# printresponse content
if response.status_code == 200:
    print("Success")  
else:
    print("Failed:", response.content)  
