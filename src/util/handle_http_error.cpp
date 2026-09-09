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
#include "prelude.hpp"
#include "util/handle_error_to_http.hpp"
#include "util/problem_detail.hpp"
#include <glog/logging.h>
#include <httplib.h>

namespace {
constexpr size_t MAX_DISPLAY_BODY_SIZE = 1024;
}
// TODO: rfc9457标准报错格式
void handle_error_to_http(
    const httplib::Request& req, httplib::Response& res, std::exception_ptr eptr
) noexcept {
    LOG(WARNING) << "in " << req.method << " " << req.path << " with payload:\n"
                 << ((req.body.size() <= 1024)
                         ? req.body
                         : strjoin("body is too large: ", req.body.size(), " bytes"));
    try {
        std::rethrow_exception(eptr);
    }
    catch (ProblemDetail& pd) {
        if (pd.instance.empty()) { pd.instance = req.path; }
        res.status = pd.status;
        res.set_content(nlohmann::json(pd).dump(2), "application/json");
    }
    catch (std::invalid_argument& e) {
        LOG(ERROR) << "respond error to http:" << e.what();
        LOG_IF(ERROR, req.body.size() < MAX_DISPLAY_BODY_SIZE) << "body is:\n" << req.body;
        ProblemDetail pd{400, "exception", "invalid argument", e.what(), req.path};
        res.set_content(nlohmann::json(pd).dump(2), "application/json");
        res.status = 400;
    }
    catch (message_bus::FormatError& e) {
        LOG(ERROR) << "respond error to http:" << e.what();
        LOG_IF(ERROR, req.body.size() < MAX_DISPLAY_BODY_SIZE) << "body is:\n" << req.body;
        ProblemDetail pd{500, "exception", "invalid message format", e.what(), req.path};
        auto payload = nlohmann::json(pd);
        payload["message_content"] = e.content;
        res.set_content(payload.dump(2), "application/json");
        res.status = 500;
    }
    catch (std::exception& e) {
        LOG(ERROR) << "respond error to http:" << e.what();
        LOG_IF(ERROR, req.body.size() < MAX_DISPLAY_BODY_SIZE) << "body is:\n" << req.body;
        ProblemDetail pd{500, "exception", "exception", e.what(), req.path};
        res.set_content(nlohmann::json(pd).dump(2), "application/json");
        res.status = 500;
    }
    catch (...) {
        ProblemDetail pd{500, "internal error", "Unknown exception", "", req.path};
        res.set_content(nlohmann::json(pd).dump(2), "application/json");
        res.status = 500;
    }
}
