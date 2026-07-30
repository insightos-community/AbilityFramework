// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#include "ftp_client.hpp"

#include "prelude.hpp"
#include "util/generator.hpp"

#include <curl/curl.h>

#include <glog/logging.h>
#include <sstream>
namespace {

inline generator<std::string_view> seperated(std::string_view sv, char sep) {
    if (sv.empty()) { co_return; }
    const auto* from = sv.begin();
    const auto* until = sv.begin();
    for (; until != sv.end(); ++until) {
        if (*until == sep) {
            if (*from != sep) { co_yield std::string_view(from, until); }
            else if (until - from > 1) { co_yield std::string_view(from + 1, until); }
            from = until;
        }
    }
    if (until - from > 1) { co_yield std::string_view(from + 1, until); }
}
inline std::string path_join(std::string_view base, std::string_view deriv) {
    std::ostringstream oss;
    if (!base.ends_with('/')) {
        oss << base << '/' << deriv;
        return oss.str();
    }
    oss << std::string{base} << deriv;
    return oss.str();
}
std::string inspect(std::string_view input) {
    std::ostringstream oss;
    oss << '"';
    for (char c : input) {

        // Check for special characters and output their representations
        switch (c) {
        case '\n': oss << "\\n"; break;
        case '\r': oss << "\\r"; break;
        case '\t': oss << "\\t"; break;
        case '\\': oss << "\\\\"; break;
        case '\"': oss << "\\\""; break;
        default:
            // If it's a printable character, just output it
            if (isprint(c)) { oss << c; }
            else {
                // If it's a non-printable character, output its ASCII value
                oss << "[0x" << std::hex << static_cast<int>(c) << "]";
            }
            break;
        }
    }
    oss << '"';
    return oss.str();
}

int debug_func(CURL* c, curl_infotype itype, char* ptr, size_t size, void* userptr) {
    LOG(INFO) << "curl debug: " << std::string_view{ptr, size};
    return CURLE_OK;
}

} // namespace

#define CHECK_CURL_RESULT(Rescode, msgopt)                                         \
    if (err != CURLE_OK) {                                                         \
        auto msg = strjoin("curl ", msgopt, " failed: ", curl_easy_strerror(err)); \
        LOG(ERROR) << msg;                                                         \
        return msg;                                                                \
    }

expected<std::vector<std::string>, ErrorMsg> FtpClient::list_files(std::string_view dir) const {
    using namespace std::string_literals;
    auto curl = curl_easy_init();
    if (!curl) { return {"curl init failed"}; }

    auto ftp_url = path_join(base_url, dir);
    if (!ftp_url.ends_with('/')) { ftp_url += '/'; }

    // Set FTP URL
    auto err = curl_easy_setopt(curl, CURLOPT_URL, ftp_url.c_str());
    CHECK_CURL_RESULT(err, "set url");

    err = curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    CHECK_CURL_RESULT(err, "set verbose");

    err = curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, debug_func);
    CHECK_CURL_RESULT(err, "set debugfunction");

    if (!password.empty()) {
        // Set username and password
        std::string usrpwd = username + ":" + password;
        err = curl_easy_setopt(curl, CURLOPT_USERPWD, usrpwd.c_str());
        CHECK_CURL_RESULT(err, "set user and password");
    }

    err = curl_easy_setopt(curl, CURLOPT_FTPLISTONLY, 1L);
    CHECK_CURL_RESULT(err, "set ftp listonly");

    std::vector<std::string> res;
    auto write_function = [](char* ptr, size_t size, size_t nmemb, void* userdata) {
        auto* out = static_cast<std::vector<std::string>*>(userdata);
        CHECK_NOTNULL(out);
        std::string_view data{ptr, size * nmemb};
        LOG(INFO) << "receive: " << inspect(data);
        for (auto filename : seperated(data, '\n')) {
            if (filename.empty()) { continue; }
            out->emplace_back(filename);
        }
        return size * nmemb;
    };

    err = curl_easy_setopt(
        curl, CURLOPT_WRITEFUNCTION, static_cast<curl_write_callback>(write_function)
    );
    CHECK_CURL_RESULT(err, "set writefunction");
    err = curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res);
    CHECK_CURL_RESULT(err, "set writedata");

    // Perform the request
    err = curl_easy_perform(curl);
    CHECK_CURL_RESULT(err, "perform");

    return res;
}

expected<std::vector<char>, ErrorMsg> FtpClient::download_file(std::string_view file_path) const {
    auto curl = curl_easy_init();
    if (!curl) { return {"curl init failed"}; }
    auto ftp_url = path_join(base_url, file_path);

    // Set FTP URL
    auto err = curl_easy_setopt(curl, CURLOPT_URL, ftp_url.c_str());
    CHECK_CURL_RESULT(err, "set url");
    if (!password.empty()) {
        // Set username and password
        std::string usrpwd = username + ":" + password;
        err = curl_easy_setopt(curl, CURLOPT_USERPWD, usrpwd.c_str());
        CHECK_CURL_RESULT(err, "set user and password");
    }
    std::vector<char> res;
    auto write_function = [](char* ptr, size_t size, size_t nmemb, void* userdata) {
        auto* out = static_cast<std::vector<char>*>(userdata);
        CHECK_NOTNULL(out);
        std::string_view output_sv(ptr, size * nmemb);
        std::copy(output_sv.begin(), output_sv.end(), std::back_inserter(*out));

        return size * nmemb;
    };

    err = curl_easy_setopt(
        curl, CURLOPT_WRITEFUNCTION, static_cast<curl_write_callback>(write_function)
    );
    CHECK_CURL_RESULT(err, "set writefunction");
    err = curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res);
    CHECK_CURL_RESULT(err, "set writedata");
    // Perform the request
    err = curl_easy_perform(curl);
    CHECK_CURL_RESULT(err, "perform");
    return res;
}
