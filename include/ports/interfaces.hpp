// Copyright 2026 InsightOS
// SPDX-License-Identifier: Apache-2.0
#pragma once
#ifdef _WIN32
#include <uv.h>
#include <cstring>
#include <cstdlib>
#include <cerrno>
// A narrow getifaddrs adapter for the existing discovery callers. libuv owns
// platform enumeration; each returned address and name has an independent copy.
struct ifaddrs {
    ifaddrs* ifa_next = nullptr;
    char* ifa_name = nullptr;
    sockaddr* ifa_addr = nullptr;
    unsigned char physical[6] = {};
    bool internal = false;
};
inline void freeifaddrs(ifaddrs* head) {
    while(head) { auto next=head->ifa_next; std::free(head->ifa_name); std::free(head->ifa_addr); delete head; head=next; }
}
inline int getifaddrs(ifaddrs** output) {
    *output=nullptr;
    uv_interface_address_t* addresses=nullptr; int count=0;
    if(uv_interface_addresses(&addresses,&count)!=0) { errno=EIO; return -1; }
    auto tail=output;
    for(int i=0;i<count;i++) {
        auto entry=new ifaddrs;
        entry->ifa_name=_strdup(addresses[i].name);
        auto length=addresses[i].address.address4.sin_family==AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
        entry->ifa_addr=static_cast<sockaddr*>(std::malloc(length));
        if(!entry->ifa_name || !entry->ifa_addr) { std::free(entry->ifa_name); std::free(entry->ifa_addr); delete entry; freeifaddrs(*output); *output=nullptr; uv_free_interface_addresses(addresses,count); errno=ENOMEM; return -1; }
        std::memcpy(entry->ifa_addr,&addresses[i].address,length);
        std::memcpy(entry->physical,addresses[i].phys_addr,6);
        entry->internal=addresses[i].is_internal!=0;
        *tail=entry;tail=&entry->ifa_next;
    }
    uv_free_interface_addresses(addresses,count);return 0;
}
#else
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif
