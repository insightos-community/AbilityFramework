// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

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
// private data of the message bus
namespace {
std::shared_mutex BusPrivateMtx;
// from module name to module pointer
std::unordered_map<std::string, std::shared_ptr<Module>> ModuleMap;
std::vector<std::shared_ptr<Module>> ModuleStack;

// uv thread queue sometimes segfaults, switch to a simpler thread pool
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
        // if an exception occurred, that is also the receiver's problem; therefore we only print the exception info here and the return value is still OK.
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
    // from module name to module pointer
    // release in reverse insertion order
    while (!ModuleStack.empty()) {
        ModuleStack.back()->on_exit();
        ModuleStack.pop_back();
    }
    ModuleMap.clear();
}
} // namespace message_bus
