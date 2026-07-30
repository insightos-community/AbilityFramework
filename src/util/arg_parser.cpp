// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#include "util/arg_parser.hpp"
#include "version.hpp"
#include <iostream>

static const std::string basic_config_text = R"(framework_name: robot_node_1 # framework instance name
http_ip: 0.0.0.0 # frameworkHTTPserviceIP
http_port: 8080 # frameworkHTTPservice port

log: # log config
  glog: # glogconfig
    color_log: true # colored log
    also_log_to_stderr: true # also output to stderr and the log file
    max_log_size: 1024 # max log file size, unit MB
    stop_logging_if_full_disk: true # stop writing when disk is full
    log_dir: log

controller_mgr: # controller module config
  fetch_interval: 10 # periodictask fetch_and_check_abilities

resource_mgr:
  update_interval: 10 # periodictask update

lifecycle_mgr: # lifecycle module config
  clear_stale_heartbeats_interval: 10 # periodictask clear_stale_heartbeats

discovery_mgr: # discovery module config
  methods: # network discovery method
    ipv4: true
    ipv6: false
  expiry: 20 # discovery timeout, in seconds

webui: # webui console config
  enabled: true # whether enabled WebUI(access /ui)
)";

static const std::string full_config_text = R"(framework_name: robot_node_1 # framework instance name
http_ip: 0.0.0.0 # frameworkHTTPserviceIP
http_port: 8080 # frameworkHTTPservice port

log: # log config
  glog: # glogconfig
    color_log: true # colored log
    also_log_to_stderr: true # also output to stderr and the log file
    max_log_size: 1024 # max log file size, unit MB
    stop_logging_if_full_disk: true # stop writing when disk is full
    log_dir: log

controller_mgr: # controller module config
  fetch_interval: 10 # periodictask fetch_and_check_abilities

resource_mgr:
  update_interval: 10 # periodictask update

lifecycle_mgr: # lifecycle module config
  clear_stale_heartbeats_interval: 10 # periodictask clear_stale_heartbeats

discovery_mgr: # discovery module config
  methods: # network discovery method
    ipv4: true
    ipv6: false
  expiry: 20 # discovery timeout, in seconds
  teams: # teamconfig, notconfigdoes not participate in networking
    - teamName: team_a # team name(cannot contain spaces)
      master: true # whether is the master node
      teamID: 3DF3A930-B100-5F8E-BE60-5884D55E5B70
      secret: a8f5f167f44f4964e6c998dee827110c
  election: # election config; election disabled if not configured
    method: bully
    params:
      weight: 20

source_urls: [] # ability package download url list

opentelemetry: # OpenTelemetry tracing(optional)
  url: # remote OTel Collector address

webui: # webui console config
  enabled: true # whether enabled WebUI(access /ui)
  custom_path: # custom frontend static file path (optional); when set, external files replace the embedded WebUI
)";

static const std::string version_info_ = std::string("AbilityFramework ") + VERSION_STRING
                                       + " (git commit " + GIT_COMMIT_HASH + ", built on "
                                       + BUILD_DATE + ")";

ProgramOptions parse_arguments(int argc, const char* argv[]) {
    ProgramOptions options;
    options.basic_config = basic_config_text;
    options.full_config_text = full_config_text;

    try {
        cxxopts::Options cxx_options("AbilityFramework", "ability framework: supports config loading and default config export");

        // clang-format off
        cxx_options.add_options()
            ("h,help", "show help")
            ("v,version", "show version")
            ("c,config", "specifiedconfig file(arg: config file name or path)", cxxopts::value<std::string>())
            ("d,dump", "output default config to terminal")
            ("o,output", "output default config to file(arg: config file name)", cxxopts::value<std::string>())
            ("f,full", "used with -d/-o, outputs full config including all optional fields")
            ("V,verbose", "print verbose logs", cxxopts::value<int>()->default_value("0"))
            ("log-to-stdout", "also log output to standard output");
        // clang-format on

        auto result = cxx_options.parse(argc, argv);

        if (result.count("help")) {
            options.show_help = true;
            options.help_info = cxx_options.help();
            return options;
        }
        if (result.count("version")) {
            options.show_version = true;
            options.version_info = version_info_;
        }
        if (result.count("config")) { options.config_file = result["config"].as<std::string>(); }
        if (result.count("dump")) { options.dump_default = true; }
        if (result.count("output")) { options.output_file = result["output"].as<std::string>(); }
        if (result.count("full")) { options.full_config = true; }
        if (result.count("log-to-stdout")) { options.log_to_stdout = true; }
        if (result.count("verbose")) { options.verbose_log = result["verbose"].as<int>(); }
    }
    catch (const std::exception& e) {
        throw std::runtime_error(std::string("parameterparse error: ") + e.what());
    }
    return options;
}
