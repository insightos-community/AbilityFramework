#pragma once
#include <iterator>
#include <mutex>

// 参考 https://boost.ac.cn/doc/libs/1_88_0/doc/html/thread/sds.html
// 参考 https://github.com/jrgfogh/synchronized_value
// gcc experimental 中也有实现

namespace draft {
inline namespace _synchronized {

template <typename T, typename Mtx>
class synchronized_guard {
public:
    explicit synchronized_guard(std::unique_lock<Mtx>&& _lk, T* _ptr)
        : lk(std::move(_lk))
        , ptr(_ptr) {}
    synchronized_guard(const synchronized_guard&) = delete;
    synchronized_guard(synchronized_guard&&) = delete;
    T* operator->() & { return ptr; }
    T& operator*() & { return *ptr; }
    T& value() & { return *ptr; }

    const T& value() const& { return *ptr; }
    const T& operator*() const& { return *ptr; }
    const T* operator->() const& { return ptr; }

    const T& operator*() const&& = delete;
    const T* operator->() const&& = delete;
    T& value() const&& = delete;

private:
    std::unique_lock<Mtx> lk;
    T* ptr;
};

template <typename T, typename Mtx>
auto begin(synchronized_guard<T, Mtx>& g) {
    return std::begin(g.value());
}
template <typename T, typename Mtx>
auto end(synchronized_guard<T, Mtx>& g) {
    return std::end(g.value());
}

template <typename T, typename Mtx = std::mutex>
class synchronized_value {
public:
    synchronized_value() = default;
    template <typename... Ts>
    synchronized_value(Ts&&... args)
        : value(std::forward<Ts>(args)...) {}

    synchronized_guard<T, Mtx> lock() {
        return synchronized_guard{std::unique_lock(m), std::addressof(value)};
    };
    synchronized_guard<const T, Mtx> lock() const {
        return synchronized_guard{std::unique_lock(m), std::addressof(value)};
    };
    synchronized_guard<T, Mtx> operator->() { return lock(); }
    synchronized_guard<T, Mtx> operator*() { return lock(); }

private:
    mutable Mtx m;
    T value;
};

} // namespace _synchronized
} // namespace draft
using draft::synchronized_value;
