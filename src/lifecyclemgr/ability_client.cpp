// Copyright 2026 InsightOS
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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
    std::string hostname; // 能力所在主机
    int port;             // 能力所在端口
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
        if (!post_res) { return ::semantic_expected::unexpected{to_string(post_res.error())}; }
        if (post_res->status != 200) {
            // 留个心眼.这里的body不见得是人类可读的格式
            return ::semantic_expected::unexpected{"client: " + post_res->body};
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
        // 目前我们相信能力httpapi接受的都是json参数
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
