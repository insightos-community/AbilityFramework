// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once
/// a common header library so common operations are not scattered across util headers

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
// construct with strjoin exception
template <typename T = std::runtime_error, OstreamPrintable... Args>
inline T make_error(Args&&... args) {
    std::ostringstream oss;
    ((oss << args), ...);
    return T(oss.str());
}

template <OstreamPrintable... Args>
inline unexpected<ErrorMsg> make_unexpected(Args&&... args) {
    std::ostringstream oss;
    ((oss << args), ...);
    return unexpected(oss.str());
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

// for unimplementedfunctionplaceholder for
#define UnImplemented(Name)                                                \
    {                                                                      \
        LOG(ERROR) << Name << " is not implemented";                       \
        throw std::logic_error(Name + std::string(" is not implemented")); \
    }

// for unimplementedfunctionplaceholder for
#define UnImplementedWarning(Name) \
    { LOG(WARNING) << Name << " is not implemented"; }
template <typename T>
T from_sv(std::string_view) = delete;

#define FWK_DECLARE_FROM_SV(TheType) \
    template <>                      \
    TheType from_sv<TheType>(std::string_view);
