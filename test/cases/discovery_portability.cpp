// Copyright 2026 InsightOS
// SPDX-License-Identifier: Apache-2.0
#include "doctest.h"
#include "resourcemgr/store_client.hpp"
#include "util/discovery_utils.hpp"
#include "discoverymgr/system_status.hpp"
#include <net/if.h>
#include <regex>

TEST_CASE("Network discovery returns real interfaces on POSIX hosts") {
    const auto interfaces = get_all_interfaces();
    REQUIRE_FALSE(interfaces.empty());
    for (const auto& name : interfaces) {
        CHECK(if_nametoindex(name.c_str()) != 0);
        const auto mac = get_mac_address(name);
        if (!mac.empty()) {
            CHECK(std::regex_match(mac, std::regex("([0-9a-f]{2}:){5}[0-9a-f]{2}")));
        }
    }
    CHECK(get_mac_address("semantic-invalid") == "");
    const auto primary = get_default_interface();
    if (!primary.empty()) { CHECK(if_nametoindex(primary.c_str()) != 0); }
}

TEST_CASE("System load sampling works without procfs on macOS") {
    const auto info = SystemInfo::current();
    CHECK(info.load != SystemLoad::unknown);
    CHECK(info.trend != SystemLoadTrend::unknown);
}

TEST_CASE("Package host information uses native OS metadata") {
    const auto host = HostInfo::read_from_system();
    CHECK_FALSE(host.os.empty());
    CHECK_FALSE(host.os_version.empty());
    CHECK_FALSE(host.arch.empty());
#ifdef __APPLE__
    CHECK(host.os == "macos");
#endif
}
