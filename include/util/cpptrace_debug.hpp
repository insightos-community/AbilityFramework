// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#ifdef AFWK_USE_CPPTRACE
#include <cpptrace/cpptrace.hpp>
#include <cpptrace/from_current.hpp>

#define FWK_DUMP_STACKTRACE_TO_LOG(Log_Level)                        \
    {                                                                \
        auto except_trace = cpptrace::from_current_exception();      \
        auto current_trace = cpptrace::generate_trace();             \
        CHECK(current_trace.frames.size() >= 2);                     \
        auto& super_frame = current_trace.frames[1];                 \
        for (auto& f : except_trace.frames) {                        \
            if (f.raw_address == super_frame.raw_address) { break; } \
            LOG(Log_Level) << f;                                     \
        }                                                            \
    }
#else
#define CPPTRACE_TRY try
#define CPPTRACE_CATCH catch

#define FWK_DUMP_STACKTRACE_TO_LOG(Log_Level)

#endif
