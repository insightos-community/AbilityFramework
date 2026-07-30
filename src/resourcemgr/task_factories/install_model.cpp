// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#include "./install_model.hpp"
#include "glog/logging.h"
#include "taskmgr/common_tasks.hpp"
#define CHECK_TASK_PARAM(_p_Name) \
    if (!params.contains(_p_Name)) { throw std::invalid_argument("missing param " _p_Name); }

TaskPtr InstallModelByIdTaskFactory::operator()(const nlohmann::json& params) const {
    using std::filesystem::path;
    CHECK_TASK_PARAM("model_id");

    std::string id_str = params.at("model_id").is_number()
                           ? std::to_string(params.at("model_id").get<int64_t>())
                           : params.at("model_id").get<std::string>();
    auto taskname = strjoin("install-model-", id_str);
    auto local_model = mgr->get_model_info(id_str);
    if (local_model && exists(path(local_model->path))) {
        return tasks::atomic_with_return_value(taskname, [v = local_model.value()]() {
            nlohmann::json res = v;
            return res;
        });
    }
    if (!mgr->is_online()) {
        throw std::runtime_error(
            "model is not locally available, but framework can't download model in local mode"
        );
    }
    return tasks::atomic_with_return_value(taskname, [mgr = this->mgr, id_str]() {
        mgr->download_model_by_id(id_str);
        auto model = mgr->get_model_info(id_str);
        if (!model) {
            throw std::runtime_error(strjoin(
                "error downloading model id: ", id_str, ", after download, it is still empty"
            ));
        }
        nlohmann::json res = *model;
        CHECK(exists(path(model->path)))
            << "model " << model->id << "storage dir " << model->path << " does not exist!";
        return res;
    });
}
