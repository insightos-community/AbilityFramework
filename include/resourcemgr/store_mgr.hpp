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
#include "prelude.hpp"
#include "store_client.hpp"
#include <fstream>
#include <mutex>
#include <shared_mutex>
#include <span>

// ResourceManager的一个子模块,从config读取仓库url,自身获得本机架构,系统等信息,然后处理下载请求
class StoreManager {
    mutable std::recursive_mutex m_urls; // 用以保护对urls的操作
    mutable std::shared_mutex m_dirs;    // 保护对package目录的操作,同一时间
    HostInfo host_info;                  // 本机系统信息
    std::vector<std::string> store_urls; // 仓库对应的url

public:
    struct SpecWithUrl {
        std::string url;
        PackageSpec spec;
    };
    StoreManager();
    ///@ brief 对外暴露自己的packages_mutex 锁
    /// ResourceManager在读取packages时,也需要加读取锁以保持packages目录稳定
    auto& packages_mutex() const { return m_dirs; }
    /// 给定一个spec,由此去从各个已知的url去查找包
    /// @return 如果成功,返回一个完整的package和url,
    std::optional<SpecWithUrl> find_package(const PackageSpec& spec) const;
    /// @brief 下载某个包
    /// @pre package.is_complete()
    /// @return 一个字节数组,表示下载下来的zip包,如果失败,它为空
    std::vector<char> download_package(const PackageSpec& spec) const;

    /// @brief 解压包到预定位置
    /// @param spec 包名称和版本信息
    /// @param pkg_data 以字节数组格式存储的能力zip包
    /// @return 如果失败,返回错误消息
    /// @pre spec.is_complete()
    expected<void, ErrorMsg> extract_package(
        const PackageSpec& spec, std::span<const char> pkg_data
    );

    struct AddPackageRes {
        bool actually_installed;
        PackageSpec spec;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(AddPackageRes, actually_installed, spec);
    };
    ///@ brief 添加包,并解压到指定位置
    ///@param file_type 支持的后缀名,目前仅支持 'zip', 后续考虑支持'tar.gz'或'7z'
    ///@param pkg_data 二进制的包文件
    ///@param force 如果包已存在,是否强制替换当前包
    /// @return 如果成功,则返回bool,表示这个包是否真的被安装,如果失败,返回错误消息
    [[nodiscard]]
    expected<AddPackageRes, ErrorMsg> add_package(
        std::string_view file_type, std::span<const char> pkg_data, bool force = false
    );

    struct RemovePackageRes {
        std::string result = "";
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(RemovePackageRes, result);
    };
    expected<RemovePackageRes, ErrorMsg> remove_package(const PackageSpec& spec);

    // ===== 包内嵌 CR 镜像 =====
    // 把 <pkg_install_dir>/crs/*.yaml 拷贝到 <framework_home>/crs/_packages/<pkg>/<version>/
    // 由 read_crs_into_db 递归扫描时识别为模板，由 reconcile 决定保留/删除。
    // 返回成功拷贝的文件数。
    static size_t mirror_package_crs(
        const std::filesystem::path& pkg_install_dir,
        const std::string& pkg_name,
        const std::string& version
    );
    // 卸载时清掉镜像; version 为空时清掉该 pkg 的所有版本
    static void unmirror_package_crs(const std::string& pkg_name, const std::string& version);
    // 镜像目录的固定位置 (供测试 / 诊断使用)
    static std::filesystem::path mirrored_pkg_crs_dir(
        const std::string& pkg_name, const std::string& version
    );

    // ===== 包内嵌 Skill 镜像 =====
    // 把 <pkg_install_dir>/skills/** (递归, 任意扩展名) 拷贝到
    // <framework_home>/skills/_packages/<pkg>/<version>/ , 让 framework / webui /
    // mcp server 都能从一个固定位置发现包带的 agent skill 文档。
    // skill 文件不参与 reconcile DB, 因为它不属于运行时模型, 只是 packaged 文档资产。
    static size_t mirror_package_skills(
        const std::filesystem::path& pkg_install_dir,
        const std::string& pkg_name,
        const std::string& version
    );
    static void unmirror_package_skills(const std::string& pkg_name, const std::string& version);
    static std::filesystem::path mirrored_pkg_skills_dir(
        const std::string& pkg_name, const std::string& version
    );

    // 列出当前 framework_home/skills/_packages/ 下所有的 skill 文件元数据
    struct SkillEntry {
        std::string package;
        std::string version;
        std::string filename;       // 相对 <pkg>/<ver>/ 的路径, 如 "SKILL.md" 或 "tasks/move.md"
        std::size_t size_bytes;
        std::string title;          // 从 frontmatter 或第一个一级标题提取, 找不到留空
    };
    static std::vector<SkillEntry> list_all_skills();

    // 读单个 skill 文件 (UTF-8). 路径必须落在镜像目录内, 防止 path traversal。
    // 返回空 optional = 找不到 / 越界 / 读失败。
    static std::optional<std::string> read_skill(
        const std::string& pkg_name, const std::string& version, const std::string& filename
    );
};
