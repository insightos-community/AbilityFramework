#include "subprocessmgr/subprocess_mgr.hpp"
#include <glog/logging.h>
#include <uvw.hpp>

namespace {
void configure_glog(const char* argv0) {
    FLAGS_colorlogtostderr = true;
    FLAGS_alsologtostderr = true;
    FLAGS_max_log_size = 1024; // max log size is 100M
    FLAGS_stop_logging_if_full_disk = true; // stop logging when disk is full
    google::InitGoogleLogging(argv0);
    google::InstallFailureSignalHandler();
}
} // namespace

int main(int argc, const char** argv) {
    configure_glog(argv[0]);
    // initialize libuv event loop
    auto loop = uvw::loop::get_default();
    CHECK_NOTNULL(loop);
    SubprocessManager mgr(loop);
    auto id = mgr.start_process(
        "bin/test-cr", {}, // command-line arguments
        {}, // environment variable
        [loop](const uvw::exit_event&) {
            LOG(INFO) << "subprocess exited shutting down loop";
            loop->walk([](auto&& sub) { sub.close(); });
            LOG(INFO) << "loop shut down";
        },
        {{"kind", "test"}} // label
    );
    LOG(INFO) << "loop run";
    loop->run();
    LOG(INFO) << "loop finish";
}
