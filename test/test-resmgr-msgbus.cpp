#include "messagebus/messagebus.hpp"
#include "prelude.hpp"
#include "subprocessmgr/subprocess_mgr.hpp"
#include "util/jthread.hpp"
#include "util/make_uuid.hpp"
#include "resourcemgr/resource_mgr.hpp"
#include <glog/logging.h>
#include <nlohmann/json.hpp>
#include <uvw.hpp>
using namespace std::chrono_literals;
namespace {
void configure_glog(const char* argv0) {
    FLAGS_colorlogtostderr = true;
    FLAGS_alsologtostderr = true;
    FLAGS_max_log_size = 1024; // max log size is 100M
    FLAGS_stop_logging_if_full_disk = true; // stop logging when disk is full
    google::InitGoogleLogging(argv0);
    google::InstallFailureSignalHandler();
}

template <typename T>
auto send_msg_sync(
    const std::string& from, const std::string& to, const std::string& operation, T&& data
) {
    std::string serialized_data = nlohmann::json(std::forward<T>(data)).dump(); 
    message_bus::Message message{
        .header{
            .id = make_uuid(),
            .parent_id{},
            .timestamp{std::chrono::system_clock::now()},
            .is_synchronous = false
        },
        .router{.source = from, .destination = to, .operation = operation},
        .content{serialized_data.begin(), serialized_data.end()}
    };
    return message_bus::send_sync(std::move(message));
}
} // namespace

int main(int argc, const char** argv) {
    configure_glog(argv[0]);
    // initialize libuv event loop
    auto loop = uvw::loop::get_default();
    CHECK_NOTNULL(loop);
    auto timer = loop->resource<uvw::timer_handle>();
    timer->on<uvw::timer_event>([](const auto& ev, auto& self) { self.close(); });
    timer->start(20s, 0s);
    // initialize module
    auto test_mod_ptr = std::make_shared<ResourceManager>();
    test_mod_ptr->update();
    // add module to the bus
    message_bus::add_module(test_mod_ptr);
    // start event loop
    jthread th_uv_loop([&loop]() {
        //message_bus::start(loop);
        LOG(INFO) << "loop run";
        loop->run();
        LOG(INFO) << "loop finish";
    });
    LOG(INFO) << "a message struct has " << sizeof(message_bus::Message) << " bytes";
    LOG(WARNING) << "begin test sync msg";
    uuids::uuid id = make_uuid();
    auto res = send_msg_sync( "test", "ResourceMgr", "find_ability_instance/id", id );
    if (!res) { LOG(ERROR) << "send sync msg failed: " << res.error(); }
    else {
        nlohmann::json res_json = nlohmann::json::parse(res->content);
        LOG(INFO) << "send sync res response: " << res_json.dump(4);
    }
}
