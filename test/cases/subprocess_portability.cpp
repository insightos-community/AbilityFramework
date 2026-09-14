// Copyright 2026 InsightOS
// SPDX-License-Identifier: Apache-2.0
#include "doctest.h"
#include "subprocessmgr/subprocess_mgr.hpp"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <uvw.hpp>
#include <vector>

TEST_CASE("native subprocess launch reports child exit without ending the framework loop") {
    auto loop = uvw::loop::create();
    SubprocessManager manager(loop);
    auto deadline = loop->resource<uvw::timer_handle>();
    bool timed_out = false;
    bool exited = false;
    std::int64_t status = -1;
    deadline->on<uvw::timer_event>([&](const auto&, auto& timer) {
        timed_out = true;
        manager.on_exit();
        timer.close();
    });
    deadline->start(std::chrono::seconds(5), std::chrono::milliseconds(0));
#ifdef _WIN32
    const auto* system_root = std::getenv("SystemRoot");
    REQUIRE(system_root != nullptr);
    const auto executable = (std::filesystem::path(system_root) / "System32/cmd.exe").string();
    const std::vector<std::string> arguments{"/d", "/c", "exit", "/b", "7"};
#else
    const std::string executable = "/bin/sh";
    const std::vector<std::string> arguments{"-c", "exit 7"};
#endif
    manager.start_process(executable, arguments, {{"SEMANTIC_SPAWN_TEST", "ready"}},
        [&](const uvw::exit_event& event) {
            exited = true;
            status = event.status;
            deadline->close();
        });
    loop->run();
    CHECK_FALSE(timed_out);
    CHECK(exited);
    CHECK(status == 7);
}
