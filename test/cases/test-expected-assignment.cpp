// Copyright 2026 InsightOS
// SPDX-License-Identifier: Apache-2.0
#include "doctest.h"
#include "util/expected.hpp"
#include <future>
#include <string>

TEST_CASE("expected assignment preserves value and error alternatives") {
    using Result = ::semantic_expected::expected<std::string, std::string>;
    Result result{std::string("value")};
    CHECK(&(result = std::string("updated")) == &result);
    CHECK(result.value() == "updated");
    const auto failure = ::semantic_expected::unexpected{std::string("failure")};
    CHECK(&(result = failure) == &result);
    CHECK_FALSE(result.has_value());
    CHECK(result.error() == "failure");
    Result copied{std::string("initial")};
    copied = result;
    CHECK(copied.error() == "failure");
    copied = Result{std::string("recovered")};
    CHECK(copied.value() == "recovered");
}

TEST_CASE("expected supports native std promise and future storage") {
    using Result = ::semantic_expected::expected<int, std::string>;
    std::promise<Result> promise;
    auto future = promise.get_future();
    promise.set_value(Result{42});
    CHECK(future.get().value() == 42);
    std::promise<::semantic_expected::expected<void, std::string>> empty;
    auto completed = empty.get_future();
    empty.set_value({});
    CHECK(completed.get().has_value());
}
