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

#pragma once
#include "util/expected.hpp"
#include <functional>
#include <future>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <utility>
#include <uuid.h>
#include <uvw/loop.h>
#include <uvw/timer.h>

// 消息总线在程序全局范围内是唯一的
namespace message_bus {
using Timestamp = std::chrono::system_clock::time_point;
using Duration = uvw::timer_handle::time;

constexpr Duration DefaultTimeout = std::chrono::seconds{10};

enum class Status : char { OK, Failed, NotExist };

struct Message;

using MsgCallback = std::function<void(Message)>;

struct InvalidOperation : public std::invalid_argument {
    std::string operation;
    InvalidOperation(const std::string& _operation)
        : std::invalid_argument("invalid operation: " + _operation)
        , operation(_operation) {}
};

struct FormatError : public std::exception {
    std::string message, content;
    FormatError(std::string msg, std::string_view _content)
        : message("FormatError: " + std::move(msg))
        , content(_content) {}
    const char* what() const noexcept override { return message.c_str(); }
};

struct RouteInfo {
    std::string source;      // 消息来源模块
    std::string destination; // 消息目的地模块
    std::string operation;   // 要执行什么操作

    // 如果此消息要求回复,那么此函数非空
    MsgCallback on_response;
};

/// 创建一个新routeinfo,与源routeinfo的source/destination正好相反,并且不具有on_response
RouteInfo reply_from(const RouteInfo&);

struct Header {
    uuids::uuid id;        // 消息id
    uuids::uuid parent_id; // 如果这条消息是回复消息,那么它是源消息的id
    Timestamp timestamp;
    bool is_synchronous = false; // 这个消息是不是同步消息
    Status status = Status::OK;  // 表示是否成功
};

// 从源header 构造出一个新header,它的parent_id等同与源header的id.
Header reply_from(const Header&, Status status = Status::OK);

// 额外信息所对应的负载基类
struct AbstractExtra {
    virtual ~AbstractExtra() = default;
};

// 装载额外消息的类,用于承载无法用字符序列描述的内容
template <typename T>
struct Extra : AbstractExtra {
    T content;
    Extra(T&& _c)
        : content(std::move(_c)) {}
    Extra(const T& _c)
        : content(_c) {}
};

template <typename T>
concept NoCVRef = std::is_same_v<T, std::remove_cvref_t<T>>;

template <typename T>
concept ToJson = requires(T x) { nlohmann::json(x); };

template <typename T>
concept FromJson = requires(nlohmann::json j) { j.get<T>(); };

struct Message {
    Header header;
    RouteInfo router;
    // content是一个字符序列
    std::vector<char> content;
    // 额外信息
    mutable std::unique_ptr<AbstractExtra> extra = nullptr;

    std::string_view operation() const { return router.operation; }
    std::string_view view() const { return {content.data(), content.size()}; }
    // 回复一个消息
    void respond(const std::string& data) const;

    void respond(std::vector<char> data) const;
    /// 将一个值序列化json,然后写入content
    template <ToJson T>
        requires(!std::is_same_v<T, std::span<const char>>) && (!std::is_same_v<T, std::span<char>>)
             && (!std::is_same_v<T, std::vector<char>>) && (!std::is_same_v<T, std::string>)
    void respond(const T& data) const {
        if constexpr (std::is_same_v<nlohmann::json, T>) {
            std::string str = data.dump();
            respond(str);
        }
        else {
            std::string str = nlohmann::json(data).dump();
            respond(str);
        }
    }
    /// 如果请求有错误,以报错结果回复一个消息
    /// @param data 序列化后的错误信息
    void respond_error(std::string_view data) const;
    /// 如果请求有错误,以报错结果回复一个消息
    /// @param data 序列化后的错误信息
    void respond_error(const std::string& data) const;
    /// 如果请求有错误,以报错结果回复一个消息
    /// @param data 序列化后的错误信息
    void respond_error(std::vector<char> data) const;
    template <ToJson T>
    void respond(const expected<T, std::string>& data) const {
        if (data) { respond(*data); }
        else { respond_error(data.error()); }
    }
    /// 检查该消息包是否有这种类型的附加信息,
    /// @tparam T 所期待的额外参数类型
    /// @return 如果是,则返回它对应的指针,如果不是,返回nullptr.
    template <NoCVRef T>
    T* get_extra() {
        if (!extra) { return nullptr; }
        auto hold = dynamic_cast<Extra<T>*>(extra.get());
        if (!hold) { return nullptr; }
        return std::addressof(hold->content);
    }
    /// 检查该消息包是否有这种类型的附加信息,
    /// @tparam T 所期待的额外参数类型
    /// @return 如果是,则返回它对应的指针,如果不是,返回nullptr.
    template <NoCVRef T>
    const T* get_extra() const {
        if (!extra) { return nullptr; }
        auto hold = dynamic_cast<Extra<T>*>(extra.get());
        if (!hold) { return nullptr; }
        return std::addressof(hold->content);
    }
    template <NoCVRef T>
    void set_extra(T data) {
        extra = std::make_unique<Extra<T>>(std::move(data));
    }
    bool success() const noexcept { return header.status == Status::OK; }
    /// 假定消息内容是json,将其解析为某个结构体
    /// @return 对应结构体
    /// @throw nlohmann::json::error 如果解析错误
    template <FromJson T>
    T parse_as() const {
        std::string err_msg;
        try {
            return nlohmann::json::parse(content).get<T>();
        }
        catch (nlohmann::json::exception& e) {
            err_msg = e.what();
        }
        if (!err_msg.empty()) {
            throw FormatError(err_msg, std::string_view(content.data(), content.size()));
        }
        throw std::logic_error(__FILE__ " : unreachable");
    }

    /// 假定消息内容是json,将其解析为某个结构体
    /// @return 对应结构体,如果消息本身为错误信息或解析时格式错误,则返回错误消息
    template <FromJson T>
    expected<T, std::string> try_parse() const {
        if (!success()) { return unexpected{view()}; }
        try {
            return nlohmann::json::parse(content).get<T>();
        }
        catch (nlohmann::json::exception& e) {
            using namespace std::string_literals;
            return unexpected{"invalid format: "s + e.what()};
        }
    }
};

/// 构建一个 unexpected, 用于报告不合法的消息头
inline unexpected<std::string> err_invalid_operation(const Message& m) {
    return unexpected{"invalid operation: " + std::string(m.operation())};
}

// 模块的通用接口
struct Module {
    virtual ~Module() = default;
    // 模块接受消息时的回调函数,这个消息可能被下发到它的子模块
    // 如果有异常,则返回错误信息
    // 注意,消息总线不负责模块内部的线程安全,
    // 模块有责任用加锁或其他方式保证自己的数据一致性
    virtual expected<void, std::string> on_receive(const Message& m) {
        return unexpected{"module " + module_name() + " doesn't implement an on_receive method"};
    };
    [[nodiscard]] virtual std::string module_name() const = 0;
    virtual void on_register() {};
    virtual void on_exit() {};
};

// 向某模块发送消息
// @如果发送成功,则返回Ok, 发送方不应该期待消息的回复
expected<void, std::string> send(const Message& message);

// 向某模块发送消息,并同步等待回答
// @return 一个future,发送方可以由此等待回答
// 这个函数的行为是新开启一个libuv work,使用uv的线程池来执行消息传递的工作
expected<Message, std::string> send_sync(Message message, Duration timeout = DefaultTimeout);

void add_module(std::shared_ptr<Module>);
/// @return 是否确实存在这个模块且它真正被删除了
bool remove_module(const std::string& name);

void stop_all_modules();
}; // namespace message_bus
