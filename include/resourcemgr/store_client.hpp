// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <nlohmann/json.hpp>
#include <string>

struct PackageSpec {
    std::string package;
    std::string version;
    std::string arch;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(PackageSpec, package, version, arch)
    [[nodiscard]] bool is_complete() const {
        return (!package.empty()) && (!version.empty()) && (!arch.empty());
    }
};

struct HostInfo {
    std::string os; /// local OS name
    std::string os_version;
    std::string arch; /// local architecture
    static HostInfo read_from_system();
};
// an abstract base class valid for a certain type of url
struct StoreClient {
    virtual ~StoreClient() = default;
    virtual std::string url() const = 0;
    // check whether a package exists
    virtual std::optional<PackageSpec> find_package(const PackageSpec& spec) const = 0;
    /// @brief download a package
    /// @pre package.is_complete()
    /// @return abytesarray,indicates the downloaded zip package,on failure,it is empty
    virtual std::vector<char> download(const PackageSpec& spec) const = 0;
    /// @brief build a StoreClient from a url
    /// @return corresponding Client base class; on failure, returns nullptr and prints error in log
    static std::unique_ptr<StoreClient> make(const HostInfo& host_info, std::string_view url);
};
