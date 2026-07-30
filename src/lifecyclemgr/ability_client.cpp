// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#include "lifecyclemgr/ability_client.hpp"
#include "prelude.hpp"
#include "util/ada_url.hpp"
#include <glog/logging.h>
#include <httplib.h>

namespace {

enum class SerializationMode : char { json, cbor };

std::optional<SerializationMode> get_serialization_mode(const std::string& s) {
    if (s == "json") { return SerializationMode::json; }

    if (s == "cbor") { return SerializationMode::cbor; }
    return {};
}

class HttpClient : public AbilityClient {
    std::string hostname; // ability host
    int port; // ability port
    SerializationMode serialization = SerializationMode::json;

public:
    HttpClient(
        std::string_view _hostname,
        int _port,
        SerializationMode _serialization = SerializationMode ::json
    )
        : hostname(_hostname)
        , port(_port)
        , serialization(_serialization) {}

protected:
    expected<void, std::string> do_execute(std::string_view command) override {
        httplib::Client cli(hostname, port);
        LOG(INFO) << "post " << strjoin(hostname, ":", port, "/api/lifecycle/", command);
        auto post_res = cli.Post(strjoin("/api/lifecycle/", command));
        if (!post_res) { return unexpected{to_string(post_res.error())}; }
        if (post_res->status != 200) {
            // heads up: the body here may not be human-readable
            return unexpected{"client: " + post_res->body};
        }
        return {};
    }
};

bool is_valid_lifecycle_command(std::string_view command) {
    constexpr std::string_view commands[] = {"start", "connect", "disconnect", "terminate"};
    for (auto c : commands) {
        if (c == command) { return true; }
    }
    return false;
}

} // namespace

expected<void, std::string> AbilityClient::execute(std::string_view command) {
    if (!is_valid_lifecycle_command(command)) {
        return make_unexpected("invalid lifecycle command: ", command);
    }

    return do_execute(command);
}

std::shared_ptr<AbilityClient> AbilityClient::make(const Config& config) {
    auto url = ada::parse(config.url);
    if (!url) {
        LOG(ERROR) << "client url is inavlid : " << config.url;
        return nullptr;
    }
    if (url->get_protocol() == "http:") {

        auto hostname = url->get_hostname();
        if (hostname.empty()) {
            LOG(ERROR) << "client host is empty";
            return nullptr;
        }
        std::string port_str{url->get_port()};
        if (port_str.empty()) {
            LOG(ERROR) << "client port is empty";
            return nullptr;
        }
        auto port = stoi(port_str);
        // currently we assume ability httpapi accepts json parameters
        auto res = std::make_shared<HttpClient>(hostname, port);
        return res;
    }
    if (url->get_protocol() == "coap:") {
        LOG(ERROR) << "coap client is not supported yet";
        return nullptr;
    }
    LOG(ERROR) << "unsupported client protocol: " << url->get_protocol();
    return nullptr;
}
