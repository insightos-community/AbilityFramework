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

#include <vector>
#include <string>
#include <iostream>


std::string get_default_interface();
std::vector<std::string> get_all_interfaces();

std::string get_mac_address(const std::string& interface);
std::vector<std::string> get_ipv4_addresses();
std::vector<std::string> get_ipv6_addresses();

// 暂时用不到
std::string sha256(const std::string& str);
std::string base64_encode(const std::string& input);

// mDNS
struct sockaddr;
struct sockaddr_in;
struct sockaddr_in6;

namespace mdns_cpp {

std::string getHostName(); 
std::string ipv4AddressToString(char *buffer, size_t capacity, const struct sockaddr_in *addr, size_t addrlen);
std::string ipv6AddressToString(char *buffer, size_t capacity, const struct sockaddr_in6 *addr, size_t addrlen);
std::string ipAddressToString(char *buffer, size_t capacity, const struct sockaddr *addr, size_t addrlen);

}  // mdns_cpp