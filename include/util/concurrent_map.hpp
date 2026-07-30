// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

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
 * @brief double-buffered map
 * read-write separation design, suitable for read-heavy, low-write high-concurrency scenarios.
 * reads are based on a read-only snapshot (lock-free or very low overhead); writes go to the buffer and update the snapshot at sync points.
 *
 * @tparam Key key type
 * @tparam T value type
 * @tparam SyncIntervalMs read/write buffer sync interval (ms, compile-time config)
 * @tparam Hash hash function
 * @tparam KeyEqual key comparison function
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

    /** @brief find element (return copy) */
    std::optional<T> find(const Key& key) const;

    /** @brief check whether exists */
    bool contains(const Key& key) const;

    /**
     * @brief get element (read-only, returns copy)
     * if key does not exist, throws std::out_of_range
     */
    T operator[](const Key& key) const;
    T at(const Key& key) const;

    /** @brief insert or update */
    void insert_or_assign(const Key& key, const T& value);
    void insert_or_assign(const Key& key, T&& value);

    /** @brief emplace */
    template <typename... Args>
    bool emplace(const Key& key, Args&&... args);

    /** @brief deleteelement */
    void erase(const Key& key);

    /** @brief clear container */
    void clear();

    /** @brief get size (snapshot size) */
    size_type size() const;

    /** @brief empty check */
    bool empty() const;

    /** @brief iterate operation (lock-free, based onsnapshot) */
    template <typename F>
    void for_each(F&& func) const;

    /**
     * @brief operation on a specific key (on write buffer)
     * allows user-defined operations on the value corresponding to a Key.
     * note: this operation holds the write lock.
     */
    template <typename F>
    void exec_on_key(const Key& key, F&& func);

    /**
     * @brief double-buffer sync (write buffer -> read buffer)
     */
    void sync();

private:
    using map_type = std::unordered_map<key_type, mapped_type>;

    // write buffer
    mutable std::mutex write_mutex_;
    map_type write_buf_;

    // read snapshot (RCU) - always points to a valid map
    std::shared_ptr<const map_type> read_snp_;

    // auto-sync thread control
    std::mutex sync_mutex_;
    std::atomic<long long> last_sync_ms_{0};
    std::atomic<bool> stop_thread_{false};
    std::thread sync_thread_;

    void maybe_sync();
    void sync_locked();
};

/** @brief spinlock implementation */
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
 * @brief sharded lock Map
 *
 * @tparam Key key type
 * @tparam T value type
 * @tparam ShardCount shard count (static config)
 * @tparam Lock lock type (supports std::mutex, util::SpinLock, etc.)
 * @tparam Hash hash function
 * @tparam KeyEqual key comparison function
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
     * @brief find element (copyreturn)
     * for thread safety, find returns a copy of the value or std::optional
     */
    std::optional<T> find(const Key& key) const {
        size_type idx = get_shard_index(key);
        std::lock_guard<Lock> lock(shards_[idx].lock);
        auto it = shards_[idx].map.find(key);
        if (it != shards_[idx].map.end()) { return it->second; }
        return std::nullopt;
    }

    /** @brief check whether exists */
    bool contains(const Key& key) const {
        size_type idx = get_shard_index(key);
        std::lock_guard<Lock> lock(shards_[idx].lock);
        return shards_[idx].map.find(key) != shards_[idx].map.end();
    }

    /** @brief subscript operation (return copy; insert default if not exists) */
    T operator[](const Key& key) {
        size_type idx = get_shard_index(key);
        std::lock_guard<Lock> lock(shards_[idx].lock);
        return shards_[idx].map[key];
    }

    /** @brief insert or update (Usage: map.insert_or_assign(k, v)) */
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
     * @brief insert (Usage: map.emplace(k, args...))
     * @return true if inserted, false if already exists
     */
    template <typename... Args>
    bool emplace(const Key& key, Args&&... args) {
        size_type idx = get_shard_index(key);
        std::lock_guard<Lock> lock(shards_[idx].lock);
        // unordered_map::emplace does not necessarily check key existence (it inserts if not exists)
        auto it = shards_[idx].map.find(key);
        if (it != shards_[idx].map.end()) { return false; }
        shards_[idx].map.emplace(key, std::forward<Args>(args)...);
        return true;
    }

    /** @brief delete */
    size_type erase(const Key& key) {
        size_type idx = get_shard_index(key);
        std::lock_guard<Lock> lock(shards_[idx].lock);
        return shards_[idx].map.erase(key);
    }

    /** @brief clearall */
    void clear() {
        for (auto& shard : shards_) {
            std::lock_guard<Lock> lock(shard.lock);
            shard.map.clear();
        }
    }

    /** @brief get size (approximate, no global lock) */
    size_type size() const {
        size_type total = 0;
        for (const auto& shard : shards_) {
            std::lock_guard<Lock> lock(shard.lock);
            total += shard.map.size();
        }
        return total;
    }

    /** @brief empty check */
    bool empty() const {
        for (const auto& shard : shards_) {
            std::lock_guard<Lock> lock(shard.lock);
            if (!shard.map.empty()) { return false; }
        }
        return true;
    }

    /**
     * @brief iterate operation
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
     * @brief operation on a specific key (Access with lock)
     * allows user-defined operation on the value for a given key, holding the lock
     * if key does not exist, a default value is created automatically
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