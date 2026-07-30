import argparse
from dataclasses import dataclass

import requests


@dataclass
class ParseRes:
    id: str
    task_type: int
    payload: dict[str, str]


def parse_arguments() -> ParseRes:
    parser = argparse.ArgumentParser(description="Process some arguments.")

    # add ability_id parameter
    parser.add_argument("ability_id", type=str, help="The ability ID")

    # add task_type parameter
    parser.add_argument(
        "-t", "--task-type", type=int, help="The task type as an integer"
    )

    # addother key=value parameter
    parser.add_argument(
        "params",
        nargs=argparse.REMAINDER,
        help="Additional parameters in key=value format",
    )

    # parse parameters
    args = parser.parse_args()

    # handle key=value format parameters
    param_dict = {}
    for param in args.params:
        if "=" in param:
            key, value = param.split("=", 1)
            param_dict[key] = value

    # outputresult
    print(f"ability_id: {args.ability_id}")
    print(f"task_type: {args.task_type}")
    print("params:", param_dict)
    return ParseRes(id=args.ability_id, task_type=args.task_type, payload=param_dict)


def get_ability_port(ability_id):
    url = "http://localhost:8080/api/ability-heartbeat"

    try:
        # initiate GET request
        response = requests.get(url)

        # check response status code
        response.raise_for_status()

        # parse JSON response
        data = response.json()

        # find the corresponding abilityPort
        for ability in data:
            if ability.get("id") == ability_id:
                return ability.get("abilityPort")

        # if the corresponding ability_id is not found
        print(f"No ability found with id: {ability_id}")
        return None

    except requests.exceptions.RequestException as e:
        print(f"Error fetching data: {e}")
        return None


def post_start_task(port: int, params: ParseRes):
    url = f"http://localhost:{port}/api/task/start_task"

    # construct the JSON data to send
    json_data = {"task_type": params.task_type, "payload": params.payload}

    try:
        # send POST request
        response = requests.post(url, json=json_data)

        # check response status code
        response.raise_for_status()
        j = response.json()
        print(j)

        # return the response JSON data
        return response.json()

    except requests.exceptions.RequestException as e:
        print(f"Error sending POST request: {e}")
        return None


# test: python3 % ability123 -t 5 key1=value1 key2=value2

if __name__ == "__main__":
    args = parse_arguments()
    ability_port = get_ability_port(args.id)
    if ability_port is not None:
        print(f"Ability Port for {args.id}: {ability_port}")
    post_start_task(ability_port, args)
