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

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>

namespace util {

/**
 * @brief 双缓冲 Map
 * 读写分离设计，适合读多写少的高并发场景。
 * 读操作基于只读快照（无锁或极低开销），写操作写入缓冲区并在同步点更新快照。
 *
 * @tparam Key 键类型
 * @tparam T 值类型
 * @tparam SyncIntervalMs 读写缓冲同步间隔（毫秒，编译期配置）
 * @tparam Hash 哈希函数
 * @tparam KeyEqual 键比较函数
 */
template <typename Key, typename T, std::size_t SyncIntervalMs = 1000>
class DoubleBufferedMap {
public:
    using key_type = Key;
    using mapped_type = T;
    using value_type = std::pair<const Key, T>;
    using size_type = std::size_t;

    static constexpr std::size_t sync_interval_ms = SyncIntervalMs;

    DoubleBufferedMap();
    ~DoubleBufferedMap();

    DoubleBufferedMap(const DoubleBufferedMap&) = delete;
    DoubleBufferedMap& operator=(const DoubleBufferedMap&) = delete;

    /** @brief 查找元素 (返回副本) */
    std::optional<T> find(const Key& key) const;

    /** @brief 检查是否存在 */
    bool contains(const Key& key) const;

    /**
     * @brief 获取元素（只读，返回副本）
     * 若 key 不存在则抛出 std::out_of_range
     */
    T operator[](const Key& key) const;
    T at(const Key& key) const;

    /** @brief 插入或更新 */
    void insert_or_assign(const Key& key, const T& value);
    void insert_or_assign(const Key& key, T&& value);

    /** @brief 原地构造插入 */
    template <typename... Args>
    bool emplace(const Key& key, Args&&... args);

    /** @brief 删除元素 */
    void erase(const Key& key);

    /** @brief 清空容器 */
    void clear();

    /** @brief 获取大小 (快照大小) */
    size_type size() const;

    /** @brief 判空 */
    bool empty() const;

    /** @brief 遍历操作 (无锁，基于快照) */
    template <typename F>
    void for_each(F&& func) const;

    /**
     * @brief 针对特定 Key 的操作 (在写缓冲上执行)
     * 允许用户自定义对 Key 对应 Value 的操作。
     * 注意：此操作持有写锁。
     */
    template <typename F>
    void exec_on_key(const Key& key, F&& func);

    /**
     * @brief 双缓冲同步（写缓冲 -> 读缓冲）
     */
    void sync();

private:
    using map_type = std::unordered_map<key_type, mapped_type>;

    // 写缓冲
    mutable std::mutex write_mutex_;
    map_type write_buf_;

    // 读快照 (RCU) - 总是指向一个有效的 map
    std::shared_ptr<const map_type> read_snp_;

    // 自动同步线程控制
    std::mutex sync_mutex_;
    std::atomic<long long> last_sync_ms_{0};
    std::atomic<bool> stop_thread_{false};
    std::thread sync_thread_;

    void maybe_sync();
    void sync_locked();
};

/** @brief 自旋锁实现 */
class SpinLock {
public:
    void lock() noexcept {
        while (flag_.test_and_set(std::memory_order_acquire)) {
#if defined(__cpp_lib_atomic_wait)
            flag_.wait(true, std::memory_order_relaxed);
#endif
        }
    }

    bool try_lock() noexcept { return !flag_.test_and_set(std::memory_order_acquire); }

    void unlock() noexcept {
        flag_.clear(std::memory_order_release);
#if defined(__cpp_lib_atomic_wait)
        flag_.notify_one();
#endif
    }

private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

/**
 * @brief 分段锁 Map
 *
 * @tparam Key 键类型
 * @tparam T 值类型
 * @tparam ShardCount 分片数量（静态配置）
 * @tparam Lock 锁类型（支持 std::mutex, util::SpinLock 等）
 * @tparam Hash 哈希函数
 * @tparam KeyEqual 键比较函数
 */
template <
    typename Key,
    typename T,
    std::size_t ShardCount = 4,
    typename Lock = std::mutex,
    typename Hash = std::hash<Key>,
    typename KeyEqual = std::equal_to<Key>>
class ShardedLockMap {
public:
    using key_type = Key;
    using mapped_type = T;
    using value_type = std::pair<const Key, T>;
    using size_type = std::size_t;
    using hasher = Hash;
    using key_equal = KeyEqual;

    ShardedLockMap() = default;
    ~ShardedLockMap() = default;

    ShardedLockMap(const ShardedLockMap&) = delete;
    ShardedLockMap& operator=(const ShardedLockMap&) = delete;

    /**
     * @brief 查找元素 (复制返回)
     * 为了线程安全，find 操作返回值的副本或 std::optional
     */
    std::optional<T> find(const Key& key) const {
        size_type idx = get_shard_index(key);
        std::lock_guard<Lock> lock(shards_[idx].lock);
        auto it = shards_[idx].map.find(key);
        if (it != shards_[idx].map.end()) { return it->second; }
        return std::nullopt;
    }

    /** @brief 检查是否存在 */
    bool contains(const Key& key) const {
        size_type idx = get_shard_index(key);
        std::lock_guard<Lock> lock(shards_[idx].lock);
        return shards_[idx].map.find(key) != shards_[idx].map.end();
    }

    /** @brief 下标操作 (返回副本，若不存在则插入默认值) */
    T operator[](const Key& key) {
        size_type idx = get_shard_index(key);
        std::lock_guard<Lock> lock(shards_[idx].lock);
        return shards_[idx].map[key];
    }

    /** @brief 插入或更新 (Usage: map.insert_or_assign(k, v)) */
    void insert_or_assign(const Key& key, const T& value) {
        size_type idx = get_shard_index(key);
        std::lock_guard<Lock> lock(shards_[idx].lock);
        shards_[idx].map[key] = value;
    }

    void insert_or_assign(const Key& key, T&& value) {
        size_type idx = get_shard_index(key);
        std::lock_guard<Lock> lock(shards_[idx].lock);
        shards_[idx].map[key] = std::move(value);
    }

    /**
     * @brief 插入 (Usage: map.emplace(k, args...))
     * @return true if inserted, false if already exists
     */
    template <typename... Args>
    bool emplace(const Key& key, Args&&... args) {
        size_type idx = get_shard_index(key);
        std::lock_guard<Lock> lock(shards_[idx].lock);
        // unordered_map::emplace 不一定检查 key 是否存在 (它会 insert if not exists)
        auto it = shards_[idx].map.find(key);
        if (it != shards_[idx].map.end()) { return false; }
        shards_[idx].map.emplace(key, std::forward<Args>(args)...);
        return true;
    }

    /** @brief 删除 */
    size_type erase(const Key& key) {
        size_type idx = get_shard_index(key);
        std::lock_guard<Lock> lock(shards_[idx].lock);
        return shards_[idx].map.erase(key);
    }

    /** @brief 清除所有 */
    void clear() {
        for (auto& shard : shards_) {
            std::lock_guard<Lock> lock(shard.lock);
            shard.map.clear();
        }
    }

    /** @brief 获取大小 (近似值，不加全局锁) */
    size_type size() const {
        size_type total = 0;
        for (const auto& shard : shards_) {
            std::lock_guard<Lock> lock(shard.lock);
            total += shard.map.size();
        }
        return total;
    }

    /** @brief 判空 */
    bool empty() const {
        for (const auto& shard : shards_) {
            std::lock_guard<Lock> lock(shard.lock);
            if (!shard.map.empty()) { return false; }
        }
        return true;
    }

    /**
     * @brief 遍历操作
     * @param func void(const Key&, T&) or void(const Key&, const T&)
     */
    template <typename F>
    void for_each(F&& func) {
        for (auto& shard : shards_) {
            std::lock_guard<Lock> lock(shard.lock);
            for (auto& kv : shard.map) {
                func(kv.first, kv.second);
            }
        }
    }

    template <typename F>
    void for_each(F&& func) const {
        for (const auto& shard : shards_) {
            std::lock_guard<Lock> lock(shard.lock);
            for (const auto& kv : shard.map) {
                func(kv.first, kv.second);
            }
        }
    }

    /**
     * @brief 针对特定 Key 的操作 (Access with lock)
     * 允许用户自定义对 Key 对应 Value 的操作，保持锁
     * 如果 key 不存在，则会自动创建默认值
     */
    template <typename F>
    void exec_on_key(const Key& key, F&& func) {
        size_type idx = get_shard_index(key);
        std::lock_guard<Lock> lock(shards_[idx].lock);
        func(shards_[idx].map[key]);
    }

private:
    struct alignas(64) Shard {
        mutable Lock lock;
        std::unordered_map<Key, T, Hash, KeyEqual> map;
    };

    std::array<Shard, ShardCount> shards_;

    size_type get_shard_index(const Key& key) const { return Hash{}(key) % ShardCount; }
};

} // namespace util

#include "concurrent_map.tpp"