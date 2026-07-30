#include "doctest.h"
#include "resourcemgr/device_crd.hpp"
#include "util/yaml_to_json.hpp"
#include <iostream>

namespace {
constexpr char DEVICE_CRD_YAML_1[] = R"yaml(
packageName: std.insightos.com # package name(must)
version: 0.1.0 # version name(must)
kind: CustomResourceDefinition # resource type, fixedisCustomResourceDefinition
metadata:
  name: Camera # device name, hereCamera indicates a generic camera
  labels: {}
  annotations:
    description: "standard camera device"

spec:
  access: # list access methods this device type may support
    - type: v4l2
      params:
        path: 
          type: string
          description: address of the device file
    - type: ros
      params:
        topic:
          type: object
          description: topic for camera imagetopic
          properties:
            color:
              type: string
              description: topic for color image
            depth:
              type: string
              description: topic for depth image
              default: null # this item is optional
        messageType:
          type: string
          description: indicatesrosin topicmessagetype
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
