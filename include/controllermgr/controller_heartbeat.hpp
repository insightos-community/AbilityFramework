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
#include <string>
#include <nlohmann/json.hpp>
#include "util/uuid_json_convert.hpp"


//控制器心跳
struct ControllerHeartbeat{
    int port; // 控制器基础服务器对应的端口
    std::string controllerInstanceId;//控制器的实例id
    std::string package;//控制器所对应能力的包名称
    std::string version;//控制器所对应能力的版本名
    std::string abilityName;//控制器所对应能力的类型名
    std::string protocol;//控制器所使用的协议

    NLOHMANN_DEFINE_TYPE_INTRUSIVE( //序列化
        ControllerHeartbeat, port, controllerInstanceId, package, version, abilityName, protocol
    )

};