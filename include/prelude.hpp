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
/// 一个通用头文件库,以免一些常见操作分散到不同的util头中

#include "util/expected.hpp"
#include <sstream>
#include <string>

using ErrorMsg = std::string;

template <typename T>
concept OstreamPrintable = requires(T t, std::ostream& o) { o << t; };

template <OstreamPrintable... Args>
inline std::string strjoin(Args&&... args) {
    std::ostringstream oss;
    ((oss << args), ...);
    return oss.str();
}
// 以strjoin构造异常
template <typename T = std::runtime_error, OstreamPrintable... Args>
inline T make_error(Args&&... args) {
    std::ostringstream oss;
    ((oss << args), ...);
    return T(oss.str());
}

template <OstreamPrintable... Args>
inline ::semantic_expected::unexpected<ErrorMsg> make_unexpected(Args&&... args) {
    std::ostringstream oss;
    ((oss << args), ...);
    return ::semantic_expected::unexpected<ErrorMsg>{oss.str()};
}

namespace std {

template <::OstreamPrintable T1, ::OstreamPrintable T2>
inline std::ostream& operator<<(std::ostream& os, const std::pair<T1, T2>& p) {
    os << "(" << p.first << ": " << p.second << ")";
    return os;
}
} // namespace std

template <std::ranges::input_range R>
    requires OstreamPrintable<std::ranges::range_value_t<R>>
std::string intercalate(OstreamPrintable auto&& mid, R&& r) {
    std::ostringstream oss;
    bool first = true;
    for (auto&& x : r) {
        oss << x;
        if (first) [[unlikely]] { first = false; }
        else { oss << mid; }
    }
    return oss.str();
}

// 为未实现的函数留的空
#define UnImplemented(Name)                                                \
    {                                                                      \
        LOG(ERROR) << Name << " is not implemented";                       \
        throw std::logic_error(Name + std::string(" is not implemented")); \
    }

// 为未实现的函数留的空
#define UnImplementedWarning(Name) \
    { LOG(WARNING) << Name << " is not implemented"; }
template <typename T>
T from_sv(std::string_view) = delete;

#define FWK_DECLARE_FROM_SV(TheType) \
    template <>                      \
    TheType from_sv<TheType>(std::string_view);
