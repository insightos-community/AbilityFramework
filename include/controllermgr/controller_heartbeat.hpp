// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <string>
#include <nlohmann/json.hpp>
#include "util/uuid_json_convert.hpp"


//controller heartbeat
struct ControllerHeartbeat{
    int port; // port of the controller base server
    std::string controllerInstanceId;//controller instance id
    std::string package;//package name of the ability the controller corresponds to
    std::string version;//version name of the ability the controller corresponds to
    std::string abilityName;//type name of the ability the controller corresponds to
    std::string protocol;//protocol used by the controller

    NLOHMANN_DEFINE_TYPE_INTRUSIVE( //serialize
        ControllerHeartbeat, port, controllerInstanceId, package, version, abilityName, protocol
    )

};