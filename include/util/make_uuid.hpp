// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <span>
#include <uuid.h>

namespace detail {
// generate deterministic seed based on packageName, version, name and kind
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

// generate deterministic seed based on framework name
inline std::size_t generate_seed(const std::string& FrameworkName) {
    std::hash<std::string> hasher;
    return hasher(FrameworkName);
}

// initialize RNG from seed
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

// generate UUID for CRD or CR
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

// generate framework UUID
inline uuids::uuid make_uuid(const std::string& FrameworkName) {
    auto seed = detail::generate_seed(FrameworkName);
    std::mt19937 generator = detail::initialize_generator_with_seed(seed);
    uuids::uuid_random_generator g(generator);
    return g();
}
