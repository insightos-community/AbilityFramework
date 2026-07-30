// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

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
} // url of the repository
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
            return unexpected("Failed to create target directory: " + path.string());
        }
    }
    mz_zip_archive zip_archive = {};
    mz_bool status = mz_zip_reader_init_mem(&zip_archive, data.data(), data.size(), 0);
    if (!status) { return unexpected("Failed to initialize ZIP archive"); }
    // get the number of files in the ZIP package
    int file_count = mz_zip_reader_get_num_files(&zip_archive);
    // iterate files and directories in the ZIP package
    for (int i = 0; i < file_count; ++i) {
        mz_zip_archive_file_stat file_stat;
        if (!mz_zip_reader_file_stat(&zip_archive, i, &file_stat)) {
            mz_zip_reader_end(&zip_archive);
            return unexpected{"Failed to get file information for index " + std::to_string(i)};
        }
        // compute target path
        std::filesystem::path target_path = path / file_stat.m_filename;
        // if it is a directory, create the directory
        if (mz_zip_reader_is_file_a_directory(&zip_archive, i)) {
            if (!std::filesystem::exists(target_path)) {
                if (!std::filesystem::create_directories(target_path)) {
                    mz_zip_reader_end(&zip_archive);
                    return unexpected{"Failed to create directory: " + target_path.string()};
                }
            }
            continue;
        }
        // if it is a file, extract and write
        size_t uncompressed_size = file_stat.m_uncomp_size;
        std::vector<char> file_data(uncompressed_size);

        if (!mz_zip_reader_extract_to_mem(&zip_archive, i, file_data.data(), file_data.size(), 0)) {
            mz_zip_reader_end(&zip_archive);
            return unexpected{"Failed to extract file: " + std::string(file_stat.m_filename)};
        }
        // ensure parent directory exists
        std::filesystem::create_directories(target_path.parent_path());
        // write file to target path
        std::ofstream ofs(target_path, std::ios::binary);
        if (!ofs) {
            mz_zip_reader_end(&zip_archive);
            return unexpected{"Failed to write to file: " + target_path.string()};
        }
        ofs.write(file_data.data(), file_data.size());
        ofs.close();
        // set executable permission
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
            return unexpected{
                "Failed to set executable permissions for file: " + target_path.string()
            };
        }
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

// package-embedded CR mirror directory: <framework_home>/crs/_packages/<pkg>/<version>/
// physically isolated from user-maintained <framework_home>/crs/*.yaml; the entire directory is removed on uninstall.
std::filesystem::path StoreManager::mirrored_pkg_crs_dir(
    const std::string& pkg_name, const std::string& version
) {
    return global_vars::home_path() / "crs" / "_packages" / pkg_name / version;
}

// take <pkg_install_dir>/crs/*.yaml copy to <framework_home>/crs/_packages/<pkg>/<version>/
// purpose: lets the framework see these CRs on the next read_crs_into_db scan (recursive scan).
// overwrite if exists, enabling hot-replacement of CR templates on package upgrade.
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

// clear mirrors on uninstall. If version is empty, clear all version mirrors of the pkg
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

// ===== package-embedded Skill mirror =====
// and CR isomorphic to mirror, but skill filedoes not enter DB, does not limit extensions either (markdown, txt, json all work).
// landing location: <framework_home>/skills/_packages/<pkg>/<version>/
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
// froma markdown grab the first few lines of the file title:
// 1) YAML frontmatter ---\n...\nname: xxx or title: xxx
// 2) no.a # first leveltitle
// if nothing found return empty string.
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
    // iterate two levels dir = pkg/version, then recursively grab files
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
    // path traversal guard: target must fall within base
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
        return unexpected{"already exists path " + destinated_path.string()};
    }
    create_directories(destinated_path.parent_path());
    auto tmp_path = make_temp_path();
    // extract to a temp directory first
    if (auto res = unzip(pkg_data, tmp_path); !res) { return res; };
    auto pkg_base_path = find_package_base_dir(tmp_path);
    if (pkg_base_path.empty()) {
        return unexpected{"no \"package.yaml\" found at package content"};
    }

    // copy the package from temp dir to final dir
    rename(pkg_base_path, destinated_path);
    // mirror package-embedded CRs to framework's crs directory, effective on next read_crs_into_db reconcile
    StoreManager::mirror_package_crs(destinated_path, spec.package, spec.version);
    // mirror package-embedded skills to framework's skills directory, for webui / mcp server to fetch
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

    // extract to a temp directory first
    auto tmp_path = make_temp_path();
    if (auto res = unzip(pkg_data, tmp_path); !res) { return res.error(); };

    auto package_yaml_path = tmp_path / "package.yaml";
    if (!exists(package_yaml_path)) {
        // tmp_path/package.yaml not found
        // check whether it was extracted to tmp_path/xxxx/package.yaml,
        // if so, then tmp_path/xxxx should be the real package path
        auto deep_path = find_package_yaml_in_deeper(tmp_path);
        if (deep_path.empty()) { return unexpected{"need /package.yaml in package"}; }
        tmp_path = deep_path;
        package_yaml_path = tmp_path / "package.yaml";
    }

    auto spec = read_yaml_from_file(package_yaml_path).and_then(read_package_spec_from_yaml);

    if (!spec) { return spec.error(); }
    if (spec->arch != host_info.arch) {
        return unexpected(strjoin(
            "unsupported architecture, host expected ", host_info.arch, ", but package has ",
            spec->arch
        ));
    }
    auto dest_path = global_vars::packages_path() / spec->package / spec->version;

    std::lock_guard _lk(m_dirs);
    bool package_already_exists = exists(dest_path) && exists(dest_path / "package.yaml");
    // if the package already exists
    if (package_already_exists && (!force)) {
        return AddPackageRes{.actually_installed = false, .spec = *spec};
    }
    // if force, delete the existing package first, then replace with the new one
    create_directories(dest_path.parent_path());
    remove_all(dest_path);
    rename(tmp_path, dest_path);
    // mirror the package-embedded CRs to the framework's crs directory so reconcile auto-loads them
    StoreManager::mirror_package_crs(dest_path, spec->package, spec->version);
    // mirror the package-embedded skills to the framework's skills directory
    StoreManager::mirror_package_skills(dest_path, spec->package, spec->version);

    return AddPackageRes{.actually_installed = true, .spec = *spec};
}

// each item is an ability id
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
        return unexpected{"there is still abilities using it: " + intercalate(", ", abilities)};
    }
    // ok, delete directory
    remove_all(dest_path);
    // also remove framework-mirrored CRs; next reconcile clears the corresponding AbilityCRBasic rows
    StoreManager::unmirror_package_crs(spec.package, spec.version);
    // also remove framework-mirrored skill docs
    StoreManager::unmirror_package_skills(spec.package, spec.version);

    return RemovePackageRes{.result = "success"};
}
