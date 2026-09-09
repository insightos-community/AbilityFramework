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

#include "discoverymgr/system_status.hpp"
#include <fstream>
#include <glog/logging.h>
#include <thread>
namespace {

SystemLoadTrend getSystemLoadTrend(double load1, double load5, double load15) {
    if (load1 > load5 && load5 > load15) { return SystemLoadTrend::increasing; }
    else if (load1 < load5 && load5 < load15) { return SystemLoadTrend::decreasing; }
    else { return SystemLoadTrend::stable; }
}
SystemLoad getSystemLoad(double load1, double load15, int cpuCores) {
    if (load15 > cpuCores) { return SystemLoad::overloaded; }
    else if (load1 < cpuCores / 2.0) { return SystemLoad::idle; }
    else if (load1 >= cpuCores / 2.0 && load1 <= cpuCores * 1.5) { return SystemLoad::optimal; }
    else { return SystemLoad::busy; }
}
} // namespace
SystemInfo SystemInfo::current() {
    std::ifstream file("/proc/loadavg");
    if (!file) { LOG(ERROR) << "Cannot open /proc/loadavg\n"; }

    double load1, load5, load15;
    if (!(file >> load1 >> load5 >> load15)) { LOG(ERROR) << "Cannot read /proc/loadavg\n"; }

    const int cpuCores = std::thread::hardware_concurrency();

    SystemLoadTrend trend = getSystemLoadTrend(load1, load15, cpuCores);
    SystemLoad load = getSystemLoad(load1, load5, load15);
    return SystemInfo{.load = load, .trend = trend};
}
