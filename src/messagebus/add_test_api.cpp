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

#include "messagebus/messagebus.hpp"
#include "util/make_uuid.hpp"
#include <glog/logging.h>
#include <httplib.h>

namespace message_bus {
namespace {
struct ExternalMessage {
    std::string source;
    std::string destination;
    std::string operation;
    std::string payload;
    bool synchronous;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(
        ExternalMessage, source, destination, operation, payload, synchronous
    );
    auto submit_sync() {
        message_bus::Message message{
            .header{
                .id = make_uuid(),
                .parent_id{},
                .timestamp{std::chrono::system_clock::now()},
                .is_synchronous = false
            },
            .router{.source = source, .destination = destination, .operation = operation},
            .content{payload.begin(), payload.end()}
        };
        return message_bus::send_sync(std::move(message));
    };
    auto submit() {
        message_bus::Message message{
            .header{
                .id = make_uuid(),
                .parent_id{},
                .timestamp{std::chrono::system_clock::now()},
                .is_synchronous = false
            },
            .router{.source = source, .destination = destination, .operation = operation},
            .content{payload.begin(), payload.end()}
        };
        return message_bus::send(message);
    }
};
} // namespace

void add_test_api(httplib::Server& server) {
    server.Post(
        "/api/internal/test-message",
        [](const httplib::Request& req, httplib::Response& res) {
            auto msg = nlohmann::json::parse(req.body).get<ExternalMessage>();
            if (msg.synchronous) {
                auto result = msg.submit_sync();
                if (result) {
                    res.set_content(
                        result->content.data(), result->content.size(), "application/json"
                    );
                    return;
                }
                else {
                    res.status = 500;
                    res.set_content(result.error(), "text/plain");
                    return;
                }
            }
            else {
                auto result = msg.submit();
                if (result) {
                    res.set_content("OK", "text/plain");
                    return;
                }
                if (!result) {
                    res.status = 500;
                    res.set_content(result.error(), "text/plain");
                    return;
                }
            }
        }
    );
}

} // namespace message_bus
