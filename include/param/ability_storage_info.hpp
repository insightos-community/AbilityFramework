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

#include <nlohmann/json.hpp>
#include <string>

namespace msg_params {
struct AbilityStorageInfo {
    // 能力包所在的路径
    std::string package_path;
    // 能力可执行文件所在的路径
    std::string executable_path;
    // 控制器所在的路径,如果这个能力还没有控制器,那么此项留空
    std::string controller_path;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(
        AbilityStorageInfo, package_path, executable_path, controller_path
    )
};

} // namespace msg_params
