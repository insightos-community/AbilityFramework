#include "doctest.h"
#include "resourcemgr/device_cr.hpp"
#include "util/yaml_to_json.hpp"

namespace {
constexpr char DEVICE_CR_YAML_1[] = R"yaml(
kind: Device # resource type, fixedisDevice
metadata:
  name: raspberrypi-camera # device instance name (must)

spec:
  package: std.insightos.com # package name(must)
  version: 0.1.0 # package version(must)
  deviceName: Camera # device type name(must)
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
