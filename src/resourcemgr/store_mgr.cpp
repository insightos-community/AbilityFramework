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

#include "resourcemgr/store_mgr.hpp"
#include "util/global_vars.hpp"
#include "util/make_uuid.hpp"
#include "util/scope.hpp"
#include <cstring>
#include <glog/logging.h>
#include <miniz/miniz.h>
#include <sstream>
#include <yaml-cpp/yaml.h>

using Path = std::filesystem::path;

namespace {
std::vector<std::string> read_store_urls() {
    std::vector<std::string> res
        = global_vars::get_config<std::vector<std::string>>("/source_urls", {});
    return res;
} // 仓库对应的url
} // namespace

StoreManager::StoreManager()
    : host_info{HostInfo::read_from_system()}
    , store_urls{read_store_urls()} {}

auto StoreManager::find_package(const PackageSpec& spec) const -> std::optional<SpecWithUrl> {
    std::lock_guard _lk(m_urls);
    for (const auto& url : store_urls) {
        auto cli = StoreClient::make(host_info, url);
        if (!cli) {
            LOG(INFO) << "make client for " << url << " failed";
            continue;
        }
        auto opt_spec = cli->find_package(spec);
        if (!opt_spec) {
            LOG(INFO) << "no package of " << nlohmann::json(spec).dump() << " found in " << url;
            continue;
        }
        return SpecWithUrl{url, *opt_spec};
    }
    LOG(WARNING) << "none of urls have package of " << nlohmann::json(spec).dump();
    return {};
}

std::vector<char> StoreManager::download_package(const PackageSpec& spec) const {
    std::lock_guard _lk(m_urls);
    for (const auto& url : store_urls) {
        auto cli = StoreClient::make(host_info, url);
        if (!cli) {
            LOG(INFO) << "make client for " << url << " failed";
            continue;
        }
        auto opt_spec = cli->find_package(spec);
        if (!opt_spec) {
            LOG(INFO) << "no package of " << nlohmann::json(spec).dump() << " found in " << url;
            continue;
        }
        auto res = cli->download(*opt_spec);
        if (res.empty()) {
            LOG(INFO) << "download from " << url << "failed";
            continue;
        }
        return res;
    }
    LOG(WARNING) << "none of urls have package of " << nlohmann::json(spec).dump();
    return {};
}

namespace {
auto make_temp_path() {
    auto id = make_uuid();
    return std::filesystem::temp_directory_path() / to_string(id);
}

expected<void, ErrorMsg> unzip(std::span<const char> data, const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        if (!std::filesystem::create_directories(path)) {
            return ::semantic_expected::unexpected("Failed to create target directory: " + path.string());
        }
    }
    mz_zip_archive zip_archive = {};
    mz_bool status = mz_zip_reader_init_mem(&zip_archive, data.data(), data.size(), 0);
    if (!status) { return ::semantic_expected::unexpected("Failed to initialize ZIP archive"); }
    // 获取 ZIP 包中文件的数量
    int file_count = mz_zip_reader_get_num_files(&zip_archive);
    // 遍历 ZIP 包中的文件和目录
    for (int i = 0; i < file_count; ++i) {
        mz_zip_archive_file_stat file_stat;
        if (!mz_zip_reader_file_stat(&zip_archive, i, &file_stat)) {
            mz_zip_reader_end(&zip_archive);
            return ::semantic_expected::unexpected{"Failed to get file information for index " + std::to_string(i)};
        }
        // 计算目标路径
        std::filesystem::path target_path = path / file_stat.m_filename;
        // 如果是目录，创建目录
        if (mz_zip_reader_is_file_a_directory(&zip_archive, i)) {
            if (!std::filesystem::exists(target_path)) {
                if (!std::filesystem::create_directories(target_path)) {
                    mz_zip_reader_end(&zip_archive);
                    return ::semantic_expected::unexpected{"Failed to create directory: " + target_path.string()};
                }
            }
            continue;
        }
        // 如果是文件，解压并写入
        size_t uncompressed_size = file_stat.m_uncomp_size;
        std::vector<char> file_data(uncompressed_size);

        if (!mz_zip_reader_extract_to_mem(&zip_archive, i, file_data.data(), file_data.size(), 0)) {
            mz_zip_reader_end(&zip_archive);
            return ::semantic_expected::unexpected{"Failed to extract file: " + std::string(file_stat.m_filename)};
        }
        // 确保父目录存在
        std::filesystem::create_directories(target_path.parent_path());
        // 写入文件到目标路径
        std::ofstream ofs(target_path, std::ios::binary);
        if (!ofs) {
            mz_zip_reader_end(&zip_archive);
            return ::semantic_expected::unexpected{"Failed to write to file: " + target_path.string()};
        }
        ofs.write(file_data.data(), file_data.size());
        ofs.close();
#ifndef _WIN32
        // 设置可执行权限
        try {
            std::filesystem::permissions(
                target_path,
                std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec
                    | std::filesystem::perms::others_exec,
                std::filesystem::perm_options::add
            );
        }
        catch (const std::exception& e) {
            mz_zip_reader_end(&zip_archive);
            return ::semantic_expected::unexpected{
                "Failed to set executable permissions for file: " + target_path.string()
            };
        }
#endif
    }
    mz_zip_reader_end(&zip_archive);
    return {};
}

std::filesystem::path find_package_base_dir(const std::filesystem::path& start) {
    using std::filesystem::recursive_directory_iterator;
    for (const auto& dir_entry : recursive_directory_iterator(start)) {
        if (!dir_entry.is_regular_file()) { continue; }
        std::string filename = dir_entry.path().filename();
        if ((filename == "package.yaml")) { return dir_entry.path().parent_path(); }
    }
    return {};
}

} // namespace

// 包内嵌 CR 镜像目录: <framework_home>/crs/_packages/<pkg>/<version>/
// 与用户手动维护的 <framework_home>/crs/*.yaml 物理隔离, uninstall 时整目录干掉。
std::filesystem::path StoreManager::mirrored_pkg_crs_dir(
    const std::string& pkg_name, const std::string& version
) {
    return global_vars::home_path() / "crs" / "_packages" / pkg_name / version;
}

// 把 <pkg_install_dir>/crs/*.yaml 拷贝到 <framework_home>/crs/_packages/<pkg>/<version>/
// 作用: 让框架下次 read_crs_into_db 扫描时能看见这些 CR (递归扫描)。
//       存在则 overwrite, 实现包升级时 CR 模板的热替换。
size_t StoreManager::mirror_package_crs(
    const std::filesystem::path& pkg_install_dir, const std::string& pkg_name,
    const std::string& version
) {
    auto src_dir = pkg_install_dir / "crs";
    if (!std::filesystem::exists(src_dir) || !std::filesystem::is_directory(src_dir)) {
        return 0;
    }
    auto dest_dir = mirrored_pkg_crs_dir(pkg_name, version);
    std::error_code ec;
    std::filesystem::create_directories(dest_dir, ec);
    if (ec) {
        LOG(WARNING) << "mirror_package_crs: create_directories " << dest_dir
                     << " failed: " << ec.message();
        return 0;
    }
    size_t count = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(src_dir)) {
        if (!entry.is_regular_file()) { continue; }
        auto rel = std::filesystem::relative(entry.path(), src_dir);
        auto target = dest_dir / rel;
        std::filesystem::create_directories(target.parent_path(), ec);
        std::filesystem::copy_file(
            entry.path(), target, std::filesystem::copy_options::overwrite_existing, ec
        );
        if (ec) {
            LOG(WARNING) << "mirror_package_crs: copy " << entry.path() << " -> " << target
                         << " failed: " << ec.message();
            ec.clear();
        } else {
            ++count;
            LOG(INFO) << "mirror cr " << entry.path() << " -> " << target;
        }
    }
    return count;
}

// uninstall 时清掉镜像。如果 version 为空, 把整个 pkg 的所有版本镜像都清掉
void StoreManager::unmirror_package_crs(const std::string& pkg_name, const std::string& version) {
    auto path = version.empty()
                    ? (global_vars::home_path() / "crs" / "_packages" / pkg_name)
                    : mirrored_pkg_crs_dir(pkg_name, version);
    std::error_code ec;
    if (std::filesystem::exists(path)) {
        auto removed = std::filesystem::remove_all(path, ec);
        if (ec) {
            LOG(WARNING) << "unmirror_package_crs: remove_all " << path
                         << " failed: " << ec.message();
        } else {
            LOG(INFO) << "unmirror_package_crs: removed " << removed << " entries under " << path;
        }
    }
}

// ===== 包内嵌 Skill 镜像 =====
// 与 CR 镜像同构, 但 skill 文件不进 DB, 也不限制扩展名 (markdown, txt, json 都行)。
// 落地位置: <framework_home>/skills/_packages/<pkg>/<version>/
std::filesystem::path StoreManager::mirrored_pkg_skills_dir(
    const std::string& pkg_name, const std::string& version
) {
    return global_vars::home_path() / "skills" / "_packages" / pkg_name / version;
}

size_t StoreManager::mirror_package_skills(
    const std::filesystem::path& pkg_install_dir, const std::string& pkg_name,
    const std::string& version
) {
    auto src_dir = pkg_install_dir / "skills";
    if (!std::filesystem::exists(src_dir) || !std::filesystem::is_directory(src_dir)) {
        return 0;
    }
    auto dest_dir = mirrored_pkg_skills_dir(pkg_name, version);
    std::error_code ec;
    std::filesystem::create_directories(dest_dir, ec);
    if (ec) {
        LOG(WARNING) << "mirror_package_skills: create_directories " << dest_dir
                     << " failed: " << ec.message();
        return 0;
    }
    size_t count = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(src_dir)) {
        if (!entry.is_regular_file()) { continue; }
        auto rel = std::filesystem::relative(entry.path(), src_dir);
        auto target = dest_dir / rel;
        std::filesystem::create_directories(target.parent_path(), ec);
        std::filesystem::copy_file(
            entry.path(), target, std::filesystem::copy_options::overwrite_existing, ec
        );
        if (ec) {
            LOG(WARNING) << "mirror_package_skills: copy " << entry.path() << " -> " << target
                         << " failed: " << ec.message();
            ec.clear();
        } else {
            ++count;
            LOG(INFO) << "mirror skill " << entry.path() << " -> " << target;
        }
    }
    return count;
}

void StoreManager::unmirror_package_skills(const std::string& pkg_name, const std::string& version) {
    auto path = version.empty()
                    ? (global_vars::home_path() / "skills" / "_packages" / pkg_name)
                    : mirrored_pkg_skills_dir(pkg_name, version);
    std::error_code ec;
    if (std::filesystem::exists(path)) {
        auto removed = std::filesystem::remove_all(path, ec);
        if (ec) {
            LOG(WARNING) << "unmirror_package_skills: remove_all " << path
                         << " failed: " << ec.message();
        } else {
            LOG(INFO) << "unmirror_package_skills: removed " << removed << " entries under " << path;
        }
    }
}

namespace {
// 从一个 markdown 文件的前几行抓 title:
//   1) YAML frontmatter ---\n...\nname: xxx 或 title: xxx
//   2) 第一个 # 一级标题
// 抓到都没就返回空字符串。
std::string extract_skill_title(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) { return ""; }
    std::string line;
    bool in_fm = false;
    int line_no = 0;
    std::string from_fm_name, from_fm_title;
    while (std::getline(in, line) && line_no < 60) {
        ++line_no;
        if (line_no == 1 && line == "---") { in_fm = true; continue; }
        if (in_fm) {
            if (line == "---") {
                in_fm = false;
                continue;
            }
            constexpr const char* keys[] = {"name:", "title:"};
            for (const char* k : keys) {
                auto klen = std::strlen(k);
                if (line.compare(0, klen, k) == 0) {
                    auto v = line.substr(klen);
                    while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(0, 1);
                    while (!v.empty() && (v.back() == ' ' || v.back() == '\r' || v.back() == '\t'))
                        v.pop_back();
                    if (!v.empty() && v.front() == '"' && v.back() == '"' && v.size() >= 2) {
                        v = v.substr(1, v.size() - 2);
                    }
                    if (k == std::string("name:")) from_fm_name = v;
                    else from_fm_title = v;
                }
            }
            continue;
        }
        if (line.compare(0, 2, "# ") == 0) {
            auto t = line.substr(2);
            while (!t.empty() && (t.back() == ' ' || t.back() == '\r')) t.pop_back();
            return t;
        }
    }
    if (!from_fm_title.empty()) return from_fm_title;
    if (!from_fm_name.empty()) return from_fm_name;
    return "";
}
} // namespace

std::vector<StoreManager::SkillEntry> StoreManager::list_all_skills() {
    std::vector<SkillEntry> out;
    auto root = global_vars::home_path() / "skills" / "_packages";
    if (!std::filesystem::exists(root)) { return out; }
    // 遍历两层 dir = pkg/version, 然后 recursive 抓文件
    std::error_code ec;
    for (const auto& pkg_entry : std::filesystem::directory_iterator(root, ec)) {
        if (!pkg_entry.is_directory()) { continue; }
        auto pkg_name = pkg_entry.path().filename().string();
        for (const auto& ver_entry : std::filesystem::directory_iterator(pkg_entry.path(), ec)) {
            if (!ver_entry.is_directory()) { continue; }
            auto version = ver_entry.path().filename().string();
            for (const auto& f : std::filesystem::recursive_directory_iterator(ver_entry.path(), ec)) {
                if (!f.is_regular_file()) { continue; }
                SkillEntry e;
                e.package = pkg_name;
                e.version = version;
                e.filename = std::filesystem::relative(f.path(), ver_entry.path()).string();
                std::error_code sec;
                e.size_bytes = std::filesystem::file_size(f.path(), sec);
                e.title = extract_skill_title(f.path());
                out.push_back(std::move(e));
            }
        }
    }
    return out;
}

std::optional<std::string> StoreManager::read_skill(
    const std::string& pkg_name, const std::string& version, const std::string& filename
) {
    if (pkg_name.empty() || version.empty() || filename.empty()) { return std::nullopt; }
    auto base = mirrored_pkg_skills_dir(pkg_name, version);
    auto target = std::filesystem::weakly_canonical(base / filename);
    auto base_canon = std::filesystem::weakly_canonical(base);
    // path traversal guard: target 必须落在 base 内
    auto rel = std::filesystem::relative(target, base_canon);
    if (rel.empty() || rel.native().compare(0, 2, "..") == 0) { return std::nullopt; }
    if (!std::filesystem::exists(target) || !std::filesystem::is_regular_file(target)) {
        return std::nullopt;
    }
    std::ifstream in(target, std::ios::binary);
    if (!in) { return std::nullopt; }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

expected<void, ErrorMsg> StoreManager::extract_package(
    const PackageSpec& spec, std::span<const char> pkg_data
) {
    scope_fail _report([&spec]() { LOG(WARNING) << "when extracting package" << spec.package; });
    if (!spec.is_complete()) { throw std::invalid_argument("spec is not complete"); }
    auto destinated_path = global_vars::packages_path() / spec.package / spec.version;
    if (exists(destinated_path)) {
        return ::semantic_expected::unexpected{"already exists path " + destinated_path.string()};
    }
    create_directories(destinated_path.parent_path());
    auto tmp_path = make_temp_path();
    // 先解压到临时目录
    if (auto res = unzip(pkg_data, tmp_path); !res) { return res; };
    auto pkg_base_path = find_package_base_dir(tmp_path);
    if (pkg_base_path.empty()) {
        return ::semantic_expected::unexpected{"no \"package.yaml\" found at package content"};
    }

    // 将包从临时目录复制到最终目录
    rename(pkg_base_path, destinated_path);
    // 包内嵌 CR 镜像到 framework 的 crs 目录, 由下一次 read_crs_into_db reconcile 生效
    StoreManager::mirror_package_crs(destinated_path, spec.package, spec.version);
    // 包内嵌 skill 镜像到 framework 的 skills 目录, 供 webui / mcp server 拉取
    StoreManager::mirror_package_skills(destinated_path, spec.package, spec.version);
    return {};
}

namespace {
expected<YAML::Node, ErrorMsg> read_yaml_from_file(std::filesystem::path path) noexcept {
    try {
        auto path_str = path.string();
        return YAML::LoadFile(path_str);
    }
    catch (std::exception& e) {
        return make_unexpected("error reading yaml from ", path, " : ", e.what());
    }
}

expected<PackageSpec, ErrorMsg> read_package_spec_from_yaml(const YAML::Node& y) {
    PackageSpec res;
    if (y["name"].IsNull()) { return make_unexpected("needs /", "name"); }
    res.package = y["name"].as<std::string>();
    if (y["version"].IsNull()) { return make_unexpected("needs /", "version"); }
    if (y["arch"].IsNull()) { return make_unexpected("needs /", "arch"); }
    res.version = y["version"].as<std::string>();
    res.arch = y["arch"].as<std::string>();
    return res;
}

std::filesystem::path find_package_yaml_in_deeper(const Path& p) {
    if (!is_directory(p)) { return {}; }
    for (const auto& e : std::filesystem::directory_iterator(p)) {
        if (!e.is_directory()) { continue; }
        if (exists(e.path() / "package.yaml")) { return e.path(); }
    }
    return {};
}

}; // namespace
auto StoreManager::add_package(
    std::string_view file_type, std::span<const char> pkg_data, bool force
) -> expected<AddPackageRes, ErrorMsg> {
    if (file_type != "zip") { throw std::invalid_argument("currently only zip is supported"); }

    // 先解压到临时目录
    auto tmp_path = make_temp_path();
    if (auto res = unzip(pkg_data, tmp_path); !res) { return res.error(); };

    auto package_yaml_path = tmp_path / "package.yaml";
    if (!exists(package_yaml_path)) {
        // 没有发现 tmp_path/package.yaml
        // 检查一下, 是不是解压成了 tmp_path/xxxx/package.yaml,
        // 如果是,那么 tmp_path/xxxx 才应该是真正的包路径
        auto deep_path = find_package_yaml_in_deeper(tmp_path);
        if (deep_path.empty()) { return ::semantic_expected::unexpected{"need /package.yaml in package"}; }
        tmp_path = deep_path;
        package_yaml_path = tmp_path / "package.yaml";
    }

    auto spec = read_yaml_from_file(package_yaml_path).and_then(read_package_spec_from_yaml);

    if (!spec) { return spec.error(); }
    if (spec->arch != host_info.arch) {
        return ::semantic_expected::unexpected(strjoin(
            "unsupported architecture, host expected ", host_info.arch, ", but package has ",
            spec->arch
        ));
    }
    auto dest_path = global_vars::packages_path() / spec->package / spec->version;

    std::lock_guard _lk(m_dirs);
    bool package_already_exists = exists(dest_path) && exists(dest_path / "package.yaml");
    // 如果包已经存在了
    if (package_already_exists && (!force)) {
        return AddPackageRes{.actually_installed = false, .spec = *spec};
    }
    // 如果force,就先删除原有的包,再替换为新的
    create_directories(dest_path.parent_path());
    remove_all(dest_path);
    rename(tmp_path, dest_path);
    // 把包内嵌的 CR 镜像到 framework 的 crs 目录, 让 reconcile 自动加载
    StoreManager::mirror_package_crs(dest_path, spec->package, spec->version);
    // 把包内嵌的 skill 镜像到 framework 的 skills 目录
    StoreManager::mirror_package_skills(dest_path, spec->package, spec->version);

    return AddPackageRes{.actually_installed = true, .spec = *spec};
}

// 每项是一个能力id
std::vector<std::string> abilities_using_this_package(const PackageSpec& spec) {
    // TODO
    return {};
}

auto StoreManager::remove_package(const PackageSpec& spec) -> expected<RemovePackageRes, ErrorMsg> {
    std::lock_guard _lk(m_dirs);
    auto dest_path = global_vars::packages_path() / spec.package / spec.version;
    bool package_already_exists = exists(dest_path) && exists(dest_path / "package.yaml");
    if (!package_already_exists) { return RemovePackageRes{.result = "not-exist"}; }

    if (auto abilities = abilities_using_this_package(spec); !abilities.empty()) {
        return ::semantic_expected::unexpected{"there is still abilities using it: " + intercalate(", ", abilities)};
    }
    // ok, 删除目录
    remove_all(dest_path);
    // 同步移除 framework 镜像的 CR; 下一次 reconcile 会清掉对应的 AbilityCRBasic 行
    StoreManager::unmirror_package_crs(spec.package, spec.version);
    // 同步移除 framework 镜像的 skill 文档
    StoreManager::unmirror_package_skills(spec.package, spec.version);

    return RemovePackageRes{.result = "success"};
}
