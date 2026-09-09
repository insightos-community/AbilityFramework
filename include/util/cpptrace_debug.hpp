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
