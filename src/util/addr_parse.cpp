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

#include "util/parse_addr.hpp"
#include <regex>
#include <string_view>
#include "ports/interfaces.hpp"
#ifndef _WIN32
#include <netinet/in.h>
#endif
#ifndef _WIN32
#include <arpa/inet.h>
#endif
#include "util/global_vars.hpp"

namespace {
constexpr char REGEX_IPV4[]
    = R"regex((?:(?:1[0-9][0-9]\.)|(?:2[0-4][0-9]\.)|(?:25[0-5]\.)|(?:[1-9][0-9]\.)|(?:[0-9]\.)){3}(?:(?:1[0-9][0-9])|(?:2[0-4][0-9])|(?:25[0-5])|(?:[1-9][0-9])|(?:[0-9])))regex";

constexpr char REGEX_IPV6[]
    = R"regex((([0-9a-fA-F]{1,4}:){7,7}[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,7}:|([0-9a-fA-F]{1,4}:){1,6}:[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,5}(:[0-9a-fA-F]{1,4}){1,2}|([0-9a-fA-F]{1,4}:){1,4}(:[0-9a-fA-F]{1,4}){1,3}|([0-9a-fA-F]{1,4}:){1,3}(:[0-9a-fA-F]{1,4}){1,4}|([0-9a-fA-F]{1,4}:){1,2}(:[0-9a-fA-F]{1,4}){1,5}|[0-9a-fA-F]{1,4}:((:[0-9a-fA-F]{1,4}){1,6})|:((:[0-9a-fA-F]{1,4}){1,7}|:)|fe80:(:[0-9a-fA-F]{0,4}){0,4}%[0-9a-zA-Z]{1,}|::(ffff(:0{1,4}){0,1}:){0,1}((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])|([0-9a-fA-F]{1,4}:){1,4}:((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])))regex";
} // namespace

bool is_valid_ipv4(std::string_view ip) {
    static const std::regex ipv4_regex(REGEX_IPV4);
    return std::regex_match(ip.begin(), ip.end(), ipv4_regex);
}

bool is_valid_ipv6(std::string_view ip) {
    static const std::regex ipv4_regex(REGEX_IPV6);
    return std::regex_match(ip.begin(), ip.end(), ipv4_regex);
}

// 获取本机ipv4和ipv6地址，框架名，框架id
std::vector<std::string> get_local_ip_addresses() {
    std::vector<std::string> ip_list;
    struct ifaddrs* ifaddr;
    if (getifaddrs(&ifaddr) == -1) { return ip_list; }
    for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;
        int family = ifa->ifa_addr->sa_family;
        char ip[INET6_ADDRSTRLEN];
        // IPv4
        if (family == AF_INET) {
            struct sockaddr_in* sa = (struct sockaddr_in*)ifa->ifa_addr;
            inet_ntop(AF_INET, &(sa->sin_addr), ip, sizeof(ip));
            ip_list.push_back(ip);
        }
        // IPv6
        else if (family == AF_INET6) {
            struct sockaddr_in6* sa6 = (struct sockaddr_in6*)ifa->ifa_addr;
            inet_ntop(AF_INET6, &(sa6->sin6_addr), ip, sizeof(ip));
            ip_list.push_back(ip);
        }
    }
    freeifaddrs(ifaddr);
    std::string framework_name = global_vars::get_config<std::string>("/framework_name");
    ip_list.emplace_back(framework_name);
    return ip_list;
}
