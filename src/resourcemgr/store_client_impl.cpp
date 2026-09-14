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

#include "ftp_client.hpp"
#include "resourcemgr/store_client.hpp"
#include "util/ada_url.hpp"
#include <glog/logging.h>
#include <httplib.h>
#ifdef _WIN32
#include <uv.h>
#endif
#ifdef __APPLE__
#include <sys/sysctl.h>
#include <sys/utsname.h>
#endif

namespace {
/// @brief 目前的实现方式是要求版本完全匹配,尚未有wildcard机制
/// @return 该文件是否为对应版本
bool version_match(std::string_view filename, std::string_view version) {
    return filename.find(version) != std::string_view::npos;
}
std::string make_package_full_name(const HostInfo& host, const PackageSpec& pkg) {
    return strjoin(
        pkg.package, '-', pkg.version, '-', host.arch, '-', host.os, '-', host.os_version, ".zip"
    );
}
// FTP 客户端实现
class ClientImplFtp : public StoreClient {
    HostInfo host_info;
    FtpClient cli;

public:
    std::string url() const noexcept override { return cli.base_url; };
    std::optional<PackageSpec> find_package(const PackageSpec& spec) const override {
        auto dir = strjoin(host_info.arch, '/', host_info.os, '/', spec.package);
        auto filenames = cli.list_files(dir);
        if (!filenames) {
            LOG(ERROR) << "find package from " << url() << " failed: " << filenames.error();
            return {};
        }
        for (const auto& filename : *filenames) {
            if (filename.ends_with(".zip") && version_match(filename, spec.version)) {
                return spec;
            }
        }

        return {};
    }
    std::vector<char> download(const PackageSpec& spec) const override {

        auto path = strjoin(
            host_info.arch, '/', host_info.os, '/', spec.package, '/', spec.version, ".zip"
        );

        auto data = cli.download_file(path);
        if (!data) {
            LOG(ERROR) << "download from " << url() << " failed: " << data.error();
            return {};
        }
        return std::move(data).value();
        ;
    }

    static std::unique_ptr<ClientImplFtp> make(
        const HostInfo& host_info, const ada::url_aggregator& url
    ) {
        auto ftp_cli = std::make_unique<ClientImplFtp>();
        ftp_cli->host_info = host_info;
        ftp_cli->cli.base_url = strjoin("ftp://", url.get_host());
        if (!url.get_username().empty()) { ftp_cli->cli.username = url.get_username(); }
        if (!url.get_password().empty()) { ftp_cli->cli.username = url.get_password(); }
        return ftp_cli;
    }
};

// HTTP 客户端实现
class ClientImplHttp : public StoreClient {
    HostInfo host_info;
    std::string base_url;

public:
    std::string url() const noexcept override { return base_url; };

    // HTTP方式：直接尝试下载，如果返回404说明不存在
    // 如 testability.company.com-0.1.0-x86_64.zip,
    // 它的详细地址为
    // /packages/testability.company.com-0.1.0-x86_64.zip
    std::optional<PackageSpec> find_package(const PackageSpec& spec) const override {
        auto package_name = make_package_full_name(host_info, spec);
        auto package_path = "/packages/" + package_name;

        // LOG(INFO) << "http find package: " << package_name << " from " << url() << " path: " <<
        // package_path;
        httplib::Client cli(base_url);
        auto res = cli.Head(package_path);
        if (res && res->status == 200) {
            LOG(INFO) << "find package: " << package_name << " from " << url()
                      << " path: " << package_path << " success";
            return spec;
        }
        LOG(ERROR) << "find package: " << package_name << " from " << url()
                   << " path: " << package_path << " failed";
        return {};
    }

    std::vector<char> download(const PackageSpec& spec) const override {
        auto package_name = make_package_full_name(host_info, spec);
        auto package_path = "/packages/" + package_name;

        // LOG(INFO) << "http download package: " << package_name << " from " << url() << " path: "
        // << package_path;
        httplib::Client cli(base_url);
        auto res = cli.Get(package_path);
        if (!res) {
            LOG(ERROR) << "http download package: " << package_name << " failed from " << url()
                       << " path: " << package_path << ", reason: " << res.error();
            return {};
        }
        if (res->status != 200) {
            LOG(ERROR) << "http download package: " << package_name << " failed from " << url()
                       << " path: " << package_path << ", status: " << res->status
                       << ", info: " << res->body;
            return {};
        }
        // 下载成功，返回数据
        std::vector<char> data(res->body.begin(), res->body.end());
        LOG(INFO) << "http download package: " << package_name << " success from " << url()
                  << " path: " << package_path;
        return data;
    }

    static std::unique_ptr<ClientImplHttp> make(
        const HostInfo& host_info, const ada::url_aggregator& url
    ) {
        auto http_cli = std::make_unique<ClientImplHttp>();
        http_cli->host_info = host_info;
        http_cli->base_url = strjoin("http://", url.get_host());
        return http_cli;
    }
};

} // namespace

std::unique_ptr<StoreClient> StoreClient::make(
    const HostInfo& host_info, std::string_view base_url
) {
    auto url = ada::parse(base_url);
    if (!url) {
        LOG(ERROR) << "parse url " << base_url << " failed";
        return nullptr;
    }
    if (url->get_protocol() == "ftp:") { return ClientImplFtp::make(host_info, *url); }
    if (url->get_protocol() == "http:") { return ClientImplHttp::make(host_info, *url); }
    LOG(ERROR) << "StoreClient::make invalid protocol_type: " << url->get_protocol();
    return nullptr;
}

namespace {
std::string read_file(std::filesystem::path filepath) {
    std::ifstream infile(filepath);
    return std::string((std::istreambuf_iterator<char>(infile)), std::istreambuf_iterator<char>());
}

std::pair<std::string, std::string> parse_os_release_line_to_kv(const std::string& line) {
    size_t pos = line.find('=');
    if (pos == std::string::npos) { return std::make_pair("", ""); }
    // remove leading and trailing spaces
    std::string key = line.substr(0, pos);
    key.erase(0, std::min(key.find_first_not_of(' '), key.size() - 1)); // leading space
    key.erase(
        std::max(key.find_last_not_of(' ') + 1, (size_t)0), std::string::npos
    ); // trailing space
    // remove quotations if exists
    std::string value = line.substr(pos + 1);
    if ((value[0] == '\"') && (value[value.length() - 1] == '\"')) {
        value = value.substr(1, value.length() - 2); // remove quotations from string
    }
    return make_pair(key, value);
}

std::unordered_map<std::string, std::string> parse_os_release(const std::string& filedata) {
    std::istringstream file(filedata);
    std::unordered_map<std::string, std::string> m;
    for (std::string line; getline(file, line);) {
        std::pair<std::string, std::string> kv = parse_os_release_line_to_kv(line);
        if (!kv.first.empty()) { m[std::move(kv.first)] = std::move(kv.second); }
    }
    return m;
}
std::string trim(const std::string& s) {
    if (s.empty()) { return s; }

    size_t start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) { return ""; }
    size_t end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

std::unordered_map<std::string, std::string> read_os_release_from_file(
    std::filesystem::path filepath
) {
    const std::string filedata = read_file(filepath);
    return parse_os_release(filedata);
}

std::string get_system_architecture() {
    const char command[] = "uname -m";
    FILE* pipe = popen(command, "r");
    if (!pipe) {
        int e = errno;
        auto msg = strjoin(__func__, " failed: popen: ", strerror(e));
        LOG(ERROR) << msg;
        throw std::runtime_error(msg);
    }

    char buffer[128];
    std::string result = "";
    while (!feof(pipe)) {
        auto* p = fgets(buffer, 128, pipe);
        if (!p) { break; }
        result += buffer;
    }

    pclose(pipe);
    return trim(result);
}

constexpr char DEFAULT_OS_RELEASE_PATH[] = "/etc/os-release";

} // namespace

HostInfo HostInfo::read_from_system() {
#ifdef _WIN32
    uv_utsname_t host{};
    if(uv_os_uname(&host)!=0) throw std::runtime_error("Cannot read Windows host information");
    std::string architecture=host.machine;
    if(architecture=="AMD64"||architecture=="x64") architecture="x86_64";
    return HostInfo{.os="windows",.os_version=host.release,.arch=architecture};
#elif defined(__APPLE__)
    struct utsname host = {};
    char version[256] = {};
    size_t length = sizeof(version);
    if (uname(&host) != 0 ||
        sysctlbyname("kern.osproductversion", version, &length, nullptr, 0) != 0) {
        throw std::runtime_error(strjoin("Cannot read macOS host information: ", strerror(errno)));
    }
    return HostInfo{.os = "macos", .os_version = version, .arch = host.machine};
#else
    std::unordered_map<std::string, std::string> os_release;
    try {
        os_release = read_os_release_from_file(DEFAULT_OS_RELEASE_PATH);
    }
    catch (std::exception& e) {
        LOG(ERROR) << "read_os_release_from_file failed: " << e.what();
        throw;
    }
    std::string arch = get_system_architecture();
    LOG(INFO) << "read os architecture: " << arch;
    return HostInfo{
        .os = os_release.at("ID"),
        .os_version = os_release.at("VERSION_ID"),
        .arch = std::move(arch)
    };
#endif
}
