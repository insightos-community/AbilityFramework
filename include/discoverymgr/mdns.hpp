// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <functional>
#include <string>
#include <thread>
#include "util/mdns.h"

struct sockaddr;

namespace mdns_cpp {

// service record
class ServiceRecord {
 public:
  const char *service;
  const char *hostname;
  uint32_t address_ipv4;
  uint8_t *address_ipv6;
  uint16_t port;
};

class mDNS {
 public:
  ~mDNS();

  void startService();
  void stopService();
  bool isServiceRunning();

  void setServiceHostname(const std::string &hostname);
  void setServicePort(std::uint16_t port);
  void setServiceName(const std::string &name);
  void setServiceTxtRecord(const std::string &text_record);

  void executeQuery(const std::string &service);
  void executeDiscovery();

 private:
  void runMainLoop();
  int openClientSockets(int *sockets, int max_sockets, int port);
  int openServiceSockets(int *sockets, int max_sockets);

  std::string hostname_{"AbilityFramework"};
  std::string name_{"_AbilityFramework._tcp.local."};
  std::uint16_t port_{8080};
  std::string txt_record_{};

  bool running_{false};

  bool has_ipv4_{false};
  bool has_ipv6_{false};

  uint32_t service_address_ipv4_{0};
  uint8_t service_address_ipv6_[16]{0};

  std::thread worker_thread_;
};

}  // namespace mdns_cpp
