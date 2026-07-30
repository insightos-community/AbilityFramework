// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "prelude.hpp"
#include "store_client.hpp"
#include <fstream>
#include <mutex>
#include <shared_mutex>
#include <span>

// a sub-module of ResourceManager; reads repo urls from config, gets local arch/system info, then handles download requests
class StoreManager {
    mutable std::recursive_mutex m_urls; // used to protect operations on urls
    mutable std::shared_mutex m_dirs; // protect package operations on the directory at the same time
    HostInfo host_info; // local system info
    std::vector<std::string> store_urls; // url of the repository

public:
    struct SpecWithUrl {
        std::string url;
        PackageSpec spec;
    };
    StoreManager();
    ///@ brief expose own packages_mutex lock
    /// ResourceManager also needs a read lock when reading packages to keep the packages directory stable
    auto& packages_mutex() const { return m_dirs; }
    /// given a spec, search for the package across all known urls
    /// @return on success, return a complete package and url,
    std::optional<SpecWithUrl> find_package(const PackageSpec& spec) const;
    /// @brief download a package
    /// @pre package.is_complete()
    /// @return abytesarray,indicates the downloaded zip package,on failure,it is empty
    std::vector<char> download_package(const PackageSpec& spec) const;

    /// @brief extract the package to a predetermined location
    /// @param spec package name and version info
    /// @param pkg_data ability stored as byte array zip package
    /// @return on failure,return error message
    /// @pre spec.is_complete()
    expected<void, ErrorMsg> extract_package(
        const PackageSpec& spec, std::span<const char> pkg_data
    );

    struct AddPackageRes {
        bool actually_installed;
        PackageSpec spec;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(AddPackageRes, actually_installed, spec);
    };
    ///@ brief add package,and extract to the specified location
    ///@param file_type supported file extensions,currently only supports 'zip', consider supporting 'tar.gz' or '7z' later
    ///@param pkg_data binary package file
    ///@param force if package exists, whether to force-replace the current package
    /// @return on success,then returnbool,indicates whether this package was actually installed,on failure,return error message
    [[nodiscard]]
    expected<AddPackageRes, ErrorMsg> add_package(
        std::string_view file_type, std::span<const char> pkg_data, bool force = false
    );

    struct RemovePackageRes {
        std::string result = "";
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(RemovePackageRes, result);
    };
    expected<RemovePackageRes, ErrorMsg> remove_package(const PackageSpec& spec);

    // ===== package-embedded CR mirror =====
    // take <pkg_install_dir>/crs/*.yaml copy to <framework_home>/crs/_packages/<pkg>/<version>/
    // identified as a template by read_crs_into_db during recursive scan; reconcile decides keep/delete.
    // return the number of files successfully copied.
    static size_t mirror_package_crs(
        const std::filesystem::path& pkg_install_dir,
        const std::string& pkg_name,
        const std::string& version
    );
    // clear mirrors on uninstall; if version is empty, clear all versions of the pkg
    static void unmirror_package_crs(const std::string& pkg_name, const std::string& version);
    // fixed location of the mirror directory (for testing / diagnostics)
    static std::filesystem::path mirrored_pkg_crs_dir(
        const std::string& pkg_name, const std::string& version
    );

    // ===== package-embedded Skill mirror =====
    // take <pkg_install_dir>/skills/** (recursion, any extension) copy to
    // <framework_home>/skills/_packages/<pkg>/<version>/, so that framework / webui /
    // the MCP server can discover package agent skill documentation from a fixed location.
    // skill files do not participate in reconcile DB, because they do not belong to the runtime model, just packaged documentation assets.
    static size_t mirror_package_skills(
        const std::filesystem::path& pkg_install_dir,
        const std::string& pkg_name,
        const std::string& version
    );
    static void unmirror_package_skills(const std::string& pkg_name, const std::string& version);
    static std::filesystem::path mirrored_pkg_skills_dir(
        const std::string& pkg_name, const std::string& version
    );

    // list all skill file metadata under framework_home/skills/_packages/
    struct SkillEntry {
        std::string package;
        std::string version;
        std::string filename; // relative <pkg>/<ver>/ path, e.g. "SKILL.md"or "tasks/move.md"
        std::size_t size_bytes;
        std::string title; // from frontmatter or thea first-level title extraction, leave empty if not found
    };
    static std::vector<SkillEntry> list_all_skills();

    // read a single skill file (UTF-8). Path must fall within the mirror directory to prevent path traversal.
    // return empty optional = not found / out of range / read failure.
    static std::optional<std::string> read_skill(
        const std::string& pkg_name, const std::string& version, const std::string& filename
    );
};
