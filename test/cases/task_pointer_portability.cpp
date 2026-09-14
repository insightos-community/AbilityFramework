// Copyright 2026 InsightOS
// SPDX-License-Identifier: Apache-2.0
#include "doctest.h"
#include "taskmgr/task.hpp"
#include "taskmgr/task_mgr.hpp"
#include <array>
#include <chrono>
#include <uvw.hpp>

namespace {
struct OtherInterfaceBase : virtual TaskInterface {
    std::array<std::uint64_t, 8> padding{};
};
struct OffsetTask : OtherInterfaceBase, TaskBase {
    int resumed = 0;
    TaskState resume() override {
        ++resumed;
        return set_state(TaskState::finished);
    }
};
}

TEST_CASE("task handles preserve interface pointers across virtual base offsets") {
    auto loop = uvw::loop::create();
    auto task = std::make_shared<OffsetTask>();
    CHECK(static_cast<void*>(static_cast<TaskInterface*>(task.get())) !=
          static_cast<void*>(static_cast<TaskBase*>(task.get())));
    TaskManager manager(loop);
    manager.add(task);
    auto deadline = loop->resource<uvw::timer_handle>();
    deadline->on<uvw::timer_event>([&](const auto&, auto&) {
        loop->walk([](auto& handle) { if (!handle.closing()) { handle.close(); } });
    });
    deadline->start(std::chrono::seconds(1), std::chrono::milliseconds(0));
    loop->run();
    CHECK(task->resumed == 1);
    CHECK(task->state() == TaskState::finished);
    CHECK(manager.at(task->id()).get() == static_cast<TaskInterface*>(task.get()));
}
