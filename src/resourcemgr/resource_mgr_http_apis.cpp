// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#include "databasemgr/database_mgr.hpp"
#include "json_schema_utils.hpp"
#include "resourcemgr/builtin_crds.hpp"
#include "lifecyclemgr/lifecycle_request.hpp"
#include "messagebus/message_client.hpp"
#include "resourcemgr/ability_manifest.hpp"
#include "resourcemgr/resource_mgr.hpp"
#include "resourcemgr/service_cr.hpp"
#include "taskmgr/common_tasks.hpp"
#include "util/make_uuid.hpp"
#include "util/problem_detail.hpp"
#include <glog/logging.h>
using httplib::Request, httplib::Response;
using namespace std::placeholders;

namespace {
bool filter_cr(const std::unordered_map<std::string, std::string>& params, const AbilityCR& cr) {
    auto abilityName = params.find("abilityName");
    if (abilityName != params.end() && cr.spec->abilityName != abilityName->second) {
        return false;
    }
    auto version = params.find("version");
    if (version != params.end() && to_string(cr.spec->version) != version->second) { return false; }
    auto package = params.find("package");
    if (package != params.end() && cr.spec->package != package->second) { return false; }
    auto nodeId = params.find("nodeId");
    if (nodeId != params.end() && cr.metadata.labels.at("fwk.io/onNode/id") != nodeId->second) {
        return false;
    }
    auto nodeName = params.find("nodeName");
    if (nodeName != params.end() && cr.metadata.labels.contains("fwk.io/onNode/name")
        && cr.metadata.labels.at("fwk.io/onNode/name") != nodeName->second) {
        return false;
    }
    auto nodeAddr = params.find("nodeAddr");
    if (nodeAddr != params.end() && cr.spec->position != nodeAddr->second) { return false; }

    return true;
}

bool is_ability(std::string_view kind) {
    return kind.find("Ability") != kind.npos;
}

// save json as yaml file
void save_json_as_yaml(const nlohmann::json& j) {
    YAML::Node node = json_to_yaml(j);
    YAML::Emitter out;
    out << node;
    // generate filename
    std::string name = j["metadata"].value("name", "unknown");
    std::string filename = "crs/" + name + ".yaml";
    // LOG(ERROR) << "filename: " << filename;
    std::ofstream fout(filename);
    fout << out.c_str();
    fout.close();
}

struct IDEntry {
    uuids::uuid raw;
    std::string str;
    void set(const std::string& s) {
        str = s;
        raw = uuids::uuid::from_string(s).value();
    }
};
std::vector<TaskPtr> prepare_task_start_ability(
    std::shared_ptr<ResourceManager> mgr, AbilityCR cr, bool start, bool connect
) {
    using nlohmann::json;
    using TaskBodyFunc = std::function<nlohmann::json()>;
    std::vector<TaskPtr> tasks;

    auto id_entry = std::make_shared<IDEntry>();
    // 1. template(CR) is managed via package/yaml, not written at runtime. Directly derive a fresh instance_id from the template.
    auto task_create = tasks::atomic(
        "create-instance",
        std::function<nlohmann::json()>([cr, mgr, id_entry]() mutable -> nlohmann::json {
            auto instance_id = mgr->create_ability_instance(cr);
            id_entry->raw = instance_id;
            id_entry->str = to_string(instance_id);
            return nlohmann::json();
        })
    );
    tasks.push_back(task_create);

    if (!start && !connect) {
        // only creates an instance; does not spawn a process
        tasks.push_back(tasks::atomic(
            "noop-start", TaskBodyFunc([id_entry]() -> nlohmann::json {
                return json{{"abilityInstanceId", id_entry->str},
                            {"taskAction", "add instance without starting"}};
            })
        ));
        return tasks;
    }

    // 2. send lifecycle "start", let LifecycleMgr spawn the subprocess
    tasks.push_back(tasks::atomic(
        "start-ability", TaskBodyFunc([id_entry]() -> nlohmann::json {
            LifecycleRequest payload{.abilityInstanceId = id_entry->raw, .command = "start"};
            auto res = send_sync(
                make_message("ResourceMgr", "LifecycleMgr", "lifecycle_request", payload)
            );
            on_error(res, [](std::string_view err_msg) {
                LOG(ERROR) << "send lifecycle_request start failed: " << err_msg;
            });
            return json{{"abilityInstanceId", id_entry->str}, {"taskAction", "start ability"}};
        })
    ));

    if (!connect) { return tasks; }

    // 3. wait for this instance to enter Standby (or later). Only Standby semantically means "the IPC portal is already bound,"
    // can accept connect command". etc. Unknown/Init then send connect will hit an unbound port → refused →
    // ability stays in Standby forever. Each task chain polls its own instance_id, up to 30s.
    auto inst_id_ptr = id_entry; // capture by value
    tasks.push_back(tasks::wait_once(
        "wait-standby",
        [mgr, inst_id_ptr]() -> bool {
            auto info = mgr->get_ability_instance(inst_id_ptr->raw);
            if (!info) { return false; }
            return info->state == "Standby" || info->state == "Running";
        },
        std::chrono::seconds(30),
        [inst_id_ptr]() {
            return std::string{"wait instance "} + inst_id_ptr->str + " reach Standby timeout";
        }
    ));

    // 4. send lifecycle "connect", pushing the status to Running
    tasks.push_back(tasks::atomic(
        "connect-ability", TaskBodyFunc([id_entry]() -> nlohmann::json {
            LifecycleRequest payload{.abilityInstanceId = id_entry->raw, .command = "connect"};
            auto res = send_sync(
                make_message("ResourceMgr", "LifecycleMgr", "lifecycle_request", payload)
            );
            on_error(res, [](std::string_view err_msg) {
                LOG(ERROR) << "send lifecycle_request connect failed: " << err_msg;
            });
            return json{{"abilityInstanceId", id_entry->str}, {"taskAction", "connect ability"}};
        })
    ));
    return tasks;
}

TaskPtr make_task_start_ability(
    std::shared_ptr<ResourceManager> mgr, AbilityCR cr, bool start, bool connect
) {
    using nlohmann::json;
    auto tasks = prepare_task_start_ability(mgr, cr, start, connect);
    return tasks::sequence("auto-start-ability", tasks);
}

TaskPtr make_task_download_pkg_and_start_ability(
    std::shared_ptr<ResourceManager> mgr, const AbilityCR& cr, bool start, bool connect
) {
    using nlohmann::json;
    std::vector<TaskPtr> tasks;
    // task1:download package
    auto task_1
        = mgr->task_download_package(PackageSpec{cr.spec->package, cr.spec->version.to_string()});
    tasks.push_back(task_1);

    // start the ability after download completes
    auto tasks_start_ability = prepare_task_start_ability(mgr, cr, start, connect);
    std::copy(tasks_start_ability.begin(), tasks_start_ability.end(), std::back_inserter(tasks));

    auto total_task = tasks::sequence("download-start-ability", tasks);
    return total_task;
}

nlohmann::json parse_json_from_req(const Request& req) {
    auto content_type = req.get_header_value("Content-Type");
    if (content_type == "application/json") { return nlohmann::json::parse(req.body); }
    if (content_type == "application/yaml") {
        auto y = YAML::Load(req.body);
        return yaml_to_json(y);
    }
    return nlohmann::json::parse(req.body);
}

void build_api_cr_file(httplib::Server& server) {
    using nlohmann::json;
    server.Get("/api/crs", [](const Request& req, Response& res) {
        std::vector<std::string> cr_files;
        // get the crs directory path
        auto crs_path = global_vars::home_path() / "crs";
        if (!std::filesystem::exists(crs_path)) {
            res.set_content(json{{"error", "crs directory not found"}}.dump(), "application/json");
            return;
        }
        // scan all yaml files in the crs directory
        for (const auto& entry : std::filesystem::directory_iterator(crs_path)) {
            if (!entry.is_regular_file()) continue;
            if (!extension_is_yaml(entry.path())) continue;
            cr_files.push_back(entry.path().filename().string());
        }
        json result;
        result["files"] = cr_files;
        result["total"] = cr_files.size();
        res.set_content(result.dump(), "application/json");
    });

    server.Post("/api/crs/autostart", [](const Request& req, Response& res) {
        auto body = json::parse(req.body);
        if (!body.contains("filename") || !body["filename"].is_string()) {
            throw std::invalid_argument("request body must contain string 'filename' field");
        }
        if (!body.contains("autoStart") || !body["autoStart"].is_boolean()) {
            throw std::invalid_argument("request body must contain boolean 'autoStart' field");
        }
        std::string filename = body["filename"].get<std::string>();
        bool autoStart = body["autoStart"].get<bool>();
        // build the full file path
        auto crs_path = global_vars::home_path() / "crs";
        auto file_path = crs_path / filename;
        if (!std::filesystem::exists(file_path)) {
            throw std::invalid_argument("CR file not found: " + file_path.string());
        }
        try {
            // use atomic operation: write to a temp file first, then atomically replace the original
            auto temp_file_path = file_path;
            temp_file_path += ".tmp";
            auto op_yaml = read_yaml_from_path(file_path);
            if (!op_yaml) { throw std::runtime_error("failed to read yaml file"); }
            YAML::Node yaml_node = *op_yaml;
            // check and modify the autoStart field
            if (yaml_node["spec"]["autoStart"]) {
                yaml_node["spec"]["autoStart"] = autoStart;
                LOG(INFO) << "Updated existing autoStart field to "
                          << (autoStart ? "true" : "false");
            }
            else {
                // ifautoStartfielddoes not exist, add
                yaml_node["spec"]["autoStart"] = autoStart;
                LOG(INFO) << "Added new autoStart field with value "
                          << (autoStart ? "true" : "false");
            }
            // write to temp file first
            std::ofstream temp_file(temp_file_path);
            if (!temp_file.is_open()) {
                throw std::runtime_error("Failed to create temporary file");
            }
            temp_file << yaml_node;
            temp_file.close();
            temp_file.flush();
            if (temp_file.fail()) {
                std::filesystem::remove(temp_file_path);
                throw std::runtime_error("Failed to flush temporary file");
            }
            const int max_retries = 3;
            const auto retry_delay = std::chrono::milliseconds(100);
            std::error_code ec;
            for (int attempt = 1; attempt <= max_retries; ++attempt) {
                ec.clear();
                std::filesystem::rename(temp_file_path, file_path, ec);
                if (!ec) {
                    LOG(INFO) << "Successfully replaced file on attempt " << attempt;
                    break;
                }
                if (attempt == max_retries) {
                    std::filesystem::remove(temp_file_path);
                    throw std::runtime_error(
                        "Failed to replace original file after " + std::to_string(max_retries)
                        + " attempts: " + ec.message()
                    );
                }
                std::this_thread::sleep_for(retry_delay);
            }

            // return success response
            json result;
            result["success"] = true;
            result["filename"] = filename;
            result["autoStart"] = autoStart;
            res.set_content(result.dump(), "application/json");
        }
        catch (const std::exception& e) {
            throw ProblemDetail{
                .status = 500,
                .type = "file_operation_error",
                .title = "File operation error",
                .detail = std::string("Failed to modify CR file: ") + e.what(),
                .instance = filename
            };
        }
    });
}

template <typename M>
bool has_kind_device(const M& params) {
    auto it = params.find("kind");
    return it != params.end() && (it->second == "device" || it->second == "Device");
}

} // namespace

void build_api_cr(std::shared_ptr<ResourceManager> mgr, httplib::Server& server) {
    CHECK_NOTNULL(mgr);
    using httplib::Request, httplib::Response;
    using nlohmann::json;
    using Handler = httplib::Server::Handler;

    server.Get("/api/cr/:id", [mgr](const Request& req, Response& res) {
        auto uuid = uuids::uuid::from_string(req.path_params.at("id"));
        if (!uuid) { throw std::invalid_argument("invalid uuid"); }
        // prefer restoring from AbilityInstance.spec_snapshot by instance_id; fall back to CR template table if not found.
        // thus the Python SDK (after startup, uses its own instance_id to GET /api/cr/<id>) can directly get its own spec.
        if (auto result = mgr->resolve_ability_cr_by_id(*uuid); result) {
            res.set_content(json(*result).dump(), "application/json");
            return;
        }
        if (auto result = mgr->get_device_cr(*uuid); result) {
            res.set_content(json(*result).dump(), "application/json");
            return;
        }
        throw ProblemDetail{
            .status = 404,
            .type = "non_exist_cr",
            .title = "cr does not exist",
            .detail = "",
            .instance = to_string(*uuid)
        };
    });

    server.Get("/api/cr", [mgr](const Request& req, Response& res) {
        std::unordered_map<std::string, std::string> params;
        for (const auto& param : req.params) {
            params[param.first] = param.second;
        }
        if (has_kind_device(params)) {
            // TODO: device cr filter options
            std::vector<DeviceCR> res_array = mgr->get_all_device_cr();
            res.set_content(json(res_array).dump(), "application/json");
        }
        else {
            std::vector<AbilityCR> res_array = mgr->get_all_ability_cr();
            std::vector<AbilityCR> res_cr;

            for (const auto& cr : res_array) {
                if (filter_cr(params, cr)) { res_cr.emplace_back(cr); }
            }
            res.set_content(json(res_cr).dump(), "application/json");
            return;
        }
    });

    server.Get("/api/device_crs", [mgr](const Request& req, Response& res) {
        std::vector<DeviceCR> res_array = mgr->get_all_device_cr();
        res.set_content(json(res_array).dump(), "application/json");
        return;
    });

    // POST /api/cr deprecated: CR templates can only be injected via package/yaml inject,
    // run only allows deriving an instance via POST /api/instance.
    server.Post("/api/cr", [](const Request& req, Response& res) {
        res.status = 410;
        res.set_content(
            R"({"error":"endpoint removed","detail":"CR templates can only be injected via package/yaml inject.pleaseuse POST /api/instance derive a runtime instance from an existing template."})",
            "application/json"
        );
    });

    // DELETE /api/cr deprecated: CR by yaml file (or futureability package) determines lifecycle,
    // to delete a template, please delete the yaml file directly; the framework will auto-reconcile on next startup.
    // runonly allowsvia DELETE /api/instance/:id destroy instance.
    server.Delete("/api/cr/:id", [](const Request& req, Response& res) {
        res.status = 410;
        res.set_content(
            R"({"error":"endpoint removed","detail":"CR template lifecycle is managed by package/yaml files. To delete a template, delete the corresponding yaml afterrestartframework, by reconcile done."})",
            "application/json"
        );
    });

    server.Get("/ability/:id/adjust-status", [mgr](const Request& req, Response& res) {
        UnImplemented("Get(/ability/:id/adjust-status)")
        // std::string instance_id = req.path_params.at("id");
        // std::string redis_key = "/ability/" + instance_id + "/adjust-status";
        // std::string status = mgr->redis.get(redis_key);

        // if (status.empty()) { res.set_content("null", "application/json"); }
        // else { res.set_content(status, "application/json"); }
    });
    build_api_cr_file(server);
}

namespace {

OwnershipMode parse_ownership_mode(std::string_view sv) {
    using namespace std::string_view_literals;
    std::string s{sv};
    for (auto& c : s) {
        c = tolower(c);
    }
    if (s == "shared"sv) { return OwnershipMode::shared; }
    if (s == "unique"sv) { return OwnershipMode::unique; }
    throw std::invalid_argument(strjoin("invalid ownership mode: ", sv));
}

OwnershipMode parse_ownership_mode(const nlohmann::json& j) {
    return parse_ownership_mode(j.get<std::string_view>());
}
} // namespace

void build_api_occupation(std::shared_ptr<ResourceManager> mgr, httplib::Server& server) {
    using httplib::Request, httplib::Response;
    server.Get("/api/cr/:id/occupation", [mgr](const Request& req, Response& res) {
        auto uuid = uuids::uuid::from_string(req.path_params.at("id"));
        if (!uuid) { throw std::invalid_argument("invalid uuid"); }
        std::unordered_map<uuids::uuid, std::string> sharers;
        std::optional<ResourceManager::OwnerInfo> owner;

        if (mgr->judge_ability_exist(*uuid)) {
            sharers = mgr->get_ability_sharers(*uuid);
            owner = mgr->get_ability_owner(*uuid);
        }
        else if (mgr->judge_device_exist(*uuid)) {
            sharers = mgr->get_device_sharers(*uuid);
            owner = mgr->get_device_owner(*uuid);
        }
        else {
            throw ProblemDetail{
                .status = 404,
                .type = "non_exist_occupyee_cr",
                .title = "Non-exist occupyee cr",
                .detail = "the cr to ocuupy does not exist",
                .instance = to_string(*uuid)
            };
        }

        nlohmann::json result;
        result["sharers"] = nlohmann::json::object();
        result["owner"] = nlohmann::json::object();

        for (const auto& [key, value] : sharers) {
            result["sharers"][to_string(key)] = value;
        }
        if (owner.has_value()) {
            result["owner"]["abilityInstanceId"] = to_string(owner->id);
            result["owner"]["position"] = owner->position;
        }
        res.set_content(result.dump(), "application/json");
        return;
    });

    server.Post("/api/cr/:id/occupation", [mgr](const Request& req, Response& res) {
        auto occupied_id = uuids::uuid::from_string(req.path_params.at("id"));
        if (!occupied_id) { throw std::invalid_argument("invalid target uuid"); }
        nlohmann::json body = nlohmann::json::parse(req.body);
        auto occupy_id = uuids::uuid::from_string(body.at("occupy_id").get<std::string>());
        if (!occupy_id) { throw std::invalid_argument("invalid occupier uuid"); }
        OwnershipMode mode = parse_ownership_mode(body.at("mode"));
        std::string position = body.at("position").get<std::string>();
        if (mgr->judge_ability_exist(*occupied_id)) {
            auto it = mgr->occupy_ability(*occupy_id, position, *occupied_id, mode);
            if (!it) {
                throw ProblemDetail{
                    .status = 409,
                    .type = "occupy_ability_failed",
                    .title = "Occupy ability failed",
                    .detail = it.error(),
                    .instance = to_string(*occupied_id)
                };
            }
            res.set_content("OK", "text/plain");
            return;
        }
        else if (mgr->judge_device_exist(*occupied_id)) {
            auto it = mgr->occupy_device(*occupy_id, position, *occupied_id, mode);
            if (!it) {
                throw ProblemDetail{
                    .status = 409,
                    .type = "occupy_device_failed",
                    .title = "Occupy device failed",
                    .detail = it.error(),
                    .instance = to_string(*occupied_id)
                };
            }
            res.set_content("OK", "text/plain");
            return;
        }
        throw ProblemDetail{
            .status = 404,
            .type = "non_exist_occupyee_cr",
            .title = "Non-exist occupyee cr",
            .detail = "the cr to ocuupy does not exist",
            .instance = to_string(*occupied_id)
        };
    });

    server.Delete("/api/cr/:id/occupation", [mgr](const Request& req, Response& res) {
        auto occupied_id = uuids::uuid::from_string(req.path_params.at("id"));
        if (!occupied_id) { throw std::invalid_argument("invalid uuid"); }
        std::string body_str = req.body;
        nlohmann::json body = nlohmann::json::parse(body_str);
        auto occupy_id = uuids::uuid::from_string(body.at("occupy_id").get<std::string>());
        OwnershipMode mode = parse_ownership_mode(body.at("mode"));
        if (mgr->judge_ability_exist(*occupied_id)) {
            mgr->unoccupy_ability(*occupy_id, *occupied_id, mode);
            return;
        }
        else if (mgr->judge_device_exist(*occupied_id)) {
            mgr->unoccupy_device(*occupy_id, *occupied_id, mode);
            return;
        }
        throw ProblemDetail{
            .status = 404,
            .type = "non_exist_cr",
            .title = "cr does not exist",
            .detail = "",
            .instance = to_string(*occupied_id)
        };
    });
}

namespace {
using CrdFilter = std::function<bool(const AbilityCRD&)>;
template <typename T>
struct equal_to {
    T v;
    template <typename U>
    bool operator()(U&& x) {
        return x == v;
    };
};

/// @brief check constraints in the CRD; if param_field is non-empty, verify crd[crd_path] == param_field
/// else return nullptr
/// @warning currently only supportsCRDfirst-level inpath, (e.g. /package),higher-levelpathnot yet implemented
CrdFilter filter_crd_by_item(const std::string& param_field, auto crd_f) {
    if (param_field.empty()) { return nullptr; }

    return [crd_f, field = param_field](const AbilityCRD& crd) {
        return std::invoke(crd_f, crd) == field;
    };
}

std::vector<CrdFilter> make_crd_filters(const httplib::Request& req) {
    std::vector<CrdFilter> res;

    auto f_ability_name = filter_crd_by_item(
        req.get_param_value("abilityName"),
        [](const AbilityCRD& crd) -> decltype(auto) { return crd.metadata.name; }
    );
    if (f_ability_name) { res.push_back(std::move(f_ability_name)); }
    auto f_package_name
        = filter_crd_by_item(req.get_param_value("package"), &AbilityCRD::packageName);
    if (f_package_name) { res.push_back(std::move(f_package_name)); }
    return res;
}

bool test_crd_filters(const std::vector<CrdFilter>& filters, const AbilityCRD& crd) {
    return std::all_of(filters.begin(), filters.end(), [crd](const CrdFilter& f) {
        return f(crd);
    });
}

std::string_view check_package_media_type(std::string_view media_type) {
    using namespace std::string_view_literals;
    using namespace std::string_literals;
    if (media_type == "application/zip"sv) { return "zip"sv; }
    throw ProblemDetail{
        .status = 400,
        .type = "invalid_application_type",
        .title = "invalid application type",
        .detail
        = "currently only application/zip is supported, but found "s + std::string{media_type},
    };
};
void handle_upload_package_single_file(
    StoreManager& mgr, const httplib::Request& req, httplib::Response& res
) {
    bool force = req.get_param_value("force") == "true";
    auto media_type = req.get_header_value("content-type");
    auto file_type = check_package_media_type(media_type);
    auto add_res = mgr.add_package(file_type, req.body, force);
    if (!add_res) { throw std::runtime_error(add_res.error()); }
    nlohmann::json payload = add_res.value();
    res.set_content(payload.dump(2), "application/json");
}
void handle_upload_package_form_data(
    StoreManager& mgr, const httplib::Request& req, httplib::Response& res
) {
    bool force = req.get_param_value("force") == "true";
    // Access uploaded files
    if (!req.has_file("file")) { throw std::invalid_argument("need 'file' entry in form data"); }

    auto file_it = req.files.find("file");
    CHECK(file_it != req.files.end());
    const auto& file = file_it->second;
    LOG(INFO) << "Uploaded file: " << file.filename << " (" << file.content_type << ") - "
              << file.content.size() << " bytes" << std::endl;
    auto file_type = check_package_media_type(file.content_type);
    auto add_res = mgr.add_package(file_type, file.content, force);
    if (!add_res) { throw std::runtime_error(add_res.error()); }
    nlohmann::json payload = add_res.value();
    res.set_content(payload.dump(2), "application/json");
}
bool handle_find_crd_by_name(
    ResourceManager& mgr,
    const std::string& name,
    const std::string& version,
    httplib::Response& res
) {
    if (auto result = mgr.get_ability_crd(name, version); result) {
        res.set_content(nlohmann::json(*result).dump(), "application/json");
        return true;
    }
    if (auto result = mgr.get_device_crd(name, version); result) {
        res.set_content(nlohmann::json(*result).dump(), "application/json");
        return true;
    }
    return false;
}
} // namespace

void build_api_crd(std::shared_ptr<ResourceManager> mgr, httplib::Server& server) {
    using httplib::Request, httplib::Response;
    using nlohmann::json;

    server.Get("/api/crd", [mgr](const Request& req, Response& res) {
        if (has_kind_device(req.params)) {
            auto device_crds = mgr->get_all_device_crd();
            res.set_content(nlohmann::json(device_crds).dump(), "application/json");
            return;
        }
        std::vector<CrdFilter> crd_filters = make_crd_filters(req);
        std::vector<AbilityCRD> all_crds = mgr->get_all_ability_crd();
        if (crd_filters.empty()) {

            res.set_content(nlohmann::json(all_crds).dump(), "application/json");
            return;
        }
        std::vector<AbilityCRD> res_array;
        for (auto& ability_crd : all_crds) {
            if (test_crd_filters(crd_filters, ability_crd)) { res_array.push_back(ability_crd); }
        }
        res.set_content(nlohmann::json(res_array).dump(), "application/json");
    });

    server.Get("/api/crd/:name/:version", [mgr](const Request& req, Response& res) {
        LOG(INFO) << "Get " << req.path << ", id= " << req.path_params.at("id");
        std::string ability_or_device_name = req.path_params.at("name");
        std::string version = req.path_params.at("version");

        bool handled = handle_find_crd_by_name(*mgr, ability_or_device_name, version, res);
        if (handled) { return; }
        throw ProblemDetail{
            .status = 400,
            .type = "invalid_argument",
            .title = "invalid argument",
            .detail = strjoin(
                "name ", ability_or_device_name, " version ", version,
                " is neither a device name nor an abiility name"
            ),
            .instance = strjoin("name ", ability_or_device_name, " version ", version)
        };
    });

    // get the framework built-in CRD schema
    server.Get("/api/builtin-crd/ability", [](const Request& req, Response& res) {
        res.set_content(get_builtin_ability_crd_schema().dump(2), "application/json");
    });

    server.Get("/api/builtin-crd/service", [](const Request& req, Response& res) {
        res.set_content(get_builtin_service_crd_schema().dump(2), "application/json");
    });

    // get ability manifest
    server.Get("/api/manifest/:name/:version", [mgr](const Request& req, Response& res) {
        const auto& name = req.path_params.at("name");
        const auto& version = req.path_params.at("version");
        auto manifest = mgr->get_ability_manifest(name, version);
        if (!manifest) {
            res.status = 404;
            res.set_content(
                R"({"error": "manifest not found"})", "application/json"
            );
            return;
        }
        nlohmann::json j = *manifest;
        res.set_content(j.dump(2), "application/json");
    });

    // list all loaded ability manifests (across all packages)
    // unlike /api/crd: CRD is the single framework built-in resource type spec,
    // manifest is the ability description published by the ability developer with the package
    server.Get("/api/manifest", [mgr](const Request& req, Response& res) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& pkg : mgr->packages) {
            for (const auto& [name, manifest] : pkg.manifests) {
                nlohmann::json entry;
                entry["abilityName"] = manifest.abilityName;
                entry["kind"] = manifest.kind;
                entry["package"] = pkg.name;
                entry["version"] = to_string(pkg.version);
                // lightweight summary: task count / whether schema is provided
                entry["taskCount"] = manifest.tasks.is_array() ? manifest.tasks.size() : 0;
                entry["hasConfigSchema"] = !manifest.schema.openAPIV3Schema.is_null()
                                        && !manifest.schema.openAPIV3Schema.empty();
                arr.push_back(entry);
            }
        }
        res.set_content(arr.dump(2), "application/json");
    });

    // test downloading package
    server.Post("/api/internal/test-download-package", [mgr](const Request& req, Response& res) {
        auto spec = nlohmann::json::parse(req.body).get<PackageSpec>();
        LOG(INFO) << "begin downloading package " << spec.package << " version=" << spec.version;

        auto res_download = mgr->store_mgr.download_package(spec);
        if (res_download.empty()) { throw std::runtime_error("download package failed"); }

        LOG(INFO) << "packaged downloaded, begin extract";
        auto res_extract = mgr->store_mgr.extract_package(spec, res_download);

        if (!res_extract) { throw make_error("extract file failed: ", res_extract.error()); }
        LOG(INFO) << "extract package complete";

        mgr->update();
        res.set_content("OK", "text/plain");
    });

    server.Post("/api/resourcemgr/occupy_ability", [mgr](const Request& req, Response& res) {
        auto req_json = json::parse(req.body);
        auto it = mgr->occupy_ability(
            req_json["occupy_ability_id"], req_json["occupy_ability_position"],
            req_json["occupied_ability_id"], req_json["OwnershipMode"]
        );
        json payload;
        if (it) { payload["result"] = "success"; }
        else
            payload["result"] = "fail";
        res.set_content(payload.dump(2), "application/json");
        return;
    });

    server.Post("/api/resourcemgr/occupy_device", [mgr](const Request& req, Response& res) {
        auto req_json = json::parse(req.body);
        auto it = mgr->occupy_device(
            req_json["occupy_device_id"], req_json["occupy_device_position"],
            req_json["occupied_device_id"], req_json["OwnershipMode"]
        );
        json payload;
        if (it) { payload["result"] = "success"; }
        else
            payload["result"] = "fail";
        res.set_content(payload.dump(2), "application/json");
        return;
    });

    server.Post("/api/resourcemgr/unoccupy_ability", [mgr](const Request& req, Response& res) {
        auto req_json = json::parse(req.body);
        auto it = mgr->unoccupy_ability(
            req_json["occupy_ability_id"], req_json["occupied_ability_id"], req_json["mode"]
        );
        json payload;
        if (it) { payload["result"] = "success"; }
        else
            payload["result"] = "fail";
        res.set_content(payload.dump(2), "application/json");
        return;
    });

    server.Post("/api/resourcemgr/unoccupy_device", [mgr](const Request& req, Response& res) {
        auto req_json = json::parse(req.body);
        auto it = mgr->unoccupy_device(
            req_json["occupy_device_id"], req_json["occupied_device_id"], req_json["mode"]
        );
        json payload;
        if (it) { payload["result"] = "success"; }
        else
            payload["result"] = "fail";
        res.set_content(payload.dump(2), "application/json");
        return;
    });

    server.Post("/api/package", [mgr](const Request& req, Response& res) {
        using namespace std::string_view_literals;
        if (req.is_multipart_form_data()) {
            handle_upload_package_form_data(mgr->store_mgr, req, res);
        } else {
            handle_upload_package_single_file(mgr->store_mgr, req, res);
        }
        // trigger update() immediately after package install/upgrade so the mirrored CRs enter reconcile,
        // without waiting for the next periodic update_interval.
        try { mgr->update(); } catch (const std::exception& e) {
            LOG(WARNING) << "update() after package upload failed: " << e.what();
        }
    });

    server.Get("/api/package", [mgr](const Request& req, Response& res) {
        // todo
        res.set_content("[]", "application/json");
    });

    server.Delete("/api/package/:name/:version", [mgr](const Request& req, Response& res) {
        const auto& name = req.path_params.at("name");
        const auto& version = req.path_params.at("version");
        auto remove_res = mgr->store_mgr.remove_package({.package = name, .version = version});
        if (!remove_res) { throw std::runtime_error(remove_res.error()); }
        json payload = *remove_res;
        res.set_content(payload.dump(), "application/json");
        // reconcile immediately after uninstall; mirrored CRs are already unmirrored, clean up DB rows here
        try { mgr->update(); } catch (const std::exception& e) {
            LOG(WARNING) << "update() after package remove failed: " << e.what();
        }
    });

    // ================================================================
    // /api/skill: exposepackage-embedded agent skill documentation
    //
    // these skills are markdown (or other text formats) published by the package author in the zip,
    // mirrored to <home>/skills/_packages/<pkg>/<ver>/ by add_package / extract_package.
    //
    // GET /api/skill → list all skill metadata
    // GET /api/skill/:package/:version/*filename → read single skill file original
    //                                                       (text/markdown UTF-8)
    // ================================================================
    server.Get("/api/skill", [](const Request& req, Response& res) {
        json arr = json::array();
        for (const auto& s : StoreManager::list_all_skills()) {
            json e;
            e["package"] = s.package;
            e["version"] = s.version;
            e["filename"] = s.filename;
            e["size"] = s.size_bytes;
            e["title"] = s.title;
            arr.push_back(std::move(e));
        }
        res.set_content(arr.dump(2), "application/json");
    });

    // use wildcard to capture :filename, because skill filenames may contain subdirectories (e.g. "tasks/move.md")
    server.Get(R"(/api/skill/([^/]+)/([^/]+)/(.+))", [](const Request& req, Response& res) {
        std::string pkg = req.matches[1];
        std::string version = req.matches[2];
        std::string filename = req.matches[3];
        auto content = StoreManager::read_skill(pkg, version, filename);
        if (!content) {
            res.status = 404;
            res.set_content(
                json{{"error", "skill not found"},
                     {"package", pkg},
                     {"version", version},
                     {"filename", filename}}
                    .dump(),
                "application/json"
            );
            return;
        }
        // markdown by default; other extensions also return as text/markdown; the MCP server then checks
        res.set_content(*content, "text/markdown; charset=utf-8");
    });
}

// ====================================================================
// ability instance API (/api/instance)
// CR is the template/Prototype; an instance is a runtime object derived from the template, with an independent instance_id, destroyed on exit.
// ====================================================================
namespace {
nlohmann::json instance_info_to_json(const ResourceManager::AbilityInstanceInfo& info) {
    nlohmann::json j;
    j["instance_id"] = to_string(info.instance_id);
    j["cr_id"] = info.cr_id ? to_string(*info.cr_id) : "";
    j["cr_name"] = info.cr_name;
    j["instance_name"] = info.instance_name;
    j["ability_name"] = info.ability_name;
    j["ability_version"] = info.ability_version;
    j["state"] = info.state;
    j["start_time"] = info.start_time;
    j["stop_time"] = info.stop_time;
    j["detail"] = info.detail;
    // spec snapshot at startup, so the SDK can get its own config directly
    j["spec_snapshot"] = info.spec_snapshot;
    return j;
}

// by template name or id checktemplate; return nullopt if not found nullopt
std::optional<AbilityCR> find_template(
    ResourceManager& mgr, const std::string& template_ref
) {
    if (auto uid = uuids::uuid::from_string(template_ref); uid) {
        if (auto cr = mgr.get_ability_cr(*uid); cr) { return cr; }
    }
    // find by name
    for (auto& cr : mgr.get_all_ability_cr()) {
        if (cr.metadata.name == template_ref) { return cr; }
    }
    return std::nullopt;
}
} // namespace

void build_api_instance(std::shared_ptr<ResourceManager> mgr, httplib::Server& server) {
    using httplib::Request, httplib::Response;
    using nlohmann::json;

    // list all instances
    server.Get("/api/instance", [mgr](const Request& req, Response& res) {
        json result = json::array();
        for (auto& info : mgr->get_all_ability_instances()) {
            result.push_back(instance_info_to_json(info));
        }
        res.set_content(result.dump(2), "application/json");
    });

    // query a specific instance
    server.Get("/api/instance/:id", [mgr](const Request& req, Response& res) {
        auto id_str = req.path_params.at("id");
        auto uid = uuids::uuid::from_string(id_str);
        if (!uid) {
            res.status = 400;
            res.set_content(R"({"error": "invalid uuid"})", "application/json");
            return;
        }
        auto info = mgr->get_ability_instance(*uid);
        if (!info) {
            res.status = 404;
            res.set_content(R"({"error": "instance not found"})", "application/json");
            return;
        }
        res.set_content(instance_info_to_json(*info).dump(2), "application/json");
    });

    // derive and start a new instance from a template
    // body like:
    // { "template": "<name or cr id>", "start": true, "connect": true }
    server.Post("/api/instance", [mgr](const Request& req, Response& res) {
        auto body = parse_json_from_req(req);
        if (!body.contains("template") || !body.at("template").is_string()) {
            throw std::invalid_argument("body must contain string 'template' (CR name or id)");
        }
        std::string template_ref = body.at("template").get<std::string>();
        auto template_opt = find_template(*mgr, template_ref);
        if (!template_opt) {
            res.status = 404;
            res.set_content(
                json{{"error", "template not found"}, {"template", template_ref}}.dump(),
                "application/json"
            );
            return;
        }
        auto& tmpl = *template_opt;

        // singleton constraint
        if (tmpl.spec && tmpl.spec->singleton.value_or(true)) {
            auto active = mgr->get_active_instances_by_ability_name(tmpl.spec->abilityName);
            if (!active.empty()) {
                json err;
                err["error"] = "singleton constraint";
                err["detail"] = "ability " + tmpl.spec->abilityName
                              + " is singleton and already has a running instance";
                err["running_instance_id"] = to_string(active[0].instance_id);
                res.status = 409;
                res.set_content(err.dump(2), "application/json");
                return;
            }
        }

        bool start = body.value("start", true);
        bool connect = start && body.value("connect", true);

        auto manifest = mgr->get_ability_manifest(
            tmpl.spec->abilityName, to_string(tmpl.spec->version)
        );
        auto total_task = manifest.has_value()
                              ? make_task_start_ability(mgr, tmpl, start, connect)
                              : make_task_download_pkg_and_start_ability(mgr, tmpl, start, connect);
        auto task_id = submit_task(total_task, mgr->module_name());
        if (!task_id) {
            throw std::runtime_error("submit start-instance task failed: " + task_id.error());
        }
        json payload;
        payload["taskId"] = *task_id;
        payload["template"] = tmpl.metadata.name;
        res.set_content(payload.dump(2), "application/json");
    });

    // terminate and destroy instance
    server.Delete("/api/instance/:id", [mgr](const Request& req, Response& res) {
        auto id_str = req.path_params.at("id");
        auto uid = uuids::uuid::from_string(id_str);
        if (!uid) {
            res.status = 400;
            res.set_content(R"({"error": "invalid uuid"})", "application/json");
            return;
        }
        auto info = mgr->get_ability_instance(*uid);
        if (!info) {
            res.status = 404;
            res.set_content(R"({"error": "instance not found"})", "application/json");
            return;
        }
        // if already running, send terminate; heartbeat/process-exit callback will then trigger row deletion
        bool active = info->state != "Inactive" && info->state != "Terminated";
        if (active) {
            nlohmann::json payload;
            payload["abilityInstanceId"] = *uid;
            payload["command"] = "terminate";
            send_sync(make_message(
                "ResourceMgr", "LifecycleMgr", "lifecycle_request", payload
            ));
        }
        // fallback: directly clear the row (even if lifecycle callback is delayed)
        mgr->delete_ability_instance(*uid);
        res.set_content(R"({"status": "ok"})", "application/json");
    });
}

// Phase 2: Service CR HTTP API
void build_api_service(std::shared_ptr<ResourceManager> mgr, httplib::Server& server) {
    using httplib::Request, httplib::Response;
    using nlohmann::json;

    server.Get("/api/service-cr", [mgr](const Request& req, Response& res) {
        auto services = mgr->get_all_service_cr();
        res.set_content(json(services).dump(2), "application/json");
    });

    server.Get("/api/service-cr/:id", [mgr](const Request& req, Response& res) {
        auto id_str = req.path_params.at("id");
        auto id = uuids::uuid::from_string(id_str);
        if (!id) {
            res.status = 400;
            res.set_content(R"({"error": "invalid uuid"})", "application/json");
            return;
        }
        auto cr = mgr->get_service_cr(*id);
        if (!cr) {
            res.status = 404;
            res.set_content(R"({"error": "service not found"})", "application/json");
            return;
        }
        json j = *cr;
        res.set_content(j.dump(2), "application/json");
    });

    server.Post("/api/service-cr", [mgr](const Request& req, Response& res) {
        json body = json::parse(req.body);

        // framework-level validation
        auto fw_res = validate_cr_framework_level(body);
        // Service CR uses service CRD schema; skip here (service has its own CRD)
        // TODO: validate using get_builtin_service_crd_schema()

        ServiceCR cr = body.get<ServiceCR>();
        if (cr.id == uuids::uuid{}) {
            // generate UUID
            cr.id = make_uuid(
                cr.spec->package, cr.spec->version.to_string(),
                cr.spec->serviceName, cr.metadata.name
            );
        }
        auto id_str = mgr->add_service_cr(cr);
        json result;
        result["id"] = id_str;
        result["status"] = "created";
        res.set_content(result.dump(2), "application/json");
    });

    server.Delete("/api/service-cr/:id", [mgr](const Request& req, Response& res) {
        auto id_str = req.path_params.at("id");
        auto id = uuids::uuid::from_string(id_str);
        if (!id) {
            res.status = 400;
            res.set_content(R"({"error": "invalid uuid"})", "application/json");
            return;
        }
        mgr->remove_service_cr(*id);
        json result;
        result["status"] = "deleted";
        res.set_content(result.dump(2), "application/json");
    });
}
