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

#include "doctest.h"
#include "resourcemgr/device_cr.hpp"
#include "util/yaml_to_json.hpp"

namespace {
constexpr char DEVICE_CR_YAML_1[] = R"yaml(
kind: Device # 资源类型, 固定为Device
metadata:
  name: raspberrypi-camera # 设备实例名 (必须)

spec:
  package: std.insightos.com # 包名(必须)
  version: 0.1.0 # 包版本(必须)
  deviceName: Camera # 设备类型名(必须)
  access:
    type: v4l2
    params:
      path: /dev/video0
)yaml";
}

TEST_CASE("read device cr") {
    auto y = YAML::Load(DEVICE_CR_YAML_1);
    auto j = yaml_to_json(y);
    DeviceCR cr = j.get<DeviceCR>();
    nlohmann::json j1 = cr;
    CHECK(cr.spec.access.type == "v4l2");
    CHECK(cr.metadata.name == "raspberrypi-camera");
    CHECK(cr.spec.deviceName == "Camera");
    CHECK(cr.spec.version == semver::version(0, 1, 0));
}
namespace {
constexpr char DEVICE_CR_YAML_2[] = R"yaml(
kind: Device
metadata:
  name: ros-camera

spec:
  package: std.insightos.com
  version: 0.1.0
  deviceName: Camera
  access:
    type: ros
    params:
      topic:
        color: '/camera/color/image_raw'
      messageType: 'sensor_msgs/Image'
)yaml";
}

TEST_CASE("read device cr 2") {
    auto y = YAML::Load(DEVICE_CR_YAML_2);
    auto j = yaml_to_json(y);
    DeviceCR cr = j.get<DeviceCR>();
    nlohmann::json j1 = cr;
    CHECK(cr.spec.access.type == "ros");
    CHECK(cr.metadata.name == "ros-camera");
    CHECK(cr.spec.deviceName == "Camera");
    CHECK(cr.spec.version == semver::version(0, 1, 0));
}
