// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vector>
#include <string>
#include <iostream>


std::string get_default_interface();
std::vector<std::string> get_all_interfaces();

std::string get_mac_address(const std::string& interface);
std::vector<std::string> get_ipv4_addresses();
std::vector<std::string> get_ipv6_addresses();

// not needed for now
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