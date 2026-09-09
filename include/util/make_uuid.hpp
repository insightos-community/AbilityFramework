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
#include <span>
#include <uuid.h>

namespace detail {
// 基于packageName,version,name和kind生成确定性种子
inline std::size_t generate_seed(
    const std::string& packageName, 
    const std::string& version, 
    const std::string& name, 
    const std::string& kind) {
    std::hash<std::string> hasher;
    return hasher(packageName) ^ 
          (hasher(version) << 1) ^
          (hasher(name) << 2) ^
          (hasher(kind) << 3);
}

// 基于框架名生成确定性种子
inline std::size_t generate_seed(const std::string& FrameworkName) {
    std::hash<std::string> hasher;
    return hasher(FrameworkName);
}

// 基于种子初始化随机数生成器
inline auto initialize_generator_with_seed(std::size_t seed) {
    std::mt19937 gen(seed);
    return gen;
}

inline auto initialize_generator_mt19937() {
    std::random_device rd;
    auto seed_data = std::array<int, std::mt19937::state_size>{};
    std::generate(std::begin(seed_data), std::end(seed_data), std::ref(rd));
    std::seed_seq seq(std::begin(seed_data), std::end(seed_data));
    std::mt19937 gen(seq);
    return gen;
}
}; // namespace detail

inline uuids::uuid make_uuid() {
    static std::mt19937 source = detail::initialize_generator_mt19937();
    static uuids::uuid_random_generator g(source);
    return g();
}

// 生成crd或者cr的UUID
inline uuids::uuid make_uuid(
    const std::string& packageName, 
    const std::string& version, 
    const std::string& name, 
    const std::string& kind) {
    auto seed = detail::generate_seed(packageName, version, name, kind);
    std::mt19937 generator = detail::initialize_generator_with_seed(seed);
    uuids::uuid_random_generator g(generator);
    return g();
}

// 生成框架的UUID
inline uuids::uuid make_uuid(const std::string& FrameworkName) {
    auto seed = detail::generate_seed(FrameworkName);
    std::mt19937 generator = detail::initialize_generator_with_seed(seed);
    uuids::uuid_random_generator g(generator);
    return g();
}
