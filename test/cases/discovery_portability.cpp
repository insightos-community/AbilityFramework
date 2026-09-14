// Copyright 2026 InsightOS
// SPDX-License-Identifier: Apache-2.0
#include "doctest.h"
#include "resourcemgr/store_client.hpp"
#include "util/discovery_utils.hpp"
#include "discoverymgr/system_status.hpp"
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#else
#include <net/if.h>
#endif
#include <regex>

namespace {
bool interface_exists(const std::string& name) {
#ifdef _WIN32
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name.c_str(), -1, nullptr, 0);
    if (size <= 0) return false;
    std::wstring alias(size, L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name.c_str(), -1, alias.data(), size)) return false;
    NET_LUID luid{};
    return ConvertInterfaceAliasToLuid(alias.c_str(), &luid) == NO_ERROR;
#else
    return if_nametoindex(name.c_str()) != 0;
#endif
}
}


TEST_CASE("Network discovery returns real interfaces on native hosts") {
    const auto interfaces = get_all_interfaces();
    REQUIRE_FALSE(interfaces.empty());
    for (const auto& name : interfaces) {
        CHECK(interface_exists(name));
        const auto mac = get_mac_address(name);
        if (!mac.empty()) {
            CHECK(std::regex_match(mac, std::regex("([0-9a-f]{2}:){5}[0-9a-f]{2}")));
        }
    }
    CHECK(get_mac_address("semantic-invalid") == "");
    const auto primary = get_default_interface();
    if (!primary.empty()) { CHECK(interface_exists(primary)); }
}

TEST_CASE("System load sampling works without procfs on macOS") {
    const auto info = SystemInfo::current();
#ifdef _WIN32
    CHECK(info.load == SystemLoad::unknown);
    CHECK(info.trend == SystemLoadTrend::unknown);
#else
    CHECK(info.load != SystemLoad::unknown);
    CHECK(info.trend != SystemLoadTrend::unknown);
#endif
}

TEST_CASE("Package host information uses native OS metadata") {
    const auto host = HostInfo::read_from_system();
    CHECK_FALSE(host.os.empty());
    CHECK_FALSE(host.os_version.empty());
    CHECK_FALSE(host.arch.empty());
#ifdef __APPLE__
    CHECK(host.os == "macos");
#elif defined(_WIN32)
    CHECK(host.os == "windows");
    CHECK(host.arch == "x86_64");
#endif
}
