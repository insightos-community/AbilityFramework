// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#include "discoverymgr/mdns.hpp"
#include "util/discovery_utils.hpp"
#include <glog/logging.h>
#include <iostream>
#include <memory>
#include <thread>
#include <netdb.h>
#include <string.h>
#include <ifaddrs.h>
#include <netinet/in.h>


namespace mdns_cpp {

static mdns_record_txt_t txtbuffer[128];

// open socket for mDNS service communication
int mDNS::openServiceSockets(int *sockets, int max_sockets) {

  int num_sockets = 0;
  openClientSockets(0, 0, 0);

  if (num_sockets < max_sockets) {
    sockaddr_in sock_addr{};
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_addr.s_addr = INADDR_ANY;
    sock_addr.sin_port = htons(MDNS_PORT);
    const int sock = mdns_socket_open_ipv4(&sock_addr);
    if (sock >= 0) {
      sockets[num_sockets++] = sock;
    }
  }

  if (num_sockets < max_sockets) {
    sockaddr_in6 sock_addr{};
    sock_addr.sin6_family = AF_INET6;
    sock_addr.sin6_addr = in6addr_any;
    sock_addr.sin6_port = htons(MDNS_PORT);
    int sock = mdns_socket_open_ipv6(&sock_addr);
    if (sock >= 0) sockets[num_sockets++] = sock;
  }

  return num_sockets;
}

// open socket for mDNS client queries
int mDNS::openClientSockets(int *sockets, int max_sockets, int port) {

  int num_sockets = 0;
  struct ifaddrs *ifaddr = nullptr;
  struct ifaddrs *ifa = nullptr;

  if (getifaddrs(&ifaddr) < 0) {
    LOG(INFO) << "Unable to get interface addresses\n";
  }

  int first_ipv4 = 1;
  int first_ipv6 = 1;
  for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
    if (!ifa->ifa_addr) {
      continue;
    }

    if (ifa->ifa_addr->sa_family == AF_INET) {
      struct sockaddr_in *saddr = (struct sockaddr_in *)ifa->ifa_addr;
      if (saddr->sin_addr.s_addr != htonl(INADDR_LOOPBACK)) {
        int log_addr = 0;
        if (first_ipv4) {
          service_address_ipv4_ = saddr->sin_addr.s_addr;
          first_ipv4 = 0;
          log_addr = 1;
        }
        has_ipv4_ = 1;
        if (num_sockets < max_sockets) {
          saddr->sin_port = htons(port);
          int sock = mdns_socket_open_ipv4(saddr);
          if (sock >= 0) {
            sockets[num_sockets++] = sock;
            log_addr = 1;
          } else {
            log_addr = 0;
          }
        }
        if (log_addr) {
          char buffer[128];
          const auto addr = ipv4AddressToString(buffer, sizeof(buffer), saddr, sizeof(struct sockaddr_in));
          LOG(INFO) << "Local IPv4 address: " << addr << "\n";
        }
      }
    } else if (ifa->ifa_addr->sa_family == AF_INET6) {
      struct sockaddr_in6 *saddr = (struct sockaddr_in6 *)ifa->ifa_addr;
      static constexpr unsigned char localhost[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
      static constexpr unsigned char localhost_mapped[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 0x7f, 0, 0, 1};
      if (memcmp(saddr->sin6_addr.s6_addr, localhost, 16) && memcmp(saddr->sin6_addr.s6_addr, localhost_mapped, 16)) {
        int log_addr = 0;
        if (first_ipv6) {
          memcpy(service_address_ipv6_, &saddr->sin6_addr, 16);
          first_ipv6 = 0;
          log_addr = 1;
        }
        has_ipv6_ = 1;
        if (num_sockets < max_sockets) {
          saddr->sin6_port = htons(port);
          int sock = mdns_socket_open_ipv6(saddr);
          if (sock >= 0) {
            sockets[num_sockets++] = sock;
            log_addr = 1;
          } else {
            log_addr = 0;
          }
        }
        if (log_addr) {
          char buffer[128] = {};
          const auto addr = ipv6AddressToString(buffer, sizeof(buffer), saddr, sizeof(struct sockaddr_in6));
          LOG(INFO) << "Local IPv6 address: " << addr << "\n";
        }
      }
    }
  }

  freeifaddrs(ifaddr);

  return num_sockets;
}

// callback for parsing records in received mDNS packets
// this function is called when an mDNS query or response is received, and parses the record type and content
static int query_callback(int sock, const struct sockaddr *from, size_t addrlen, mdns_entry_type_t entry,
                          uint16_t query_id, uint16_t rtype, uint16_t rclass, uint32_t ttl, const void *data,
                          size_t size, size_t name_offset, size_t name_length, size_t record_offset,
                          size_t record_length, void *user_data) {
  (void)sizeof(sock);
  (void)sizeof(query_id);
  (void)sizeof(name_length);
  (void)sizeof(user_data);

  static char addrbuffer[64]{};
  static char namebuffer[256]{};
  static char entrybuffer[256]{};

  const auto fromaddrstr = ipAddressToString(addrbuffer, sizeof(addrbuffer), from, addrlen);
  const char *entrytype =
      (entry == MDNS_ENTRYTYPE_ANSWER) ? "answer" : ((entry == MDNS_ENTRYTYPE_AUTHORITY) ? "authority" : "additional");
  mdns_string_t entrystr = mdns_string_extract(data, size, &name_offset, entrybuffer, sizeof(entrybuffer));

  const int str_capacity = 1000;
  char str_buffer[str_capacity]={};

  if (rtype == MDNS_RECORDTYPE_PTR) {
    mdns_string_t namestr =
        mdns_record_parse_ptr(data, size, record_offset, record_length, namebuffer, sizeof(namebuffer));

    snprintf(str_buffer, str_capacity, "%s : %s %.*s PTR %.*s rclass 0x%x ttl %u length %d\n", fromaddrstr.data(),
             entrytype, MDNS_STRING_FORMAT(entrystr), MDNS_STRING_FORMAT(namestr), rclass, ttl, (int)record_length);
  } else if (rtype == MDNS_RECORDTYPE_SRV) {
    mdns_record_srv_t srv =
        mdns_record_parse_srv(data, size, record_offset, record_length, namebuffer, sizeof(namebuffer));
    snprintf(str_buffer, str_capacity,"%s : %s %.*s SRV %.*s priority %d weight %d port %d\n", fromaddrstr.data(), entrytype,
           MDNS_STRING_FORMAT(entrystr), MDNS_STRING_FORMAT(srv.name), srv.priority, srv.weight, srv.port);
  } else if (rtype == MDNS_RECORDTYPE_A) {
    struct sockaddr_in addr;
    mdns_record_parse_a(data, size, record_offset, record_length, &addr);
    const auto addrstr = ipv4AddressToString(namebuffer, sizeof(namebuffer), &addr, sizeof(addr));
    snprintf(str_buffer, str_capacity,"%s : %s %.*s A %s\n", fromaddrstr.data(), entrytype, MDNS_STRING_FORMAT(entrystr), addrstr.data());
  } else if (rtype == MDNS_RECORDTYPE_AAAA) {
    struct sockaddr_in6 addr;
    mdns_record_parse_aaaa(data, size, record_offset, record_length, &addr);
    const auto addrstr = ipv6AddressToString(namebuffer, sizeof(namebuffer), &addr, sizeof(addr));
    snprintf(str_buffer, str_capacity,"%s : %s %.*s AAAA %s\n", fromaddrstr.data(), entrytype, MDNS_STRING_FORMAT(entrystr), addrstr.data());
  } else if (rtype == MDNS_RECORDTYPE_TXT) {
    size_t parsed = mdns_record_parse_txt(data, size, record_offset, record_length, txtbuffer,
                                          sizeof(txtbuffer) / sizeof(mdns_record_txt_t));
    for (size_t itxt = 0; itxt < parsed; ++itxt) {
      if (txtbuffer[itxt].value.length) {
        snprintf(str_buffer, str_capacity,"%s : %s %.*s TXT %.*s = %.*s\n", fromaddrstr.data(), entrytype, MDNS_STRING_FORMAT(entrystr),
               MDNS_STRING_FORMAT(txtbuffer[itxt].key), MDNS_STRING_FORMAT(txtbuffer[itxt].value));
      } else {
        snprintf(str_buffer, str_capacity,"%s : %s %.*s TXT %.*s\n", fromaddrstr.data(), entrytype, MDNS_STRING_FORMAT(entrystr),
               MDNS_STRING_FORMAT(txtbuffer[itxt].key));
      }
    }
  } else {
    snprintf(str_buffer, str_capacity,"%s : %s %.*s type %u rclass 0x%x ttl %u length %d\n", fromaddrstr.data(), entrytype,
           MDNS_STRING_FORMAT(entrystr), rtype, rclass, ttl, (int)record_length);
  }
  LOG(INFO) << std::string(str_buffer);

  return 0;
}

// callback for handling mDNS service queries
// this function is called when an mDNS query is received, and sends based on query type
int service_callback(int sock, const struct sockaddr *from, size_t addrlen, mdns_entry_type entry, uint16_t query_id,
                     uint16_t rtype, uint16_t rclass, uint32_t ttl, const void *data, size_t size, size_t name_offset,
                     size_t name_length, size_t record_offset, size_t record_length, void *user_data) {
  (void)sizeof(name_offset);
  (void)sizeof(name_length);
  (void)sizeof(ttl);

  if (static_cast<int>(entry) != MDNS_ENTRYTYPE_QUESTION) {
    return 0;
  }

  char addrbuffer[64] = {0};
  char namebuffer[256] = {0};

  const auto fromaddrstr = ipAddressToString(addrbuffer, sizeof(addrbuffer), from, addrlen);
  if (rtype == static_cast<uint16_t>(mdns_record_type::MDNS_RECORDTYPE_PTR)) {
    const mdns_string_t service =
        mdns_record_parse_ptr(data, size, record_offset, record_length, namebuffer, sizeof(namebuffer));

    // LOG(INFO) << fromaddrstr << " : question PTR " << std::string(service.str, service.length) << "\n";

    const char dns_sd[] = "_services._dns-sd._udp.local.";
    const ServiceRecord *service_record = (const ServiceRecord *)user_data;
    const size_t service_length = strlen(service_record->service);
    char sendbuffer[256] = {0};

    if ((service.length == (sizeof(dns_sd) - 1)) && (strncmp(service.str, dns_sd, sizeof(dns_sd) - 1) == 0)) {
      LOG(INFO) << "Answer " << service_record->service << " \n";
      mdns_discovery_answer(sock, from, addrlen, sendbuffer, sizeof(sendbuffer), service_record->service,
                            service_length);
    } else if ((service.length == service_length) &&
               (strncmp(service.str, service_record->service, service_length) == 0)) {
      uint16_t unicast = (rclass & MDNS_UNICAST_RESPONSE);
      LOG(INFO) << "Answer " << service_record->hostname << "." << service_record->service << " port "
               << service_record->port << " (" << (unicast ? "unicast" : "multicast") << ")\n";
      if (!unicast) addrlen = 0;
      char txt_record[] = "asdf=1";
      mdns_query_answer(sock, from, addrlen, sendbuffer, sizeof(sendbuffer), query_id, service_record->service,
                        service_length, service_record->hostname, strlen(service_record->hostname),
                        service_record->address_ipv4, service_record->address_ipv6, (uint16_t)service_record->port,
                        txt_record, sizeof(txt_record));
    }
  } else if (rtype == static_cast<uint16_t>(mdns_record_type::MDNS_RECORDTYPE_SRV)) {
    mdns_record_srv_t service =
        mdns_record_parse_srv(data, size, record_offset, record_length, namebuffer, sizeof(namebuffer));
    LOG(INFO) << fromaddrstr << " : question SRV  " << "\n";

  }
  return 0;
}

// destructor
mDNS::~mDNS() { stopService(); }

// start mDNS service
// if the service is already running, then firststop service, then restart
void mDNS::startService() {
  if (running_) {
    stopService();
  }

  running_ = true;
  worker_thread_ = std::thread([this]() { this->runMainLoop(); });
}

// stop mDNS service
// this function sets the run flag to false and waits for worker threads to finish
void mDNS::stopService() {
  running_ = false;
  if (worker_thread_.joinable()) {
    worker_thread_.join();
  }
}

// check whether the mDNS service is running
bool mDNS::isServiceRunning() { return running_; }

// set hostname, port, name and TXT record of the mDNS service
void mDNS::setServiceHostname(const std::string &hostname) { hostname_ = hostname; }

// set the port of the mDNS service
void mDNS::setServicePort(std::uint16_t port) { port_ = port; }

// set the name of the mDNS service
void mDNS::setServiceName(const std::string &name) { name_ = name; }

// set TXT record of the mDNS service
void mDNS::setServiceTxtRecord(const std::string &txt_record) { txt_record_ = txt_record; }

// mDNS service, listens for incoming mDNS queries and generates responses based on query content
void mDNS::runMainLoop() {
  constexpr size_t number_of_sockets = 32;
  int sockets[number_of_sockets];
  const int num_sockets = openServiceSockets(sockets, sizeof(sockets) / sizeof(sockets[0]));
  if (num_sockets <= 0) {
    const auto msg = "Error: Failed to open any client sockets";
    LOG(INFO) << msg << "\n";
    throw std::runtime_error(msg);
  }

  LOG(INFO) << "Opened " << std::to_string(num_sockets) << " socket" << (num_sockets ? "s" : "")
           << " for mDNS service\n";
  LOG(INFO) << "Service mDNS: " << name_ << ":" << port_ << "\n";
  LOG(INFO) << "Hostname: " << hostname_.data() << "\n";

  constexpr size_t capacity = 2048u;
  std::shared_ptr<void> buffer(malloc(capacity), free);
  ServiceRecord service_record{};
  service_record.service = name_.data();
  service_record.hostname = hostname_.data();
  service_record.address_ipv4 = has_ipv4_ ? service_address_ipv4_ : 0;
  service_record.address_ipv6 = has_ipv6_ ? service_address_ipv6_ : 0;
  service_record.port = port_;

  // check the incoming query
  while (running_) {
    int nfds = 0;
    fd_set readfs{};
    FD_ZERO(&readfs);
    for (int isock = 0; isock < num_sockets; ++isock) {
      if (sockets[isock] >= nfds) nfds = sockets[isock] + 1;
      FD_SET(sockets[isock], &readfs);
    }

    if (select(nfds, &readfs, 0, 0, 0) >= 0) {
      for (int isock = 0; isock < num_sockets; ++isock) {
        if (FD_ISSET(sockets[isock], &readfs)) {
          mdns_socket_listen(sockets[isock], buffer.get(), capacity, service_callback, &service_record);
        }
        FD_SET(sockets[isock], &readfs);
      }
    } else {
      break;
    }
  }

  for (int isock = 0; isock < num_sockets; ++isock) {
    mdns_socket_close(sockets[isock]);
  }
  // LOG(INFO) << "Closed socket " << (num_sockets ? "s" : "") << "\n";
}

// perform mDNS query
// this function opens a client socket, sends a query request, and handles the query response
// query a specific service instance e.g. _AbilityFramework._tcp.local.
void mDNS::executeQuery(const std::string &service) {
  int sockets[32];
  int query_id[32];
  int num_sockets = openClientSockets(sockets, sizeof(sockets) / sizeof(sockets[0]), 0);

  if (num_sockets <= 0) {
    const auto msg = "Failed to open any client sockets";
    LOG(INFO) << msg << "\n";
    throw std::runtime_error(msg);
  }
  LOG(INFO) << "Opened " << num_sockets << " socket" << (num_sockets ? "s" : "") << " for mDNS query\n";

  size_t capacity = 2048;
  void *buffer = malloc(capacity);
  void *user_data = 0;
  size_t records;

  LOG(INFO) << "Sending mDNS query: " << service << "\n";
  for (int isock = 0; isock < num_sockets; ++isock) {
    query_id[isock] = mdns_query_send(sockets[isock], MDNS_RECORDTYPE_PTR, service.data(), strlen(service.data()),
                                      buffer, capacity, 0);
    if (query_id[isock] < 0) {
      LOG(INFO) << "Failed to send mDNS query: " << strerror(errno) << "\n";
    }
  }

  // wait 5s for reply
  int res{};
  LOG(INFO) << "Reading mDNS query replies\n";
  do {
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;

    int nfds = 0;
    fd_set readfs;
    FD_ZERO(&readfs);
    for (int isock = 0; isock < num_sockets; ++isock) {
      if (sockets[isock] >= nfds) nfds = sockets[isock] + 1;
      FD_SET(sockets[isock], &readfs);
    }

    records = 0;
    res = select(nfds, &readfs, 0, 0, &timeout);
    if (res > 0) {
      for (int isock = 0; isock < num_sockets; ++isock) {
        if (FD_ISSET(sockets[isock], &readfs)) {
          records += mdns_query_recv(sockets[isock], buffer, capacity, query_callback, user_data, query_id[isock]);
        }
        FD_SET(sockets[isock], &readfs);
      }
    }
  } while (res > 0);

  free(buffer);

  for (int isock = 0; isock < num_sockets; ++isock) {
    mdns_socket_close(sockets[isock]);
  }
  LOG(INFO) << "Closed socket" << (num_sockets ? "s" : "") << "\n";
}

// perform mDNS service discovery
// this function opens a client socket, sends a service-discovery request, and handles service discovery
// discover all service types; fixed discovery target _services._dns-sd._udp.local.
void mDNS::executeDiscovery() {
  int sockets[32];
  int num_sockets = openClientSockets(sockets, sizeof(sockets) / sizeof(sockets[0]), 0);
  if (num_sockets <= 0) {
    const auto msg = "Failed to open any client sockets";
    LOG(INFO) << msg << "\n";
    throw std::runtime_error(msg);
  }

  LOG(INFO) << "Opened " << num_sockets << " socket" << (num_sockets ? "s" : "") << " for DNS-SD\n";
  LOG(INFO) << "Sending DNS-SD discovery\n";
  for (int isock = 0; isock < num_sockets; ++isock) {
    if (mdns_discovery_send(sockets[isock])) {
      LOG(INFO) << "Failed to send DNS-DS discovery: " << strerror(errno) << " \n";
    }
  }

  size_t capacity = 2048;
  void *buffer = malloc(capacity);
  void *user_data = 0;
  size_t records;

  // wait 5s for reply
  int res;
  LOG(INFO) << "Reading DNS-SD replies\n";
  do {
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;

    int nfds = 0;
    fd_set readfs;
    FD_ZERO(&readfs);
    for (int isock = 0; isock < num_sockets; ++isock) {
      if (sockets[isock] >= nfds) nfds = sockets[isock] + 1;
      FD_SET(sockets[isock], &readfs);
    }

    records = 0;
    res = select(nfds, &readfs, 0, 0, &timeout);
    if (res > 0) {
      for (int isock = 0; isock < num_sockets; ++isock) {
        if (FD_ISSET(sockets[isock], &readfs)) {
          records += mdns_discovery_recv(sockets[isock], buffer, capacity, query_callback, user_data);
        }
      }
    }
  } while (res > 0);

  free(buffer);

  for (int isock = 0; isock < num_sockets; ++isock) {
    mdns_socket_close(sockets[isock]);
  }
  LOG(INFO) << "Closed socket" << (num_sockets ? "s" : "") << "\n";
}

}  // namespace mdns_cpp
