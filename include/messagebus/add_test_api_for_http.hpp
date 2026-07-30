// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "messagebus.hpp"

#include <httplib.h>

namespace message_bus {

void add_test_api(httplib::Server& server);
} // namespace message_bus
