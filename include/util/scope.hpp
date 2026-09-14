#pragma once
#include <climits>
#include <version>

// <experimental/scope> -*- C++ -*-

// Copyright The GNU Toolchain Authors.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 3, or (at your option)
// any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// Under Section 7 of GPL version 3, you are granted additional
// permissions described in the GCC Runtime Library Exception, version
// 3.1, as published by the Free Software Foundation.

// You should have received a copy of the GNU General Public License and
// a copy of the GCC Runtime Library Exception along with this program;
// see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
// <http://www.gnu.org/licenses/>.

/** @file experimental/scope
 *  This is a TS C++ Library header.
 *  @ingroup libfund-ts
 */

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>
#include <concepts>
#include <exception> // uncaught_exceptions

namespace draft_scope {
using namespace std;

template <typename Tp, typename Up>
concept not_same_as = !same_as<Tp, Up>;

template <typename Tp>
concept not_lvalue_ref = !is_lvalue_reference_v<Tp>;

template <typename Ef>
class [[nodiscard]] scope_exit {
public:
    template <typename Efp>
        requires not_same_as<remove_cvref_t<Efp>, scope_exit> && constructible_from<Ef, Efp>
    [[nodiscard]] explicit scope_exit(Efp&& _f) noexcept(is_nothrow_constructible_v<Ef, Efp&>)
#ifdef __cpp_exceptions
        try
#endif
        : M_exit_function(_f) {
    }
#ifdef __cpp_exceptions
    catch (...) {
        _f();
    }
#endif

    template <typename Efp>
        requires not_same_as<remove_cvref_t<Efp>, scope_exit> && constructible_from<Ef, Efp>
              && not_lvalue_ref<Efp> && is_nothrow_constructible_v<Ef, Efp>
    explicit scope_exit(Efp&& _f) noexcept
        : M_exit_function(std::forward<Efp>(_f)) {}

    scope_exit(scope_exit&& _rhs) noexcept
        requires is_nothrow_move_constructible_v<Ef>
        : M_exit_function(std::forward<Ef>(_rhs.M_exit_function)) {
        _rhs.release();
    }

    scope_exit(scope_exit&& _rhs) noexcept(is_nothrow_copy_constructible_v<Ef>)
        requires(!is_nothrow_move_constructible_v<Ef>) && is_copy_constructible_v<Ef>
        : M_exit_function(_rhs.M_exit_function) {
        _rhs.release();
    }

    scope_exit(const scope_exit&) = delete;
    scope_exit& operator=(const scope_exit&) = delete;
    scope_exit& operator=(scope_exit&&) = delete;

    ~scope_exit() noexcept {
        if (M_execute_on_destruction) { M_exit_function(); }
    }

    void release() noexcept { M_execute_on_destruction = false; }

private:
    [[no_unique_address]] Ef M_exit_function;
    bool M_execute_on_destruction = true;
};

template <typename Ef>
scope_exit(Ef) -> scope_exit<Ef>;

template <typename Ef>
class [[nodiscard]] scope_fail {
public:
    template <typename Efp>
        requires not_same_as<remove_cvref_t<Efp>, scope_fail> && constructible_from<Ef, Efp>
    explicit scope_fail(Efp&& _f) noexcept(is_nothrow_constructible_v<Ef, Efp&>)
#ifdef __cpp_exceptions
        try
#endif
        : exit_function(_f) {
    }
#ifdef __cpp_exceptions
    catch (...) {
        _f();
    }
#endif

    template <typename Efp>
        requires not_same_as<remove_cvref_t<Efp>, scope_fail> && constructible_from<Ef, Efp>
              && not_lvalue_ref<Efp> && is_nothrow_constructible_v<Ef, Efp>
    explicit scope_fail(Efp&& _f) noexcept
        : exit_function(std::forward<Efp>(_f)) {}

    scope_fail(scope_fail&& _rhs) noexcept
        requires is_nothrow_move_constructible_v<Ef>
        : exit_function(std::forward<Ef>(_rhs.exit_function)) {
        _rhs.release();
    }

    scope_fail(scope_fail&& _rhs) noexcept(is_nothrow_copy_constructible_v<Ef>)
        requires(!is_nothrow_move_constructible_v<Ef>) && is_copy_constructible_v<Ef>
        : exit_function(_rhs.exit_function) {
        _rhs.release();
    }

    scope_fail(const scope_fail&) = delete;
    scope_fail& operator=(const scope_fail&) = delete;
    scope_fail& operator=(scope_fail&&) = delete;

    ~scope_fail() noexcept {
        if (std::uncaught_exceptions() > M_uncaught_init) { exit_function(); }
    }

    void release() noexcept { M_uncaught_init = INT_MAX; }

private:
    [[no_unique_address]] Ef exit_function;
    int M_uncaught_init = std::uncaught_exceptions();
};

template <typename Ef>
scope_fail(Ef) -> scope_fail<Ef>;

template <typename Ef>
class [[nodiscard]] scope_success {
public:
    template <typename Efp>
        requires not_same_as<remove_cvref_t<Efp>, scope_success> && constructible_from<Ef, Efp>
    explicit scope_success(Efp&& _f) noexcept(is_nothrow_constructible_v<Ef, Efp&>)
        : exit_function(_f) {}

    template <typename Efp>
        requires not_same_as<remove_cvref_t<Efp>, scope_success> && constructible_from<Ef, Efp>
              && not_lvalue_ref<Efp> && is_nothrow_constructible_v<Ef, Efp>
    explicit scope_success(Efp&& _f) noexcept
        : exit_function(std::forward<Efp>(_f)) {}

    scope_success(scope_success&& _rhs) noexcept
        requires is_nothrow_move_constructible_v<Ef>
        : exit_function(std::forward<Ef>(_rhs.exit_function)) {
        _rhs.release();
    }

    scope_success(scope_success&& _rhs) noexcept(is_nothrow_copy_constructible_v<Ef>)
        requires(!is_nothrow_move_constructible_v<Ef>) && is_copy_constructible_v<Ef>
        : exit_function(_rhs.exit_function) {
        _rhs.release();
    }

    scope_success(const scope_success&) = delete;
    scope_success& operator=(const scope_success&) = delete;
    scope_success& operator=(scope_success&&) = delete;

    ~scope_success() noexcept(noexcept(this->exit_function())) {
        if (std::uncaught_exceptions() <= M_uncaught_init) { exit_function(); }
    }

    void release() noexcept { M_uncaught_init = -INT_MAX; }

private:
    [[no_unique_address]] Ef exit_function;
    int M_uncaught_init = std::uncaught_exceptions();
};

template <typename Ef>
scope_success(Ef) -> scope_success<Ef>;

template <typename Resrc, typename Del>
class [[nodiscard]] unique_resource {
    static_assert(!is_rvalue_reference_v<Resrc>);
    static_assert(!is_reference_v<Del>);

    struct Dummy {
        constexpr void release() {}
    };

    template <typename Tp>
    struct Wrap {
        template <typename Up>
            requires is_constructible_v<Tp, Up>
        Wrap(Up&&) noexcept(is_nothrow_constructible_v<Tp, Up>);

        template <typename Up, typename Del2>
            requires is_constructible_v<Tp, Up>
        Wrap(Up&& _r, Del2&& _d) noexcept(is_nothrow_constructible_v<Tp, Up>)
            : M_t(std::forward<Up>(_r)) {
            _d.release();
        }

        Wrap() = default;

        Wrap(Wrap&&) = default;

        Wrap(Wrap&& _rhs) noexcept(is_nothrow_constructible_v<Tp, Tp&>)
            requires(!is_nothrow_move_constructible_v<Tp>)
            : M_t(_rhs.M_t) {}

        Wrap& operator=(const Wrap&) = default;

        Wrap& operator=(Wrap&&) = default;

        constexpr Tp& get() noexcept { return M_t; }
        constexpr const Tp& get() const noexcept { return M_t; }

        [[no_unique_address]] Tp M_t{};
    };

    template <typename _Tp>
    struct Wrap<_Tp&> {
        template <typename _Up>
            requires is_constructible_v<reference_wrapper<_Tp>, _Up>
        Wrap(_Up&&) noexcept(is_nothrow_constructible_v<reference_wrapper<_Tp>, _Up>);

        template <typename _Up, typename _Del2>
        Wrap(
            _Up&& __r, _Del2&& __d
        ) noexcept(is_nothrow_constructible_v<reference_wrapper<_Tp>, _Up>)
            : _M_p(std::addressof(static_cast<_Tp&>(__r))) {
            __d.release();
        }

        Wrap() = delete;

        Wrap(const Wrap&) = default;

        Wrap& operator=(const Wrap&) = default;

        _Tp& get() noexcept { return *_M_p; }
        const _Tp& get() const noexcept { return *_M_p; }

        _Tp* _M_p = nullptr;
    };

    using _Res1 = Wrap<Resrc>;

    template <typename _Tp, typename _Up>
        requires is_constructible_v<_Tp, _Up>
                  && (is_nothrow_constructible_v<_Tp, _Up> || is_constructible_v<_Tp, _Up&>)
    using _Fwd_t = std::conditional_t<is_nothrow_constructible_v<_Tp, _Up>, _Up, _Up&>;

    template <typename _Tp, typename _Up>
    static constexpr _Fwd_t<_Tp, _Up> _S_fwd(_Up& __u) {
        return static_cast<_Fwd_t<_Tp, _Up>&&>(__u);
    }

    template <typename _Tp, typename _Up, typename _Del2, typename _Res2>
    static constexpr auto _S_guard(_Del2& __d, _Res2& __r) {
        if constexpr (is_nothrow_constructible_v<_Tp, _Up>)
            return Dummy{};
        else
            return scope_fail{[&] { __d(__r); }};
    }

public:
    unique_resource() = default;

    template <typename _Res2, typename _Del2>
        requires requires {
            typename _Fwd_t<_Res1, _Res2>;
            typename _Fwd_t<Del, _Del2>;
        }
    unique_resource(_Res2&& __r, _Del2&& __d) noexcept(
        (is_nothrow_constructible_v<_Res1, _Res2> || is_nothrow_constructible_v<_Res1, _Res2&>)
        && (is_nothrow_constructible_v<Del, _Del2> || is_nothrow_constructible_v<Del, _Del2&>)
    )
        : _M_res(_S_fwd<_Res1, _Res2>(__r), _S_guard<_Res1, _Res2>(__d, __r))
        , _M_del(_S_fwd<Del, _Del2>(__d), _S_guard<Del, _Del2>(__d, _M_res.get()))
        , _M_exec_on_reset(true) {}

    unique_resource(unique_resource&& __rhs) noexcept
        requires is_nothrow_move_constructible_v<_Res1> && is_nothrow_move_constructible_v<Del>
        : _M_res(std::move(__rhs._M_res))
        , _M_del(std::move(__rhs._M_del))
        , _M_exec_on_reset(std::exchange(__rhs._M_exec_on_reset, false)) {}

    unique_resource(unique_resource&& __rhs)
        requires is_nothrow_move_constructible_v<_Res1> && (!is_nothrow_move_constructible_v<Del>)
        : _M_res(std::move(__rhs._M_res))
        , _M_del(_S_fwd<Del, Del>(__rhs._M_del.get()), scope_fail([&] {
                     if (__rhs._M_exec_on_reset) {
                         __rhs._M_del.get()(_M_res.get());
                         __rhs.release();
                     }
                 }))
        , _M_exec_on_reset(std::exchange(__rhs._M_exec_on_reset, false)) {}

    unique_resource(unique_resource&& __rhs)
        requires(!is_nothrow_move_constructible_v<_Res1>)
        : unique_resource(__rhs._M_res.get(), __rhs._M_del.get(), Dummy{}) {
        if (__rhs._M_exec_on_reset) {
            _M_exec_on_reset = true;
            __rhs._M_exec_on_reset = false;
        }
    }

    // 3.3.3.3, Destructor
    ~unique_resource() { reset(); }

    // 3.3.3.4, Assignment
    unique_resource& operator=(unique_resource&& __rhs
    ) noexcept(is_nothrow_move_assignable_v<_Res1> && is_nothrow_move_assignable_v<Del>) {
        reset();
        if constexpr (is_nothrow_move_assignable_v<_Res1>) {
            if constexpr (is_nothrow_move_assignable_v<Del>) {
                _M_res = std::move(__rhs._M_res);
                _M_del = std::move(__rhs._M_del);
            }
            else {
                _M_del = __rhs._M_del;
                _M_res = std::move(__rhs._M_res);
            }
        }
        else {
            if constexpr (is_nothrow_move_assignable_v<Del>) {
                _M_res = __rhs._M_res;
                _M_del = std::move(__rhs._M_del);
            }
            else {
                _M_res = __rhs._M_res;
                _M_del = __rhs._M_del;
            }
        }
        _M_exec_on_reset = std::exchange(__rhs._M_exec_on_reset, false);
        return *this;
    }

    // 3.3.3.5, Other member functions
    void reset() noexcept {
        if (_M_exec_on_reset) {
            _M_exec_on_reset = false;
            _M_del.get()(_M_res.get());
        }
    }

    template <typename _Res2>
    void reset(_Res2&& __r) {
        reset();
        if constexpr (is_nothrow_assignable_v<_Res1&, _Res2>)
            _M_res.get() = std::forward<_Res2>(__r);
        else
            _M_res.get() = const_cast<const remove_reference_t<_Res2>&>(__r);
        _M_exec_on_reset = true;
    }

    void release() noexcept { _M_exec_on_reset = false; }

    const Resrc& get() const noexcept { return _M_res.get(); }

    add_lvalue_reference_t<remove_pointer_t<Resrc>> operator*() const noexcept
        requires is_pointer_v<Resrc> && (!is_void_v<remove_pointer_t<Resrc>>)
    {
        return *get();
    }

    Resrc operator->() const noexcept
        requires is_pointer_v<Resrc>
    {
        return _M_res.get();
    }

    const Del& get_deleter() const noexcept { return _M_del.get(); }

private:
    [[no_unique_address]] _Res1 _M_res{};
    [[no_unique_address]] Wrap<Del> _M_del{};
    bool _M_exec_on_reset = false;

    template <typename _Res2, typename _Del2, typename _St>
    friend unique_resource<decay_t<_Res2>, decay_t<_Del2>>
    make_unique_resource_checked(_Res2&&, const _St&, _Del2&&) noexcept(is_nothrow_constructible_v<decay_t<_Res2>, _Res2> && is_nothrow_constructible_v<decay_t<_Del2>, _Del2>);

    template <typename _Res2, typename _Del2>
    unique_resource(
        _Res2&& __r, _Del2&& __d, Dummy __noop
    ) noexcept(is_nothrow_constructible_v<Resrc, _Res2> && is_nothrow_constructible_v<Del, _Del2>)
        : _M_res(std::forward<_Res2>(__r), __noop)
        , _M_del(std::forward<Del>(__d), __noop) {}
};

template <typename _Resrc, typename _Del>
unique_resource(_Resrc, _Del) -> unique_resource<_Resrc, _Del>;

template <typename _Resrc, typename _Del, typename _St = decay_t<_Resrc>>
unique_resource<decay_t<_Resrc>, decay_t<_Del>> make_unique_resource_checked(
    _Resrc&& __r, const _St& __invalid, _Del&& __d
) noexcept(is_nothrow_constructible_v<decay_t<_Resrc>, _Resrc> && is_nothrow_constructible_v<decay_t<_Del>, _Del>) {
    if (__r == __invalid) return {std::forward<_Resrc>(__r), std::forward<_Del>(__d), {}};
    return {std::forward<_Resrc>(__r), std::forward<_Del>(__d)};
}

} // namespace draft_scope
using draft_scope::scope_exit;
using draft_scope::scope_fail;
using draft_scope::scope_success;
using draft_scope::unique_resource;
