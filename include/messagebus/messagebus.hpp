// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

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

// the message bus is globally unique within the program
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
    std::string source; // source module of the message
    std::string destination; // destination module of the message
    std::string operation; // what operation to execute

    // if this message requires a reply, this function is non-null
    MsgCallback on_response;
};

/// create a new route info, with source route info's source/destination exactly reversed, and without on_response
RouteInfo reply_from(const RouteInfo&);

struct Header {
    uuids::uuid id; // message id
    uuids::uuid parent_id; // if this is a reply message, it is the id of the source message
    Timestamp timestamp;
    bool is_synchronous = false; // whether this message is a sync message
    Status status = Status::OK; // indicates whether successful
};

// from sourceheader construct a new header,its parent_id equals the source header's id.
Header reply_from(const Header&, Status status = Status::OK);

// payload base class corresponding to extra info
struct AbstractExtra {
    virtual ~AbstractExtra() = default;
};

// class for carrying extra messages,used to carry content that cannot be described by a character sequence
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
    // content is a character sequence
    std::vector<char> content;
    // extra info
    mutable std::unique_ptr<AbstractExtra> extra = nullptr;

    std::string_view operation() const { return router.operation; }
    std::string_view view() const { return {content.data(), content.size()}; }
    // reply with a message
    void respond(const std::string& data) const;

    void respond(std::vector<char> data) const;
    /// serialize a value to json, then write to content
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
    /// if the request has an error, reply with an error result
    /// @param data serialized error info
    void respond_error(std::string_view data) const;
    /// if the request has an error, reply with an error result
    /// @param data serialized error info
    void respond_error(const std::string& data) const;
    /// if the request has an error, reply with an error result
    /// @param data serialized error info
    void respond_error(std::vector<char> data) const;
    template <ToJson T>
    void respond(const expected<T, std::string>& data) const {
        if (data) { respond(*data); }
        else { respond_error(data.error()); }
    }
    /// check whether this message has this type of extra info,
    /// @tparam T: expected extra parameter type
    /// @return if it is, return its corresponding pointer; if not, return nullptr.
    template <NoCVRef T>
    T* get_extra() {
        if (!extra) { return nullptr; }
        auto hold = dynamic_cast<Extra<T>*>(extra.get());
        if (!hold) { return nullptr; }
        return std::addressof(hold->content);
    }
    /// check whether this message has this type of extra info,
    /// @tparam T: expected extra parameter type
    /// @return if it is, return its corresponding pointer; if not, return nullptr.
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
    /// assume the message body is json and parse it into a struct
    /// @return corresponding struct
    /// @throw nlohmann::json::error ifparse error
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

    /// assume the message body is json and parse it into a struct
    /// @return corresponding struct; returns an error message if the message itself is an error or parsing fails
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

/// builda unexpected, used to report an invalid message header
inline unexpected<std::string> err_invalid_operation(const Message& m) {
    return unexpected{"invalid operation: " + std::string(m.operation())};
}

// common interface of the module
struct Module {
    virtual ~Module() = default;
    // callback when the module receives a message; the message may be dispatched to its sub-modules
    // ifhas exception,then return error info
    // note: the message bus is not responsible for intra-module thread safety,
    // modules are responsible for ensuring their own data consistency via locking or other means
    virtual expected<void, std::string> on_receive(const Message& m) {
        return unexpected{"module " + module_name() + " doesn't implement an on_receive method"};
    };
    [[nodiscard]] virtual std::string module_name() const = 0;
    virtual void on_register() {};
    virtual void on_exit() {};
};

// send a message to a module
// @on successful send returns Ok; the sender should not expect a reply
expected<void, std::string> send(const Message& message);

// send a message to a module and synchronously wait for reply
// @return afuture,sender can wait for the reply via this
// this function starts a new libuv work, using uv's thread pool to execute message-passing work
expected<Message, std::string> send_sync(Message message, Duration timeout = DefaultTimeout);

void add_module(std::shared_ptr<Module>);
/// @return whether this module actually exists and was truly deleted
bool remove_module(const std::string& name);

void stop_all_modules();
}; // namespace message_bus
