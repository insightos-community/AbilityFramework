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

#include "messagebus/messagebus.hpp"
#include "prelude.hpp"
#include "thread_pool.hpp"
#include "util/jthread.hpp"
#include "util/make_uuid.hpp"
#include <atomic>
#include <glog/logging.h>
#include <mutex>
#include <shared_mutex>
#include <uvw.hpp>

namespace message_bus {
// 消息总线的私有数据
namespace {
std::shared_mutex BusPrivateMtx;
// 从模块名到模块对应的指针
std::unordered_map<std::string, std::shared_ptr<Module>> ModuleMap;
std::vector<std::shared_ptr<Module>> ModuleStack;

// uv thread queue 有时候会遇到段错误,换用一个简单点的线程池
ThreadPool BusThreadPool(2);

} // namespace

expected<void, std::string> send(const Message& message) {
    const auto& destination = message.router.destination;
    if (destination.empty()) {
        LOG(ERROR) << "message destination is empty";
        return unexpected{"need destination"};
    }
    std::shared_lock _lk(BusPrivateMtx);
    auto it = ModuleMap.find(destination);
    if (it == ModuleMap.end()) {
        LOG(ERROR) << "no such module: " << destination;
        return unexpected{"no such module: " + destination};
    }
    try {
        auto& mod = it->second;
        return mod->on_receive(message);
    }
    catch (std::exception& e) {
        // 如果发生异常,那也是接收方的问题,因此在这里只是打印异常信息,返回值仍然是OK.
        LOG(INFO) << "error recurred when module " << destination
                  << " processing message: " << e.what();
    }
    return {};
}

expected<Message, std::string> send_sync(Message message, Duration timeout) {
    const auto& destination = message.router.destination;
    if (destination.empty()) {
        LOG(ERROR) << "message destination is empty";
        return unexpected{"need destination"};
    }
    std::shared_lock _lk(BusPrivateMtx);
    auto it = ModuleMap.find(destination);
    if (it == ModuleMap.end()) {
        LOG(ERROR) << "no such module: " << destination;
        return unexpected{"no such module: " + destination};
    }

    using Result = expected<Message, std::string>;
    std::promise<Result> prom;
    auto fut = prom.get_future();
    BusThreadPool.enqueue([message = std::move(message),
                           target_module = it->second,
                           prom_inner = std::move(prom)]() mutable {
        message.router.on_response
            = [&prom_inner](Message msg) mutable { prom_inner.set_value(Result{std::move(msg)}); };
        try {
            auto res = target_module->on_receive(message);
            if (!res) { prom_inner.set_value(Result{res.error()}); }
        }
        catch (FormatError& e) {
            prom_inner.set_value(Result(
                unexpected{nlohmann::json({{"reason", e.what()}, {"content", e.content}}).dump()}
            ));
        }
        catch (std::exception& e) {
            prom_inner.set_value(Result(unexpected{e.what()}));
        }
    });
    auto wait_state = fut.wait_for(timeout);
    if (wait_state == std::future_status::ready) { return fut.get(); }
    else { return unexpected{"timeout"}; }
}

void add_module(std::shared_ptr<Module> m) {
    CHECK(m) << "input module ptr is empty!";
    {
        std::unique_lock _lk(BusPrivateMtx);
        ModuleMap[m->module_name()] = m;
        ModuleStack.push_back(m);
    }
    m->on_register();
    LOG(INFO) << "added module: " << m->module_name();
}

bool remove_module(const std::string& name) {
    std::unique_lock _lk(BusPrivateMtx);
    if (!ModuleMap.contains(name)) {
        LOG(WARNING) << "trying to remove an unexistant module: " << name;
        return false;
    }
    auto it = ModuleMap.find(name);
    ModuleMap.erase(name);
    auto module_ptr = it->second;
    auto v_it = std::find(ModuleStack.begin(), ModuleStack.end(), module_ptr);
    if (v_it != ModuleStack.end()) { ModuleStack.erase(v_it); }

    LOG(INFO) << "removed module: " << name;
    return true;
}

namespace {
std::vector<char> span_to_vector(const auto& sp) {
    return std::vector<char>(sp.begin(), sp.end());
}
} // namespace

void Message::respond(const std::string& data) const {
    if (!router.on_response) {
        LOG(ERROR) << router.destination << " try to respond to a non-responsible message";
        throw std::logic_error("non-responsible message");
    };
    router.on_response(Message{
        .header = reply_from(header), .router = reply_from(router), .content = span_to_vector(data)
    });
}

void Message::respond(std::vector<char> data) const {
    if (!router.on_response) {
        LOG(ERROR) << router.destination << " try to respond to a non-responsible message";
        throw std::logic_error("non-responsible message");
    }

    router.on_response(Message(reply_from(header), reply_from(router), std::move(data)));
};
void Message::respond_error(const std::string& data) const {
    if (!router.on_response) {
        LOG(ERROR) << router.destination << " try to respond to a non-responsible message";
        throw std::logic_error("non-responsible message");
    };
    router.on_response(Message{
        .header = reply_from(header, Status::Failed),
        .router = reply_from(router),
        .content = std::vector(data.begin(), data.end())
    });
}
void Message::respond_error(std::string_view data) const {
    if (!router.on_response) {
        LOG(ERROR) << router.destination << " try to respond to a non-responsible message";
        throw std::logic_error("non-responsible message");
    };
    router.on_response(Message{
        .header = reply_from(header, Status::Failed),
        .router = reply_from(router),
        .content = std::vector(data.begin(), data.end())
    });
}
void Message::respond_error(std::vector<char> data) const {
    if (!router.on_response) {
        LOG(ERROR) << router.destination << " try to respond to a non-responsible message";
        throw std::logic_error("non-responsible message");
    }

    router.on_response(
        Message(reply_from(header, Status::Failed), reply_from(router), std::move(data))
    );
};
RouteInfo reply_from(const RouteInfo& r) {
    RouteInfo res{r};
    std::swap(res.source, res.destination);
    return res;
}

Header reply_from(const Header& h, Status status) {
    return Header{
        .id = make_uuid(),
        .parent_id = h.id,
        .timestamp = std::chrono::system_clock::now(),
        .is_synchronous = false,
        .status = status
    };
}

void stop_all_modules() {
    std::lock_guard _lk(BusPrivateMtx);
    // 从模块名到模块对应的指针
    // 按照添加顺序,逆序释放
    while (!ModuleStack.empty()) {
        ModuleStack.back()->on_exit();
        ModuleStack.pop_back();
    }
    ModuleMap.clear();
}
} // namespace message_bus
