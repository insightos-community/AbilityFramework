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

#include <string>
#include <functional>
#include <sstream>
#include <uuid.h>
#include <random>
#include <iostream>

namespace detail {

// 基于name和version生成确定性种子
inline std::size_t generate_seed(const std::string& name, const std::string& version) {
    std::hash<std::string> hasher;
    auto res = hasher(name) ^ (hasher(version) << 1);
    std::cout << "Result: " << res << std::endl;
    return res;
}

// 基于种子初始化随机数生成器
inline auto initialize_generator_with_seed(std::size_t seed) {
    std::mt19937 gen(seed);
    return gen;
}

} // namespace detail

// 生成UUID的函数，基于name和version
inline uuids::uuid make_uuid(const std::string& name, const std::string& version) {
    auto seed = detail::generate_seed(name, version);
    std::mt19937 generator = detail::initialize_generator_with_seed(seed);
    uuids::uuid_random_generator g(generator);
    return g();
}

// 测试主函数
int main() {
    std::string name1 = "example-crd";
    std::string version1 = "v1.0";

    std::string name2 = "example";
    std::string version2 = "v2.0";

    // 使用相同name和version生成UUID
    uuids::uuid uuid1 = make_uuid(name1, version1);
    uuids::uuid uuid2 = make_uuid(name1, version1);

    // 使用不同name或version生成UUID
    uuids::uuid uuid3 = make_uuid(name2, version1);
    uuids::uuid uuid4 = make_uuid(name1, version2);
    uuids::uuid uuid5 = make_uuid(name1, version2);
    uuids::uuid uuid6 = make_uuid(name2, version2);

    std::cout << "UUID 1 (same name/version): " << uuid1 << '\n';
    std::cout << "UUID 2 (same name/version): " << uuid2 << '\n';
    std::cout << "UUID 3 (different name): " << uuid3 << '\n';
    std::cout << "UUID 4 (different version): " << uuid4 << '\n';
    std::cout << "UUID 5 (different version): " << uuid5 << '\n';
    std::cout << "UUID 6 (different version): " << uuid6 << '\n';

    return 0;
}
