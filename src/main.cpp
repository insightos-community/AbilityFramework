// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#include "abilityalertmgr/abilityalert_mgr.hpp"
#include "webui_embedded.hpp"
#include "databasemgr/database_mgr.hpp"
#include "messagebus/add_test_api_for_http.hpp"
#include "prelude.hpp"
#include <csignal>
#include <deque>
#include <filesystem>
#include <fstream>
#include <glog/logging.h>
#include <iostream>
#include <uvw.hpp>

#include "controllermgr/controller_mgr.hpp"
#include "discoverymgr/discovery_mgr.hpp"
#include "lifecyclemgr/lifecycle_mgr.hpp"
#include "resourcemgr/resource_mgr.hpp"
#include "subprocessmgr/subprocess_mgr.hpp"
#include "taskmgr/task_mgr.hpp"

#include "messagebus/messagebus.hpp"
// #include "taskmgr/task_mgr_messagebus.hpp"

#include "taskmgr/task_status_mgr.hpp"
#include "util/arg_parser.hpp"
#include "util/cpptrace_debug.hpp"
#include "util/global_vars.hpp"
#include "util/handle_error_to_http.hpp"
#include "util/jthread.hpp"

using namespace std::chrono_literals;
namespace {
void configure_glog(const std::string& log_dir, bool log_to_stdout) {
    FLAGS_colorlogtostderr = global_vars::get_config<bool>("/log/glog/color_log", true);
    FLAGS_alsologtostderr = log_to_stdout
        || global_vars::get_config<bool>("/log/glog/also_log_to_stderr", false);
    FLAGS_log_dir = log_dir;
    FLAGS_max_log_size
        = global_vars::get_config<int>("/log/glog/max_log_size", 1024);
    FLAGS_stop_logging_if_full_disk = global_vars::get_config<bool>(
        "/log/glog/stop_logging_if_full_disk", true
    );
    google::InitGoogleLogging("AbilityFramework-cpp");
    google::InstallFailureSignalHandler();
    // glog's failure signal set includes SIGTERM by default, which makes
    // every clean shutdown look like a crash (it prints "Aborted at ..."
    // plus a fake stack trace before the process actually exits). SIGTERM
    // is a normal termination request from systemd / launcher / parent
    // process, not a fault — restore it to the default disposition so
    // glog only intercepts the actually-fatal signals (SIGSEGV/ABRT/etc).
    std::signal(SIGTERM, SIG_DFL);
}

void init_workspace_paths(const std::filesystem::path& path) {
    std::error_code ec;
    create_directories(path, ec);
    if (ec) {
        LOG(ERROR) << "failed making workspace directory on " << path << " : " << ec.message();
        exit(-1);
    }
    create_directories(path / "log", ec);
    if (ec) {
        LOG(ERROR) << "failed making log directory: " << ec.message();
        exit(-1);
    }
    create_directories(path / "packages", ec);
    if (ec) {
        LOG(ERROR) << "failed making packages directory: " << ec.message();
        exit(-1);
    }
    create_directories(path / "crs", ec);
    if (ec) {
        LOG(ERROR) << "failed making crs directory: " << ec.message();
        exit(-1);
    }
    create_directories(path / "databases", ec);
    if (ec) {
        LOG(ERROR) << "failed making crs directory: " << ec.message();
        exit(-1);
    }
}

auto make_timer(uvw::loop& loop, auto interval, auto&& f) {
    auto timer = loop.resource<uvw::timer_handle>();
    timer->on<uvw::timer_event>([cb = f](const uvw::timer_event& ev, uvw::timer_handle& self) {
        CPPTRACE_TRY {
            cb();
        }
        CPPTRACE_CATCH(std::exception & e) {
            LOG(ERROR) << "timer callback failed: " << e.what();
            FWK_DUMP_STACKTRACE_TO_LOG(ERROR);
        }
    });
    timer->start(50ms, interval);
    return timer;
}

void build_api_hello(httplib::Server& server) {
    server.Get("/api/hello", [](const auto& req, httplib::Response& res) {
        res.set_content("this is ability framework", "text/plain");
    });
}

} // namespace

namespace message_bus {
namespace {
template <typename... Ts>
void add_modules(const Ts&... args) {
    (message_bus::add_module(args), ...);
}

} // namespace
} // namespace message_bus

void build_api_config(httplib::Server& server) {
    using namespace httplib;
    server.Get("/api/config", [](const Request& req, Response& res) {
        std::filesystem::path config_path = global_vars::config_path();
        if (!exists(config_path)) {
            res.status = 404;
            res.set_content("config file not exist: " + config_path.string(), "text/plain");
            return;
        }
        try {
            YAML::Node config = YAML::LoadFile(config_path.string());
            nlohmann::json json_config = yaml_to_json(config);
            res.set_content(json_config.dump(2), "application/json");
            return;
        }
        catch (const std::exception& e) {
            res.status = 500;
            res.set_content(std::string("failed to parse config: ") + e.what(), "text/plain");
            return;
        }
    });
}

void build_api_log(httplib::Server& server) {
    using namespace httplib;
    // read latest log (tail N lines)
    server.Get("/api/log", [](const Request& req, Response& res) {
        int lines = 100;
        auto lines_param = req.get_param_value("lines");
        if (!lines_param.empty()) {
            try { lines = std::stoi(lines_param); } catch (...) {}
            lines = std::clamp(lines, 1, 5000);
        }
        std::string level = req.get_param_value("level"); // INFO, WARNING, ERROR
        if (level.empty()) { level = "INFO"; }

        auto log_dir = global_vars::log_path();
        std::string log_file;
        // glog filename format: AbilityFramework-cpp.{hostname}.{user}.log.{LEVEL}.{date}-{pid}
        // symlink: AbilityFramework-cpp.{LEVEL}
        for (const auto& name : {"AbilityFramework-cpp.INFO",
                                  "AbilityFramework-cpp.WARNING",
                                  "AbilityFramework-cpp.ERROR"}) {
            if (level == "INFO" || (level == "WARNING" && std::string(name).find("INFO") == std::string::npos)
                || (level == "ERROR" && std::string(name).find("ERROR") != std::string::npos)) {
                auto p = std::filesystem::path(log_dir) / name;
                if (std::filesystem::exists(p)) { log_file = p.string(); break; }
            }
        }
        // simplified: defaultread INFO level(containalllog)
        if (log_file.empty()) {
            auto p = std::filesystem::path(log_dir) / "AbilityFramework-cpp.INFO";
            if (std::filesystem::exists(p)) { log_file = p.string(); }
        }

        if (log_file.empty()) {
            res.set_content("[]", "application/json");
            return;
        }

        // read file tail
        std::ifstream ifs(log_file);
        if (!ifs) {
            res.set_content("[]", "application/json");
            return;
        }
        std::deque<std::string> tail;
        std::string line;
        while (std::getline(ifs, line)) {
            tail.push_back(std::move(line));
            if (static_cast<int>(tail.size()) > lines) { tail.pop_front(); }
        }

        nlohmann::json result = nlohmann::json::array();
        for (auto& l : tail) { result.push_back(std::move(l)); }
        res.set_content(result.dump(), "application/json");
    });
}

using std::make_shared;
int main(int argc, const char* argv[]) {
    ProgramOptions options;
    try {
        options = parse_arguments(argc, argv);
    }
    catch (const std::exception& e) {
        LOG(ERROR) << e.what();
        return 1;
    }
    // -h
    if (options.show_help) {
        std::cout << options.help_info << std::endl;
        return 0;
    }
    // -v
    if (options.show_version) {
        std::cout << options.version_info << std::endl;
        return 0;
    }
    // -d
    if (options.dump_default) {
        const auto& config_text = options.full_config ? options.full_config_text : options.basic_config;
        YAML::Node d_config = YAML::Load(config_text);
        nlohmann::json json_d_config = yaml_to_json(d_config);
        std::cout << json_d_config.dump(2) << std::endl;
        return 0;
    }
    // -o
    if (!options.output_file.empty()) {
        std::ofstream ofs(options.output_file);
        if (!ofs) {
            std::cerr << "failed to write config file: " << options.output_file << std::endl;
            return 1;
        }
        const auto& config_text = options.full_config ? options.full_config_text : options.basic_config;
        ofs << config_text;
        std::cout << "default config written to: " << options.output_file << std::endl;
        return 0;
    }
    // --verbose
    if (options.verbose_log > 0) { google::SetVLOGLevel("*", options.verbose_log); }
    // -c
    if (!options.config_file.empty()) {
        global_vars::init(options.config_file);
    }
    else { global_vars::init(); }

    // create log dir first, then init glog, to avoid "Logging before InitGoogleLogging" to stderr
    std::filesystem::create_directories(global_vars::home_path() / "log");
    configure_glog(global_vars::log_path(), options.log_to_stdout);
    init_workspace_paths(global_vars::home_path());
    database_mgr::init_database(global_vars::home_path() / "databases" / "ability_framework.db");
    // read environment variable, then read config from that directory
    // ...

    // initialize libuv event loop
    auto loop = uvw::loop::get_default();
    CHECK_NOTNULL(loop);

    // initialize all modules
    auto resource_mgr = make_shared<ResourceManager>();
    auto subproccess_mgr = make_shared<SubprocessManager>(loop);
    auto lifecycle_mgr = make_shared<LifecycleManager>();
    auto task_mgr = make_shared<TaskManager>(loop);
    auto task_status_mgr = make_shared<TaskStatusManager>();
    auto controller_mgr = make_shared<ControllerManager>();
    auto discovery_mgr = make_shared<DiscoveryManager>(loop);
    auto abilityalert_mgr = make_shared<AbilityAlertManager>();

    // register all modules to the message bus
    // note, taskmgrmustas theaparameter
    message_bus::add_modules(
        task_mgr, lifecycle_mgr, resource_mgr, subproccess_mgr, task_status_mgr, controller_mgr
    );

    // initialize http server
    httplib::Server http_server;
    bool webui_enabled = global_vars::get_config<bool>("/webui/enabled", true);
    if (webui_enabled) {
        std::string custom_path = global_vars::get_config<std::string>("/webui/custom_path", "");
        if (!custom_path.empty() && std::filesystem::exists(custom_path)) {
            http_server.set_mount_point("/ui", custom_path);
            LOG(INFO) << "webui: using custom path " << custom_path;
        }
        else {
            webui_embedded::mount(http_server);
            LOG(INFO) << "webui: using embedded";
        }
    }
    else {
        LOG(INFO) << "webui: disabled";
    }
    http_server.set_exception_handler(handle_error_to_http);
    http_server.set_payload_max_length(100 * 1024 * 1024); // limit100M
    http_server.set_logger([](const httplib::Request& req, const httplib::Response& res) {
        LOG(INFO)<< "From: "<<  req.remote_addr <<" Send: " <<req.method << " " << req.path << " -> " << res.status << " -> \n-> "  << req.body;
    });
    build_api_hello(http_server);
    build_api(lifecycle_mgr, http_server);
    build_api(controller_mgr, http_server);
    build_api(task_mgr, http_server);
    build_api(task_status_mgr, http_server);
    build_api(resource_mgr, http_server);
    build_api(discovery_mgr, http_server);
    build_api(abilityalert_mgr, http_server);
    build_api_config(http_server);
    build_api_log(http_server);
    message_bus::add_test_api(http_server);

    int update_resource_mgr_interval
        = global_vars::get_config<int>("/resource_mgr/update_interval", 10);
    int clear_stale_heartbeats_interval
        = global_vars::get_config<int>("/ligecy_mgr/clear_stale_heartbeats_interval", 10);
    auto t_update_resource_mgr
        = make_timer(*loop, std::chrono::seconds(update_resource_mgr_interval), [resource_mgr]() {
              resource_mgr->update();
          });
    auto t_clear_stale_heartbeats = make_timer(
        *loop, std::chrono::seconds(clear_stale_heartbeats_interval),
        [lifecycle_mgr]() { lifecycle_mgr->clear_stale_heartbeats(); }
    );
    // start http server
    jthread th_http_server([&http_server, webui_enabled]() {
        const std::string HTTP_IP = global_vars::get_config<std::string>("/http_ip", "0.0.0.0");
        const int HTTP_PORT = global_vars::get_config<int>("/http_port", 8080);
        bool success = http_server.bind_to_port(HTTP_IP, HTTP_PORT);
        CHECK(success) << "http server bind ip " << HTTP_IP << " port " << HTTP_PORT << " failed";

        std::cout << "\n"
                  << "  AbilityFramework is running\n"
                  << "\n"
                  << "  API:   http://" << (HTTP_IP == "0.0.0.0" ? "localhost" : HTTP_IP) << ":" << HTTP_PORT << "/api/hello\n";
        if (webui_enabled) {
            std::cout << "  WebUI: http://" << (HTTP_IP == "0.0.0.0" ? "localhost" : HTTP_IP) << ":" << HTTP_PORT << "/ui\n";
        }
        std::cout << "  Log:   " << global_vars::log_path() << "\n"
                  << std::endl;

        http_server.listen_after_bind();
        LOG(INFO) << "http_server stopped";
    });

    // set signal receiver
    auto signal_handler = loop->resource<uvw::signal_handle>();
    signal_handler->on<uvw::signal_event>(
        [&, loop](const uvw::signal_event& ev, uvw::signal_handle& self) {
            LOG(ERROR) << "SIGINT received, shutting down";
            t_update_resource_mgr->stop();
            t_clear_stale_heartbeats->stop();
            http_server.stop();
            message_bus::stop_all_modules();
            // should start teardown of all modules here
            // after teardown, the uv loop can stop
            loop->stop();
            database_mgr::close_database();
        }
    );
    signal_handler->start(SIGINT);

    // run event loop
    loop->run();
    LOG(WARNING) << "ability framework main finish";
}
