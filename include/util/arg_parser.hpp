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
