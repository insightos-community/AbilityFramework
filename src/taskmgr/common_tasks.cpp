// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#include "taskmgr/common_tasks.hpp"
#include "messagebus/message_client.hpp"
#include "prelude.hpp"
#include "taskmgr/task.hpp"
#include "taskmgr/task_status.hpp"
#include "util/cpptrace_debug.hpp"
#include "util/global_vars.hpp"
#include <glog/logging.h>
#include <uvw/work.h>

#include <utility>

std::ostream& operator<<(std::ostream& o, TaskState ts) {
#define _item(Name)       \
    case TaskState::Name: \
        o << #Name;       \
        return o;         \
        break;
    switch (ts) {
        _item(unstarted);
        _item(running);
        _item(pending);
        _item(fine);
        _item(finished);
        _item(error);
        _item(cancelled);
    default: {
        o << "???";
        return o;
    }
    }
#undef _item
}
namespace {

std::shared_ptr<uvw::loop> DefaultUVLoop = []() {
    auto l = uvw::loop::get_default();
    CHECK(l) << "need valid default loop ptr";
    return l;
}();
inline void record_task_status(TaskBase& task) {
    using namespace std::chrono_literals;
    TaskStatus status{
        .id = task.id(),
        .executor_id = global_vars::framework_id(),
        .executor_type = ExecutorType::framework,
        .state = task.state(),
        .start_time = format_as_task_datetime(task.start_time()),
        .end_time = format_as_task_datetime(task.end_time()),
        .timeout = 1h,
        .payload = task.result(),
        .message = task.message(),
    };
    auto res = send(make_message("TaskMgr", "TaskStatusMgr", "TaskStatusEvent", status));
    if (!res) { LOG(ERROR) << "send TaskStatus error:" << res.error(); }
}
#define record_status_and_return(ReturnStatus) \
    {                                          \
        auto s_ret = set_state(ReturnStatus);  \
        record_task_status(*this);             \
        return s_ret;                          \
    }
#define record_error_and_return(Eptr) \
    {                                 \
        auto s_ret = set_error(Eptr); \
        record_task_status(*this);    \
        return s_ret;                 \
    }

#define record_start_time_if_first_try                                                \
    if (this->start_time() == std::chrono::system_clock::time_point{}) [[unlikely]] { \
        record_start_time();                                                          \
        set_state(TaskState::running);                                                \
        record_task_status(*this);                                                    \
    }

class AtomicTask : public TaskBase {
    std::function<void()> task;
    std::string name_;

public:
    AtomicTask(std::function<void()> _task, std::string_view _name = "")
        : task(std::move(_task))
        , name_(_name) {
        CHECK_NOTNULL(task);
    };
    [[nodiscard]] std::string name() const override { return name_; }
    TaskState resume() override {
        CPPTRACE_TRY {
            record_start_time_if_first_try;
            task();
            record_status_and_return(TaskState::finished);
        }
        CPPTRACE_CATCH(...) {
            FWK_DUMP_STACKTRACE_TO_LOG(ERROR)
            record_error_and_return(std::current_exception());
        }
    }
    [[nodiscard]] nlohmann::json status() const override {
        return {
            {"name", name()},
            {"state", state()},
            {"type", "atomic"},
        };
    }
};

class ValueAtomicTask : public TaskBase {
    std::function<nlohmann::json()> task;
    std::string name_;
    nlohmann::json result_;

public:
    ValueAtomicTask(std::function<nlohmann::json()> _task, std::string_view _name = "")
        : task(std::move(_task))
        , name_(_name) {
        CHECK_NOTNULL(task);
    };
    [[nodiscard]] std::string name() const override { return name_; }
    TaskState resume() override {
        CPPTRACE_TRY {
            record_start_time_if_first_try;
            result_ = task();
            record_status_and_return(TaskState::finished);
        }
        CPPTRACE_CATCH(...) {
            FWK_DUMP_STACKTRACE_TO_LOG(ERROR)
            record_error_and_return(std::current_exception());
        }
    }

    [[nodiscard]] nlohmann::json status() const override {
        return {
            {"name", name()},
            {"state", state()},
            {"type", "atomic"},
        };
    }
    nlohmann::json result() const override { return result_; }
};

class SequentialTask : public TaskBase {
    std::vector<nlohmann::json> results;
    std::vector<TaskPtr> subs;
    std::string name_;
    size_t current = 0;

public:
    nlohmann::json result() const override {
        if (subs.empty()) { return {}; }
        if (!subs.back()) {
            LOG(ERROR) << "in sequence task " << name() << ", last task ptr is empty";
            return {};
        }
        return subs.back()->result();
    }
    SequentialTask(std::vector<TaskPtr> tasks, std::string_view _name = "")
        : subs(std::move(tasks))
        , name_(_name){};
    [[nodiscard]] std::string name() const override { return name_; }
    [[nodiscard]] nlohmann::json status() const override {
        nlohmann::json res{
            {"name", name()},
            {"state", state()},
            {"type", "sequence"},
            {"total", subs.size()},
            {"current", current}
        };
        for (const auto& sub : subs) {
            res["subtasks"].push_back(sub->status());
        }
        return res;
    }
    TaskState resume() override {
        using TS = TaskState;
        record_start_time_if_first_try;
        if (current >= subs.size()) { record_status_and_return(TaskState::finished); }
        auto& t = subs[current];
        auto s = t->state();
        if (s == TS::unstarted || s == TS::running || s == TS::fine) {
            try {
                s = t->resume();
            }
            catch (...) {
                record_error_and_return(std::current_exception());
            }
        }
        switch (s) {
        case TaskState::finished: {
            LOG(INFO) << "task " << name_ << " forward " << current << " to " << (current + 1);
            current++;
            auto my_state = current >= subs.size() ? TaskState::finished : TaskState::running;
            if (my_state == TaskState::finished) { record_status_and_return(TaskState::finished); }
            return set_state(my_state);
        } break;
        case TaskState::fine: {
            return set_state(s);
        } break;
        case TaskState::running: {
            return set_state(TaskState::running);
        } break;
        case TaskState::error: {
            record_error_and_return(t->error());
        } break;
        case TS::pending:
        case TS::cancelled: {
            record_status_and_return(s);
        } break;
        default: break;
        }
        record_status_and_return(TaskState::running);
    }
};

struct TimeoutError : public std::runtime_error {
    TimeoutError()
        : std::runtime_error("timeout") {}
    TimeoutError(const std::string& msg)
        : std::runtime_error(msg) {}
};

class WaitOnceTask : public TaskBase {
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    std::string name_;
    std::function<bool()> pred;
    std::function<std::string()> on_timeout;
    TimePoint begin_time, timeout_time;

public:
    WaitOnceTask(
        std::function<bool()> condition,
        Clock::duration max_timeout,
        std::function<std::string()> _on_timeout = {},
        std::string_view _name = ""
    )
        : pred(condition)
        , begin_time(Clock::now())
        , on_timeout(_on_timeout)
        , name_(_name) {
        timeout_time = begin_time + max_timeout;
    };
    std::string name() const override { return name_; }
    TaskState resume() noexcept override try {
        using TS = TaskState;
        record_start_time_if_first_try;
        auto now = Clock::now();
        if (now > timeout_time) {
            if (on_timeout) {
                auto timeout_info = on_timeout();

                record_error_and_return(std::make_exception_ptr(TimeoutError{timeout_info}));
            }
            record_error_and_return(std::make_exception_ptr(TimeoutError{}));
        }
        bool ok = pred();
        if (ok) { record_status_and_return(ok ? TS::finished : TS::running); }
        return set_state(TS::running);
    }
    catch (...) {
        record_error_and_return(std::current_exception());
    }
    nlohmann::json status() const override {
        return {
            {"name", name()},
            {"state", state()},
            {"type", "wait_once"},
        };
    }
};

class DelayTask : public TaskBase {
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    bool started = false;
    Clock::duration duration;
    TimePoint start_time, finish_time;

public:
    DelayTask(Clock::duration dur)
        : duration(dur){};

    std::string name() const override { return "delay"; }
    TaskState resume() noexcept override {
        using TS = TaskState;
        if (!started) {
            started = true;
            start_time = Clock::now();
            finish_time = start_time + duration;
            started = true;
        }
        auto now = Clock::now();
        if (now >= finish_time) { record_status_and_return(TaskState::finished); }
        return TaskState::running;
    }
    nlohmann::json status() const override {
        return {
            {"name", name()},
            {"state", state()},
            {"type", "delay"},
        };
    }
};

// for time-consuming work, put it in uv's thread pool so it does not block the main event loop
class ThreadQueueWorkTask : public TaskBase {

    std::shared_ptr<uvw::work_req> work_req;
    std::string name_;

public:
    ThreadQueueWorkTask(std::string_view _name, std::function<void()> task)
        : name_(_name)
        , work_req(DefaultUVLoop->resource<uvw::work_req>([inner_task = std::move(task),
                                                           this]() mutable noexcept {
            try {
                CHECK_NOTNULL(inner_task);
                LOG(INFO) << "thread queue task inner_task begin";
                inner_task();

                LOG(INFO) << "inner_task complete";
                set_state(TaskState::finished);
                record_task_status(*this);
            }
            catch (std::exception& e) {
                LOG(ERROR) << "inner_task error:" << e.what();
                set_error(std::current_exception());
                record_task_status(*this);
            }
            catch (...) {
                LOG(ERROR) << "inner_task error";
                set_error(std::current_exception());
                record_task_status(*this);
            }
        })) {

        CHECK_NOTNULL(work_req);
        work_req->on<uvw::error_event>([this](const auto& ev, uvw::work_req& self) {
            LOG(ERROR) << "queue work task error: " << ev.what();
            set_state(TaskState::error);
            record_task_status(*this);
        });
        work_req->on<uvw::work_event>([this](const auto& ev, uvw::work_req& self) {
            LOG(INFO) << "work event received";
        });
    }
    std::string name() const override { return name_; }
    TaskState resume() noexcept override try {
        // thread queue task taskstatus update must be done in the worker function, so only the start time is recorded here
        if (this->start_time() == std ::chrono ::system_clock ::time_point{}) [[unlikely]] {
            record_start_time();
        };
        if (state() == TaskState::unstarted) {
            LOG(INFO) << "uv queue work";
            set_state(TaskState::running);
            record_task_status(*this);
            work_req->queue();
        }
        if (error()) { return TaskState::error; }
        return state();
    }
    catch (...) {
        record_error_and_return(std::current_exception());
    }
    nlohmann::json status() const override {
        return {
            {"name", name()},
            {"state", state()},
            {"type", "thread_queue_work"},
        };
    }
};

class ParallelTask : public TaskBase {
    std::vector<TaskPtr> subs;
    std::string name_;

    int finished = 0;
    int current = 0;
    int find_first_running() {
        int start = current;
        if (!is_end(subs[start]->state())) { return start; }
        start = (start + 1) % subs.size();
        while (start != current) {
            if (!is_end(subs[start]->state())) { return start; }
        }
        throw std::runtime_error("invalid parallel task state");
    }

public:
    ParallelTask(std::string_view _name, std::vector<TaskPtr> tasks)
        : subs(std::move(tasks))
        , name_(_name){};

    std::string name() const override { return name_; }
    TaskState resume() noexcept override try {
        record_start_time_if_first_try;
        if (finished >= subs.size()) { return set_state(TaskState::finished); }
        current = find_first_running();
        auto sub_state = subs[current]->resume();
        if (sub_state == TaskState::error) { record_error_and_return(subs[current]->error()); }
        if (sub_state == TaskState::finished) {
            LOG(INFO) << "parallel task[" << current << "] finished";
            ++finished;
            if (finished >= subs.size()) { record_status_and_return(TaskState::finished); }
        }
        current = (current + 1) % subs.size();
        return set_state(TaskState::running);
    }
    catch (...) {
        record_error_and_return(std::current_exception());
    }
    nlohmann::json status() const override {
        nlohmann::json res{
            {"name", name()},
            {"state", state()},
            {"type", "parallel"},
            {"total", subs.size()},
            {"finished", finished}
        };
        for (auto& sub : subs) {
            res["subtasks"].push_back(sub->status());
        }
        return res;
    }
};

} // namespace

namespace tasks {
TaskPtr atomic(std::string_view name, std::function<void()> f) {
    return std::make_shared<AtomicTask>(f, name);
}
TaskPtr atomic(std::string_view name, std::function<nlohmann::json()> f) {
    return std::make_shared<ValueAtomicTask>(f, name);
}
TaskPtr atomic_with_return_value(std::string_view name, std::function<nlohmann::json()> f) {
    return std::make_shared<ValueAtomicTask>(f, name);
}

TaskPtr sequence(std::string_view name, std::vector<TaskPtr> tasks) {
    return std::make_shared<SequentialTask>(std::move(tasks), name);
}
TaskPtr delay(std::chrono::steady_clock::duration duration) {
    return std::make_shared<DelayTask>(duration);
}
TaskPtr wait_once(
    std::string_view _name,
    std::function<bool()> condition,
    std::chrono::steady_clock::duration max_timeout,
    std::function<std::string()> _on_timeout
) {
    return std::make_shared<WaitOnceTask>(condition, max_timeout, _on_timeout, _name);
}

TaskPtr on_thread_queue(std::string_view name, std::function<void()> f) {
    return std::make_shared<ThreadQueueWorkTask>(name, std::move(f));
}

TaskPtr parallel(std::string_view name, std::vector<TaskPtr> tasks) {
    return std::make_shared<ParallelTask>(name, std::move(tasks));
}
}; // namespace tasks
