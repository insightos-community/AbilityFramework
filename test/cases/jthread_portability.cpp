// Copyright 2026 InsightOS
// SPDX-License-Identifier: Apache-2.0
#include "util/jthread.hpp"
#include "util/jthread.hpp"
#include "doctest.h"

TEST_CASE("jthread requests stop and joins on destruction") {
    std::atomic<bool> stopped = false;
    {
        jthread worker([&](stop_token token) {
            while (!token.stop_requested()) { std::this_thread::yield(); }
            stopped = true;
        });
    }
    CHECK(stopped.load());
}
