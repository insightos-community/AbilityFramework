// Copyright 2026 InsightOS
// SPDX-License-Identifier: Apache-2.0
#include "util/scope.hpp"
#include "doctest.h"
#include <stdexcept>

TEST_CASE("Scope cleanup survives move and release") {
    int calls = 0;
    {
        scope_exit original([&]() { ++calls; });
        auto moved = std::move(original);
    }
    CHECK(calls == 1);
    { scope_exit released([&]() { ++calls; }); released.release(); }
    CHECK(calls == 1);
}

TEST_CASE("Scope guards distinguish successful and exceptional exits") {
    int failures = 0, successes = 0;
    try {
        scope_fail failed([&]() { ++failures; });
        scope_success success([&]() { ++successes; });
        throw std::runtime_error("test failure");
    } catch (const std::runtime_error&) {}
    CHECK(failures == 1);
    CHECK(successes == 0);
    { scope_success success([&]() { ++successes; }); }
    CHECK(successes == 1);
}
