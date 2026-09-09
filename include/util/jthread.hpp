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

#include <version>

#if __cpp_lib_jthread >= 201911L
// 标准库里已经有了,直接用
#include <thread>
using std::jthread;
using std::stop_source;
using std::stop_token;

// 否则用如下实现
#else

#include <memory>

namespace draft::thread_aux {
struct StopState {
    std::atomic<bool> stopped = false;
};
struct nostopstate_t {};

inline constexpr nostopstate_t nostopstate;

class stop_token {
    std::shared_ptr<StopState> data;
    friend class stop_source;

public:
    stop_token()
        : data(nullptr) {}
    [[nodiscard]] bool stop_requested() const { return data && data->stopped; }
    [[nodiscard]] bool stop_possible() const { return data != nullptr; }
};

class stop_source {
    std::shared_ptr<StopState> data;

public:
    stop_source(nostopstate_t _)
        : data(nullptr) {}
    stop_source()
        : data(std::make_shared<StopState>()) {}
    [[nodiscard]] bool stop_requested() const { return data && data->stopped; }
    [[nodiscard]] bool stop_possible() const { return data != nullptr; }
    bool request_stop() {
        if (!data) { return false; }
        data->stopped = true;
        return true;
    }
    [[nodiscard]] stop_token get_token() const {
        stop_token s;
        s.data = data;
        return s;
    }
};

using std::move, std::forward;
;
class jthread {
    stop_source stop;
    std::thread th;

public:
    using id = std::thread::id;
    using native_handle_type = std::thread::native_handle_type;

    [[nodiscard]] bool joinable() const { return th.joinable(); }
    [[nodiscard]] id get_id() const { return th.get_id(); }
    [[nodiscard]] stop_source get_stop_source() const { return stop; }
    [[nodiscard]] stop_token get_stop_token() const { return stop.get_token(); }
    [[nodiscard]] bool request_stop() { return stop.request_stop(); }
    static auto hardward_concurrency() { return std::thread::hardware_concurrency(); }
    void join() { th.join(); }
    void detach() { th.detach(); }

    ~jthread() {
        if (th.joinable()) {
            stop.request_stop();
            th.join();
        }
    }
    jthread()
        : stop(nostopstate) {}
    jthread(const jthread&) = delete;
    jthread(jthread&& other) noexcept
        : stop(std::move(other.stop))
        , th(std::move(other.th)) {}
    template <typename F, typename... Args>
    explicit jthread(F&& f, Args&&... args) {
        if constexpr (std::is_invocable_v<std::decay_t<F>, stop_token, std::decay_t<Args>...>) {
            th = std::thread(forward<F>(f), get_stop_token(), forward<Args>(args)...);
        }
        else {
            static_assert(
                std::is_invocable_v<std::decay_t<F>, std::decay_t<Args>...>,
                "std::jthread arguments must be invocable after"
                " conversion to rvalues"
            );
            th = std::thread{forward<F>(f), forward<Args>(args)...};
        }
    }
};

} // namespace draft::thread_aux

using draft::thread_aux::jthread;
using draft::thread_aux::stop_source;
using draft::thread_aux::stop_token;
#endif
