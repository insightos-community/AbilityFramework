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

#include "util/arg_parser.hpp"
#include "version.hpp"
#include <iostream>

static const std::string basic_config_text = R"(framework_name: robot_node_1   # 框架实例名称
http_ip: 0.0.0.0              # 框架HTTP服务IP
http_port: 8080               # 框架HTTP服务端口

log:                            # 日志配置
  glog:                         # glog配置
    color_log: true             # 彩色日志
    also_log_to_stderr: true    # 同时输出到stderr和日志文件
    max_log_size: 1024          # 日志文件最大大小，单位 MB
    stop_logging_if_full_disk: true # 磁盘满时停止写入
    log_dir: log

controller_mgr:               # controller模块配置
  fetch_interval: 10          # 定时任务 fetch_and_check_abilities

resource_mgr:
  update_interval: 10         # 定时任务 update

lifecycle_mgr:                # lifecycle模块配置
  clear_stale_heartbeats_interval: 10  # 定时任务 clear_stale_heartbeats

discovery_mgr:                # discovery模块配置
  methods:                    # 网络发现方式
    ipv4: true
    ipv6: false
  expiry: 20                  # 发现超时时间，单位秒

webui:                        # 管理控制台配置
  enabled: true               # 是否开启 WebUI（访问 /ui）
)";

static const std::string full_config_text = R"(framework_name: robot_node_1   # 框架实例名称
http_ip: 0.0.0.0              # 框架HTTP服务IP
http_port: 8080               # 框架HTTP服务端口

log:                            # 日志配置
  glog:                         # glog配置
    color_log: true             # 彩色日志
    also_log_to_stderr: true    # 同时输出到stderr和日志文件
    max_log_size: 1024          # 日志文件最大大小，单位 MB
    stop_logging_if_full_disk: true # 磁盘满时停止写入
    log_dir: log

controller_mgr:               # controller模块配置
  fetch_interval: 10          # 定时任务 fetch_and_check_abilities

resource_mgr:
  update_interval: 10         # 定时任务 update

lifecycle_mgr:                # lifecycle模块配置
  clear_stale_heartbeats_interval: 10  # 定时任务 clear_stale_heartbeats

discovery_mgr:                # discovery模块配置
  methods:                    # 网络发现方式
    ipv4: true
    ipv6: false
  expiry: 20                  # 发现超时时间，单位秒
  teams:                      # 队伍配置，未配置时不参与组网
    - teamName: team_a        # 队伍名称（不可含空格）
      master: true            # 是否为主节点
      teamID: 3DF3A930-B100-5F8E-BE60-5884D55E5B70
      secret: development-only-change-before-use
  election:                   # 选举配置，未配置时不启用选举
    method: bully
    params:
      weight: 20

source_urls: []               # 能力包下载地址列表

opentelemetry:                # OpenTelemetry 链路追踪（可选）
  url:                        # 远端 OTel Collector 地址

webui:                        # 管理控制台配置
  enabled: true               # 是否开启 WebUI（访问 /ui）
  custom_path:                # 自定义前端静态文件路径（可选），配置后使用外部文件替代内嵌 WebUI
)";

static const std::string version_info_ = std::string("AbilityFramework ") + VERSION_STRING
                                       + " (git commit " + GIT_COMMIT_HASH + ", built on "
                                       + BUILD_DATE + ")";

ProgramOptions parse_arguments(int argc, const char* argv[]) {
    ProgramOptions options;
    options.basic_config = basic_config_text;
    options.full_config_text = full_config_text;

    try {
        cxxopts::Options cxx_options("AbilityFramework", "能力框架：支持配置加载与默认配置导出");

        // clang-format off
        cxx_options.add_options()
            ("h,help", "显示帮助信息")
            ("v,version", "显示版本信息")
            ("c,config", "指定配置文件(arg : 配置文件名或路径)", cxxopts::value<std::string>())
            ("d,dump", "输出默认配置到终端")
            ("o,output", "输出默认配置到文件(arg : 配置文件名)", cxxopts::value<std::string>())
            ("f,full", "与 -d/-o 配合使用，输出包含所有可选项的完整配置")
            ("V,verbose", "打印繁复日志", cxxopts::value<int>()->default_value("0"))
            ("log-to-stdout", "将日志同时输出到标准输出");
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
        throw std::runtime_error(std::string("参数解析错误: ") + e.what());
    }
    return options;
}
