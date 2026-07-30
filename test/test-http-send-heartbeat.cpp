#include "util/make_uuid.hpp"
#include <httplib.h>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>

using json = nlohmann::json;

class HeartbeatSender {
public:
    uuids::uuid id;
    HeartbeatSender()
        : id(make_uuid()) {}

    void send_heartbeat(
        const std::string& server_address,
        const std::string& ability_name,
        const std::string& instance_name,
        const std::string& state,
        int ipc_port,
        int ability_port
    ) {
        httplib::Client cli(server_address.c_str());

        json heartbeat = {{"abilityName", ability_name}, {"instanceName", instance_name},
                          {"id", to_string(id)},         {"state", state},
                          {"IPCProtocol", "http"},       {"IPCPort", ipc_port},
                          {"abilityPort", ability_port}};

        auto res = cli.Post("/api/ability-heartbeat", heartbeat.dump(), "application/json");

        if (res && res->status == 200) { std::cout << "Heartbeat sent successfully!" << std::endl; }
        else {
            std::cerr << "Failed to send heartbeat. Status: " << (res ? res->status : 0)
                      << std::endl;
        }
    }
};

int main() {
    HeartbeatSender sender;
    sender.send_heartbeat(
        "localhost:8080", "SomeAbility", "some-ability-1", "Running", 12345, 54321
    );
    return 0;
}
