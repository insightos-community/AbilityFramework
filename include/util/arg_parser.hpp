// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cxxopts.hpp>
#include <string>

struct ProgramOptions {
    bool show_help = false;
    bool dump_default = false;
    bool show_version = false;
    bool full_config = false;
    bool log_to_stdout = false;

    std::string help_info;
    std::string basic_config;
    std::string full_config_text;
    std::string version_info;
    std::string config_file;
    std::string output_file;
    int verbose_log = 0;
};

ProgramOptions parse_arguments(int argc, const char* argv[]);
