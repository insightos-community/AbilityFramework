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

#include "subprocessmgr/subprocess_mgr.hpp"
#include <glog/logging.h>
#include <uvw.hpp>

namespace {
void configure_glog(const char* argv0) {
    FLAGS_colorlogtostderr = true;
    FLAGS_alsologtostderr = true;
    FLAGS_max_log_size = 1024;              // 最大日志大小为100M
    FLAGS_stop_logging_if_full_disk = true; // 当磁盘被写满时，停止日志输出
    google::InitGoogleLogging(argv0);
    google::InstallFailureSignalHandler();
}
} // namespace

int main(int argc, const char** argv) {
    configure_glog(argv[0]);
    // 初始化 libuv 事件循环
    auto loop = uvw::loop::get_default();
    CHECK_NOTNULL(loop);
    SubprocessManager mgr(loop);
    auto id = mgr.start_process(
        "bin/test-cr", {}, // 命令行参数
        {},                // 环境变量
        [loop](const uvw::exit_event&) {
            LOG(INFO) << "subprocess exited shutting down loop";
            loop->walk([](auto&& sub) { sub.close(); });
            LOG(INFO) << "loop shut down";
        },
        {{"kind", "test"}} // 标签
    );
    LOG(INFO) << "loop run";
    loop->run();
    LOG(INFO) << "loop finish";
}
