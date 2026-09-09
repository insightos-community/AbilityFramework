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

namespace util {

template <typename Key, typename T, std::size_t SyncIntervalMs>
DoubleBufferedMap<Key, T, SyncIntervalMs>::DoubleBufferedMap() {
    read_snp_ = std::make_shared<const map_type>();
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now().time_since_epoch()
    )
                      .count();
    last_sync_ms_.store(now_ms, std::memory_order_relaxed);

    if constexpr (SyncIntervalMs > 0) {
        sync_thread_ = std::thread([this]() {
            while (!stop_thread_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(SyncIntervalMs));
                if (stop_thread_) { break; }
                maybe_sync();
            }
        });
    }
}

template <typename Key, typename T, std::size_t SyncIntervalMs>
DoubleBufferedMap<Key, T, SyncIntervalMs>::~DoubleBufferedMap() {
    stop_thread_ = true;
    if (sync_thread_.joinable()) { sync_thread_.join(); }
}

template <typename Key, typename T, std::size_t SyncIntervalMs>
std::optional<T> DoubleBufferedMap<Key, T, SyncIntervalMs>::find(const Key& key) const {
    auto ptr = std::atomic_load(&read_snp_);
    auto it = ptr->find(key);
    if (it != ptr->end()) { return it->second; }
    return std::nullopt;
}

template <typename Key, typename T, std::size_t SyncIntervalMs>
bool DoubleBufferedMap<Key, T, SyncIntervalMs>::contains(const Key& key) const {
    auto ptr = std::atomic_load(&read_snp_);
    return ptr->find(key) != ptr->end();
}

template <typename Key, typename T, std::size_t SyncIntervalMs>
T DoubleBufferedMap<Key, T, SyncIntervalMs>::operator[](const Key& key) const {
    return at(key);
}

template <typename Key, typename T, std::size_t SyncIntervalMs>
T DoubleBufferedMap<Key, T, SyncIntervalMs>::at(const Key& key) const {
    auto ptr = std::atomic_load(&read_snp_);
    auto it = ptr->find(key);
    if (it == ptr->end()) { throw std::out_of_range("key not found"); }
    return it->second;
}

template <typename Key, typename T, std::size_t SyncIntervalMs>
void DoubleBufferedMap<Key, T, SyncIntervalMs>::insert_or_assign(const Key& key, const T& value) {
    {
        std::lock_guard<std::mutex> lock(write_mutex_);
        write_buf_[key] = value;
    }
    maybe_sync();
}

template <typename Key, typename T, std::size_t SyncIntervalMs>
void DoubleBufferedMap<Key, T, SyncIntervalMs>::insert_or_assign(const Key& key, T&& value) {
    {
        std::lock_guard<std::mutex> lock(write_mutex_);
        write_buf_[key] = std::move(value);
    }
    maybe_sync();
}

template <typename Key, typename T, std::size_t SyncIntervalMs>
template <typename... Args>
bool DoubleBufferedMap<Key, T, SyncIntervalMs>::emplace(const Key& key, Args&&... args) {
    bool inserted = false;
    {
        std::lock_guard<std::mutex> lock(write_mutex_);
        auto it = write_buf_.find(key);
        if (it == write_buf_.end()) {
            write_buf_.emplace(key, std::forward<Args>(args)...);
            inserted = true;
        }
    }
    if (inserted) { maybe_sync(); }
    return inserted;
}

template <typename Key, typename T, std::size_t SyncIntervalMs>
void DoubleBufferedMap<Key, T, SyncIntervalMs>::erase(const Key& key) {
    {
        std::lock_guard<std::mutex> lock(write_mutex_);
        write_buf_.erase(key);
    }
    maybe_sync();
}

template <typename Key, typename T, std::size_t SyncIntervalMs>
void DoubleBufferedMap<Key, T, SyncIntervalMs>::clear() {
    {
        std::lock_guard<std::mutex> lock(write_mutex_);
        write_buf_.clear();
    }
    maybe_sync();
}

template <typename Key, typename T, std::size_t SyncIntervalMs>
auto DoubleBufferedMap<Key, T, SyncIntervalMs>::size() const -> size_type {
    auto ptr = std::atomic_load(&read_snp_);
    return ptr->size();
}

template <typename Key, typename T, std::size_t SyncIntervalMs>
bool DoubleBufferedMap<Key, T, SyncIntervalMs>::empty() const {
    auto ptr = std::atomic_load(&read_snp_);
    return ptr->empty();
}

template <typename Key, typename T, std::size_t SyncIntervalMs>
template <typename F>
void DoubleBufferedMap<Key, T, SyncIntervalMs>::for_each(F&& func) const {
    auto ptr = std::atomic_load(&read_snp_);
    for (const auto& kv : *ptr) {
        func(kv.first, kv.second);
    }
}

template <typename Key, typename T, std::size_t SyncIntervalMs>
template <typename F>
void DoubleBufferedMap<Key, T, SyncIntervalMs>::exec_on_key(const Key& key, F&& func) {
    {
        std::lock_guard<std::mutex> lock(write_mutex_);
        func(write_buf_[key]);
    }
    maybe_sync();
}

template <typename Key, typename T, std::size_t SyncIntervalMs>
void DoubleBufferedMap<Key, T, SyncIntervalMs>::sync() {
    std::lock_guard<std::mutex> guard(sync_mutex_);
    sync_locked();
}

template <typename Key, typename T, std::size_t SyncIntervalMs>
void DoubleBufferedMap<Key, T, SyncIntervalMs>::maybe_sync() {
    if constexpr (SyncIntervalMs == 0) { return; }
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now().time_since_epoch()
    )
                      .count();
    auto last = last_sync_ms_.load(std::memory_order_relaxed);

    // Check interval
    if (now_ms - last < static_cast<long long>(SyncIntervalMs)) { return; }

    // Try lock to avoid contention on sync
    if (!sync_mutex_.try_lock()) { return; }
    std::lock_guard<std::mutex> guard(sync_mutex_, std::adopt_lock);

    // Check again
    last = last_sync_ms_.load(std::memory_order_relaxed);
    if (now_ms - last < static_cast<long long>(SyncIntervalMs)) { return; }

    sync_locked();
    last_sync_ms_.store(now_ms, std::memory_order_relaxed);
}

template <typename Key, typename T, std::size_t SyncIntervalMs>
void DoubleBufferedMap<Key, T, SyncIntervalMs>::sync_locked() {
    // Under sync_mutex_
    std::shared_ptr<const map_type> new_snp;
    {
        std::lock_guard<std::mutex> lock(write_mutex_);
        // Create a copy of the write buffer
        // Note: write_buf_ always contains the latest state.
        // We create a new shared_ptr pointing to a COPY of write_buf_.
        new_snp = std::make_shared<const map_type>(write_buf_);
    }
    // Update read snapshot atomically
    std::atomic_store(&read_snp_, std::move(new_snp));
}

} // namespace util
