// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#include "messagebus/messagebus.hpp"
#include "prelude.hpp"
#include "util/handle_error_to_http.hpp"
#include "util/problem_detail.hpp"
#include <glog/logging.h>
#include <httplib.h>

namespace {
constexpr size_t MAX_DISPLAY_BODY_SIZE = 1024;
}
// TODO: rfc9457 standard error format
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
