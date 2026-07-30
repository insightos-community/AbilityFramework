// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <nlohmann/detail/macro_scope.hpp>

#define FWK_ENUM_FIELD(Name)   \
    case FWK_TheType ::Name: { \
        o << #Name;            \
        return o;              \
    } break;

#define FWK_DEFINE_ENUM_OSTREAM_OUTPUT(Type, ...)                                  \
    inline std::ostream& operator<<(std::ostream& o, Type x) {                     \
        using FWK_TheType = Type;                                                  \
        switch (x) {                                                               \
            NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(FWK_ENUM_FIELD, __VA_ARGS__)) \
        default: throw std::invalid_argument("invalid " #Type);                    \
        }                                                                          \
    }
