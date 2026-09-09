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
    std::string os; /// 本机操作系统名
    std::string os_version;
    std::string arch; /// 本机架构
    static HostInfo read_from_system();
};
// 一个对某种类型的url有效的一个虚基类
struct StoreClient {
    virtual ~StoreClient() = default;
    virtual std::string url() const = 0;
    // 查找某个包是否存在
    virtual std::optional<PackageSpec> find_package(const PackageSpec& spec) const = 0;
    /// @brief 下载某个包
    /// @pre package.is_complete()
    /// @return 一个字节数组,表示下载下来的zip包,如果失败,它为空
    virtual std::vector<char> download(const PackageSpec& spec) const = 0;
    /// @brief 从url构建StoreClient
    /// @return 对应的CLient基类, 如果失败,返回nullptr,并在日志上打印错误
    static std::unique_ptr<StoreClient> make(const HostInfo& host_info, std::string_view url);
};
