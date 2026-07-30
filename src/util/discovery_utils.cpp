// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#include "util/discovery_utils.hpp"
#include <arpa/inet.h>
#include <cstring>
#include <fstream>
#include <glog/logging.h>
#include <ifaddrs.h>
#include <iostream>
#include <net/if.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <sstream>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdexcept>
#include <netdb.h>
#include <netinet/in.h>


// get the default network interface name
std::string get_default_interface() {
    std::ifstream routeFile("/proc/net/route");
    std::string line;
    std::string interface;

    // read /proc/net/route file
    while (std::getline(routeFile, line)) {
        std::istringstream iss(line);
        std::string iface;
        std::string destination;
        std::string gateway;

        iss >> iface >> destination >> gateway;

        // default route target address is 00000000, gateway is not 00000000
        if (destination == "00000000" && gateway != "00000000") {
            interface = iface;
            break;
        }
    }

    return interface;
}

// get the names of all network interfaces
std::vector<std::string> get_all_interfaces() {
    std::vector<std::string> interfaces;
    struct ifaddrs *ifaddr, *ifa;

    // get network interface info
    if (getifaddrs(&ifaddr) == -1) {
        LOG(ERROR) << "Error getting network interfaces: " << strerror(errno);
        return interfaces;
    }

    // iterate network interfaces
    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr) { continue; }

        // add interface name to list
        interfaces.push_back(ifa->ifa_name);
    }

    // free memory
    freeifaddrs(ifaddr);
    return interfaces;
}

// getMACaddress
std::string get_mac_address(const std::string& interface) {
    struct ifreq ifr;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);

    if (fd == -1) {
        perror("socket");
        return "";
    }

    ifr.ifr_addr.sa_family = AF_INET;
    strncpy(ifr.ifr_name, interface.c_str(), IFNAMSIZ - 1);

    if (ioctl(fd, SIOCGIFHWADDR, &ifr) == -1) {
        perror("ioctl");
        close(fd);
        return "";
    }

    close(fd);

    unsigned char* mac = (unsigned char*)ifr.ifr_hwaddr.sa_data;
    char macStr[18];
    snprintf(
        macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3],
        mac[4], mac[5]
    );

    return std::string(macStr);
}

// getIPv4address
std::vector<std::string> get_ipv4_addresses() {
    std::vector<std::string> ipv4_addresses;
    struct ifaddrs *ifaddr, *ifa;
    char addr_buf[INET_ADDRSTRLEN];

    // get network interface info
    if (getifaddrs(&ifaddr) == -1) {
        LOG(ERROR) << "Error getting network interfaces: " << strerror(errno);
        return ipv4_addresses;
    }

    // iterate network interfaces
    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        // skip interfaces without addresses
        if (ifa->ifa_addr == nullptr) { continue; }
        // onlygetIPv4address
        if (ifa->ifa_addr->sa_family == AF_INET) {
            void* addr_ptr = &((struct sockaddr_in*)ifa->ifa_addr)->sin_addr;
            // convert tostring
            inet_ntop(AF_INET, addr_ptr, addr_buf, INET_ADDRSTRLEN);
            // exclude local loopback addresses
            std::string ip_address(addr_buf);
            if (ip_address != "127.0.0.1") { ipv4_addresses.push_back(ip_address); }
        }
    }
    // free memory
    freeifaddrs(ifaddr);
    return ipv4_addresses;
}

// getIPv6address
std::vector<std::string> get_ipv6_addresses() {
    std::vector<std::string> ipv6_addresses;
    struct ifaddrs *ifaddr, *ifa;
    char addr_buf[INET6_ADDRSTRLEN];

    // get network interface info
    if (getifaddrs(&ifaddr) == -1) {
        LOG(ERROR) << "Error getting network interfaces: " << strerror(errno);
        return ipv6_addresses;
    }

    // iterate network interfaces
    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        // skip interfaces without addresses
        if (ifa->ifa_addr == nullptr) { continue; }
        // onlygetIPv6address
        if (ifa->ifa_addr->sa_family == AF_INET6) {
            void* addr_ptr = &((struct sockaddr_in6*)ifa->ifa_addr)->sin6_addr;
            // convert tostring
            inet_ntop(AF_INET6, addr_ptr, addr_buf, INET6_ADDRSTRLEN);
            // exclude local loopback addresses
            std::string ip_address(addr_buf);
            if (ip_address != "::1") { ipv6_addresses.push_back(ip_address); }
        }
    }
    // free memory
    freeifaddrs(ifaddr);
    return ipv6_addresses;
}

// SHA256
std::string sha256(const std::string& str) {
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    if (!context) { throw std::runtime_error("Failed to create EVP_MD_CTX"); }

    if (EVP_DigestInit_ex(context, EVP_sha256(), nullptr) != 1) {
        EVP_MD_CTX_free(context);
        throw std::runtime_error("Failed to initialize SHA-256 digest");
    }

    if (EVP_DigestUpdate(context, str.c_str(), str.size()) != 1) {
        EVP_MD_CTX_free(context);
        throw std::runtime_error("Failed to update SHA-256 digest");
    }

    unsigned char hash[SHA256_DIGEST_LENGTH];
    unsigned int lengthOfHash = 0;
    if (EVP_DigestFinal_ex(context, hash, &lengthOfHash) != 1) {
        EVP_MD_CTX_free(context);
        throw std::runtime_error("Failed to finalize SHA-256 digest");
    }

    EVP_MD_CTX_free(context);

    std::string result;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", hash[i]);
        result += buf;
    }
    return result;
}

// base64 encode
std::string base64_encode(const std::string& input) {
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* mem = BIO_new(BIO_s_mem());
    BIO_push(b64, mem);
    BIO_write(b64, input.c_str(), input.length());
    BIO_flush(b64);

    BUF_MEM* bptr;
    BIO_get_mem_ptr(b64, &bptr);

    std::string result(bptr->data, bptr->length - 1);
    BIO_free_all(b64);
    return result;
}

namespace mdns_cpp {

std::string getHostName() {
  const char *hostname = "dummy-host";

#ifdef _WIN32
  WORD versionWanted = MAKEWORD(1, 1);
  WSADATA wsaData;
  if (WSAStartup(versionWanted, &wsaData)) {
    const auto msg = "Error: Failed to initialize WinSock";
    // MDNS_LOG << msg << "\n";
    throw std::runtime_error(msg);
  }

  char hostname_buffer[256];
  DWORD hostname_size = (DWORD)sizeof(hostname_buffer);
  if (GetComputerNameA(hostname_buffer, &hostname_size)) {
    hostname = hostname_buffer;
  }

#else

  char hostname_buffer[256];
  const size_t hostname_size = sizeof(hostname_buffer);
  if (gethostname(hostname_buffer, hostname_size) == 0) {
    hostname = hostname_buffer;
  }

#endif

  return hostname;
}

std::string ipv4AddressToString(char *buffer, size_t capacity, const sockaddr_in *addr, size_t addrlen) {
  char host[NI_MAXHOST] = {0};
  char service[NI_MAXSERV] = {0};
  const int ret = getnameinfo((const struct sockaddr *)addr, (socklen_t)addrlen, host, NI_MAXHOST, service, NI_MAXSERV,
                              NI_NUMERICSERV | NI_NUMERICHOST);
  int len = 0;
  if (ret == 0) {
    if (addr->sin_port != 0) {
      len = snprintf(buffer, capacity, "%s:%s", host, service);
    } else {
      len = snprintf(buffer, capacity, "%s", host);
    }
  }
  if (len >= (int)capacity) {
    len = (int)capacity - 1;
  }

  return std::string(buffer, len);
}

std::string ipv6AddressToString(char *buffer, size_t capacity, const sockaddr_in6 *addr, size_t addrlen) {
  char host[NI_MAXHOST] = {0};
  char service[NI_MAXSERV] = {0};
  const int ret = getnameinfo((const struct sockaddr *)addr, (socklen_t)addrlen, host, NI_MAXHOST, service, NI_MAXSERV,
                              NI_NUMERICSERV | NI_NUMERICHOST);
  int len = 0;
  if (ret == 0) {
    if (addr->sin6_port != 0) {
      {
        len = snprintf(buffer, capacity, "[%s]:%s", host, service);
      }
    } else {
      len = snprintf(buffer, capacity, "%s", host);
    }
  }
  if (len >= (int)capacity) {
    len = (int)capacity - 1;
  }

  return std::string(buffer, len);
}

std::string ipAddressToString(char *buffer, size_t capacity, const sockaddr *addr, size_t addrlen) {
  if (addr->sa_family == AF_INET6) {
    return ipv6AddressToString(buffer, capacity, (const struct sockaddr_in6 *)addr, addrlen);
  }
  return ipv4AddressToString(buffer, capacity, (const struct sockaddr_in *)addr, addrlen);
}

}  // namespace mdns_cpp