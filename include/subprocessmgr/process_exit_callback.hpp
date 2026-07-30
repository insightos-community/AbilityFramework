// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <functional>
#include <uvw/process.h>
using ProcessExitCallback = std::function<void(const uvw::exit_event&)>;
