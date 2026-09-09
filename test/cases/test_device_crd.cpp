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
#include "resourcemgr/device_crd.hpp"
#include "util/yaml_to_json.hpp"
#include <iostream>

namespace {
constexpr char DEVICE_CRD_YAML_1[] = R"yaml(
packageName: std.insightos.com # 包名称(必须)
version: 0.1.0 # 版本名称(必须)
kind: CustomResourceDefinition # 资源类型, 固定为CustomResourceDefinition
metadata:
  name: Camera # 设备名称, 此处Camera 表示一种通用相机
  labels: {}
  annotations:
    description: "standard camera device"

spec:
  access: # 列举该种设备可能支持的访问方式
    - type: v4l2
      params:
        path: 
          type: string
          description: 设备文件所在地址
    - type: ros
      params:
        topic:
          type: object
          description: 相机图片话题所在topic
          properties:
            color:
              type: string
              description: 彩色画面所在话题
            depth:
              type: string
              description: 深度画面所在话题
              default: null # 该项可选
        messageType:
          type: string
          description: 表示ros话题中的消息类型
  schema:
    openAPIV3Schema:
      true
)yaml";
}

TEST_CASE("read device crd") {
    auto y = YAML::Load(DEVICE_CRD_YAML_1);
    auto j = yaml_to_json(y);
    DeviceCRD crd = j.get<DeviceCRD>();
    nlohmann::json j1 = crd;
    std::cout << j1.dump(2) << std::endl;
    CHECK(crd.spec.access.size() == 2);
    CHECK(crd.spec.access[0].type == "v4l2");
    CHECK(crd.metadata.name == "Camera");
    CHECK(crd.metadata.annotations["description"] == "standard camera device");
}
