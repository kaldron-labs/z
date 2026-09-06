//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z library core header
// - extended capabilities not present in STL (e.g. ZuFwdLike, ZuUnder, etc.)
// - natural camel-case naming not constrained by STL tech debt
// - avoids dragging in STL cruft

#ifndef ZuLib_HH
#define ZuLib_HH

#if !defined(Z_VMAJOR) || !defined(Z_VMINOR) || !defined(Z_VPATCH)
#error "define Z_VMAJOR, Z_VMINOR and Z_VPATCH"
#endif
#if Z_VMINOR > 99
#error "Z_VMINOR > 99"
#endif
#if Z_VPATCH > 999
#error "Z_VPATCH > 999"
#endif
#include <zlib/ZuPP.hh>
#define Z_VERSION ((Z_VMAJOR * 100000) + (Z_VMINOR * 1000) + Z_VPATCH)
#define Z_VERNAME ZuPP_Eval( \
  ZuPP_Defer(ZuPP_Q)(Z_VMAJOR) "." \
  ZuPP_Defer(ZuPP_Q)(Z_VMINOR) "." \
  ZuPP_Defer(ZuPP_Q)(Z_VPATCH))

#include <assert.h>

// Windows cruft
#ifdef _WIN32

#ifndef WINVER
#define WINVER 0x0800
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0800
#endif

#ifndef _WIN32_DCOM
#define _WIN32_DCOM
#endif

#ifndef _WIN32_WINDOWS
#define _WIN32_WINDOWS 0x0800
#endif

#ifndef _WIN32_IE
#define _WIN32_IE 0x0700
#endif

#ifndef __MSVCRT_VERSION__
#define __MSVCRT_VERSION__ 0x0A00
#endif

#ifndef UNICODE
#define UNICODE
#endif

#ifndef _UNICODE
#define _UNICODE
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>

#define ZuExport_API __declspec(dllexport)
#define ZuExport_Explicit
#define ZuImport_API __declspec(dllimport)
#define ZuImport_Explicit extern

#ifdef ZU_EXPORTS
#define ZuAPI ZuExport_API
#define ZuExplicit ZuExport_Explicit
#else
#define ZuAPI ZuImport_API
#define ZuExplicit ZuImport_Explicit
#endif
#define ZuExtern extern ZuAPI

#else /* _WIN32 */

#define ZuAPI
#define ZuExplicit
#define ZuExtern extern

#endif /* _WIN32 */

// sanity check platform
#include <limits.h>
#if CHAR_BIT != 8
#error "Broken platform - CHAR_BIT is not 8 - a byte is not 8 bits!"
#endif
#if UINT_MAX < 0xffffffff
#error "Broken platform - UINT_MAX < 0xffffffff - int < 32 bits!"
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#ifndef _WIN32
#include <string.h>
#endif

static_assert(sizeof(int) == sizeof(unsigned));
static_assert(sizeof(int) >= sizeof(int32_t));
static_assert(sizeof(unsigned) >= sizeof(uint32_t));

// clear secret data without dead-store elimination
inline void ZuClear(void *ptr, size_t size) noexcept
{
#ifndef _WIN32
  ::explicit_bzero(ptr, size);
#else
  ::SecureZeroMemory(ptr, size);
#endif
}

#ifdef __GNUC__

#define ZuLikely(x) __builtin_expect(!!(x), 1)
#define ZuUnlikely(x) __builtin_expect(!!(x), 0)

#ifdef ZDEBUG
#define ZuInline inline
#define ZuNoInline inline
#else
#define ZuInline inline __attribute__((always_inline))
#define ZuNoInline inline __attribute__((noinline))
#endif

#ifdef ZDEBUG
#define ZuUnreachable() do { ::abort(); __builtin_unreachable(); } while (0)
#else
#define ZuUnreachable() __builtin_unreachable()
#endif

#else /* __GNUC__ */

#define ZuLikely(x) (x)
#define ZuUnlikely(x) (x)

#ifdef _MSC_VER
#define ZuInline __forceinline
#else
#define ZuInline inline
#endif
#define ZuNoInline

#ifdef _MSC_VER
#define ZuUnreachable() __assume(false)
#endif

#endif /* __GNUC__ */

#if defined(linux) || defined(__mips64)
#include <endian.h>
#if __BYTE_ORDER == __BIG_ENDIAN
#define Zu_BIGENDIAN 1
#else
#define Zu_BIGENDIAN 0
#endif
#else
#ifdef _WIN32
#define Zu_BIGENDIAN 0
#endif
#endif

// alternative to std::launder
template <class T>
constexpr T *ZuLaunder(T *p) noexcept {
#ifdef __GNUC__
  return __builtin_launder(p);
#else
  return p;
#endif
}

// alternative to std::remove_reference
template <typename T_>
struct ZuDeref_ { using T = T_; };
template <typename T_>
struct ZuDeref_<T_ &> { using T = T_; };
template <typename T_>
struct ZuDeref_<const T_ &> { using T = const T_; };
template <typename T_>
struct ZuDeref_<volatile T_ &> { using T = volatile T_; };
template <typename T_>
struct ZuDeref_<const volatile T_ &> { using T = const volatile T_; };
template <typename T_>
struct ZuDeref_<T_ &&> { using T = T_; };
template <typename T>
using ZuDeref = typename ZuDeref_<T>::T;

// alternative to std::remove_cv (strip qualifiers)
template <typename T_>
struct ZuStrip_ { using T = T_; };
template <typename T_>
struct ZuStrip_<const T_> { using T = T_; };
template <typename T_>
struct ZuStrip_<volatile T_> { using T = T_; };
template <typename T_>
struct ZuStrip_<const volatile T_> { using T = T_; };
template <typename T>
using ZuStrip = typename ZuStrip_<T>::T;

// alternative to std::decay
template <typename T> using ZuDecay = ZuStrip<ZuDeref<T>>;

// various type mappings used as template parameters
template <typename T> using ZuAsIs = T;
template <typename T> using ZuMkConst = const T;
template <typename T> using ZuMkVolatile = volatile T;
template <typename T> using ZuMkRRef = T &&;
template <typename T> using ZuMkLRef = T &;
template <typename T> using ZuMkCRef = const T &;

// consteval instantiable constants
template <typename T_, T_ V_> struct ZuConstant {
  using T = T_;
  static constexpr T V = V_;
  constexpr operator const T &() const noexcept { return V; }
  constexpr const T &operator()() const noexcept { return V; }
};
template <int I> using ZuInt = ZuConstant<int, I>;
template <unsigned I> using ZuUnsigned = ZuConstant<unsigned, I>;
template <auto B> using ZuBool = ZuConstant<bool, bool(B)>;
using ZuFalse = ZuBool<false>;	// interoperable with std::false_type
using ZuTrue = ZuBool<true>;	// interoperable with std::true_type
template <typename> struct ZuAlwaysTrue : public ZuTrue { };
template <typename> struct ZuAlwaysFalse : public ZuFalse { };

// cv checking
template <typename U> struct ZuIsConst : public ZuFalse { };
template <typename U> struct ZuIsConst<const U> : public ZuTrue { };

template <typename U> struct ZuIsVolatile : public ZuFalse { };
template <typename U> struct ZuIsVolatile<volatile U> : public ZuTrue { };

template <typename U, typename R, bool = ZuIsConst<U>{}>
struct ZuConst_;
template <typename U, typename R>
struct ZuConst_<U, R, true> { using T = R; };
template <typename U, typename R = void>
using ZuConst = typename ZuConst_<U, R>::T;

template <typename U, typename R, bool = !ZuIsConst<U>{}>
struct ZuMutable_;
template <typename U, typename R>
struct ZuMutable_<U, R, true> { using T = R; };
template <typename U, typename R = void>
using ZuMutable = typename ZuMutable_<U, R>::T;

template <typename U, typename R, bool = ZuIsVolatile<U>{}>
struct ZuVolatile_;
template <typename U, typename R>
struct ZuVolatile_<U, R, true> { using T = R; };
template <typename U, typename R = void>
using ZuVolatile = typename ZuVolatile_<U, R>::T;

template <typename U, typename R, bool = !ZuIsVolatile<U>{}>
struct ZuNonVolatile_;
template <typename U, typename R>
struct ZuNonVolatile_<U, R, true> { using T = R; };
template <typename U, typename R = void>
using ZuNonVolatile = typename ZuNonVolatile_<U, R>::T;

// ref checking
template <typename U> struct ZuIsLRef : public ZuFalse { };
template <typename U> struct ZuIsLRef<U &> : public ZuTrue { };

template <typename U> struct ZuIsCRef : public ZuFalse { };
template <typename U> struct ZuIsCRef<const U &> : public ZuTrue { };

template <typename U> struct ZuIsRRef : public ZuFalse { };
template <typename U> struct ZuIsRRef<U &&> : public ZuTrue { };

template <typename U, typename R, bool = ZuIsLRef<U>{}>
struct ZuLRef_;
template <typename U, typename R>
struct ZuLRef_<U, R, true> { using T = R; };
template <typename U, typename R = void>
using ZuLRef = typename ZuLRef_<U, R>::T;

template <typename U, typename R, bool = !ZuIsLRef<U>{}>
struct ZuNotLRef_;
template <typename U, typename R>
struct ZuNotLRef_<U, R, true> { using T = R; };
template <typename U, typename R = void>
using ZuNotLRef = typename ZuNotLRef_<U, R>::T;

template <typename U, typename R, bool = ZuIsCRef<U>{}>
struct ZuCRef_;
template <typename U, typename R>
struct ZuCRef_<U, R, true> { using T = R; };
template <typename U, typename R = void>
using ZuCRef = typename ZuCRef_<U, R>::T;

template <typename U, typename R, bool = !ZuIsCRef<U>{}>
struct ZuNotCRef_;
template <typename U, typename R>
struct ZuNotCRef_<U, R, true> { using T = R; };
template <typename U, typename R = void>
using ZuNotCRef = typename ZuNotCRef_<U, R>::T;

template <typename U, typename R, bool = ZuIsRRef<U>{}>
struct ZuRRef_;
template <typename U, typename R>
struct ZuRRef_<U, R, true> { using T = R; };
template <typename U, typename R = void>
using ZuRRef = typename ZuRRef_<U, R>::T;

template <typename U, typename R, bool = !ZuIsRRef<U>{}>
struct ZuNotRRef_;
template <typename U, typename R>
struct ZuNotRRef_<U, R, true> { using T = R; };
template <typename U, typename R = void>
using ZuNotRRef = typename ZuNotRRef_<U, R>::T;

// shorthand constexpr alternative to std::forward
template <typename T>
constexpr T &&ZuFwd(ZuDeref<T> &v) noexcept { // fwd lvalue
  return static_cast<T &&>(v);
}
template <typename T>
constexpr T &&ZuFwd(ZuDeref<T> &&v) noexcept { // fwd rvalue
  return static_cast<T &&>(v);
}
// shorthand constexpr alternative to std::move
template <typename T>
constexpr ZuDeref<T> &&ZuMv(T &&v) noexcept {
  return static_cast<ZuDeref<T> &&>(v);
}
// ZuMv for types that can be both smart and raw pointers
// - ensures the from pointer is nullptr after the move
template <typename T> struct ZuMvPtr_ {
  static constexpr T &&mv(T &v) noexcept { return ZuMv(v); }
};
template <typename T> struct ZuMvPtr_<T *> {
  static constexpr T *mv(T *&v) noexcept {
    T *p = v;
    v = nullptr;
    return p;
  }
};
template <typename T, typename = ZuMutable<T>>
constexpr decltype(auto) ZuMvPtr(T &v) noexcept {
  return ZuMvPtr_<T>::mv(v);
}
// shorthand std::forward_like, extended for converting the passed parameter
// - ZuFwdLike<decltype(self)>(self.member)
template <typename T, typename V>
constexpr auto &&ZuFwdLike(V &&v) noexcept {
  using U = ZuDeref<V>;
  constexpr bool Const = ZuIsConst<ZuDeref<T>>{};
  if constexpr (ZuIsLRef<T>{}) {
    if constexpr (Const)
      return static_cast<ZuMkConst<U> &>(v);
    else
      return static_cast<U &>(v);
  } else {
    if constexpr (Const)
      return static_cast<const U &&>(static_cast<ZuMkConst<ZuDeref<U>> &>(v));
    else
      return static_cast<U &&>(v);
  }
}
// - ZuFwdLike<decltype(self), U>(self) // casts self to appropriate U
template <typename T, typename U, typename V>	// V should be convertible to U
constexpr auto &&ZuFwdLike(V &&v) noexcept {
  constexpr bool Const = ZuIsConst<ZuDeref<T>>{};
  if constexpr (ZuIsLRef<T>{}) {
    if constexpr (Const)
      return static_cast<ZuMkConst<U> &>(v);
    else
      return static_cast<U &>(v);
  } else {
    if constexpr (Const)
      return static_cast<const U &&>(static_cast<ZuMkConst<ZuDeref<U>> &>(v));
    else
      return static_cast<U &&>(v);
  }
}

// generic RAII guard
template <typename L> struct ZuGuard {
  L	fn;
  bool	cancelled = false;

  ZuGuard(L fn_) noexcept : fn{ZuMv(fn_)} { }
  ~ZuGuard() noexcept { if (!cancelled) fn(); }
  ZuGuard(const ZuGuard &) = delete;
  ZuGuard &operator =(const ZuGuard &) = delete;
  ZuGuard(ZuGuard &&o) noexcept : fn{ZuMv(o.fn)} { o.cancelled = true; }
  ZuGuard &operator =(ZuGuard &&o) noexcept {
    if (this != &o) { this->~ZuGuard(); new (this) ZuGuard{ZuMv(o)}; }
    return *this;
  }

  void cancel() { cancelled = true; }
  void cancel(bool v) { cancelled = v; }
};

// safe bool idiom, given operator !()
#define ZuOpBool \
  operator const void *() const { \
    return !*this ? \
      static_cast<const void *>(nullptr) : \
      static_cast<const void *>(this); \
  }

// compile-time ?:
// - ZuIf<typename B, typename T1, typename T2> evaluates to B ? T1 : T2
template <typename T1, typename T2, bool B> struct ZuIf_;
template <typename T1, typename T2>
struct ZuIf_<T1, T2, true> { using T = T1; };
template <typename T1, typename T2>
struct ZuIf_<T1, T2, false> { using T = T2; };
template <bool B, typename T1, typename T2>
using ZuIf = typename ZuIf_<T1, T2, B>::T;

// alternative to std::enable_if
// - compile-time SFINAE (substitution failure is not an error)
// - ZuIfT<bool B, typename T = void> evaluates to T (default void)
//   if B is true, or is a substitution failure if B is false
template <bool, typename R = void> struct ZuIfT_ { };
template <typename R> struct ZuIfT_<true, R> { using T = R; };
template <bool B, typename R = void>
using ZuIfT = typename ZuIfT_<B, R>::T;

// alternative to std::declval
template <typename U> struct ZuDeclVal__ { using T = U; };
template <typename T> auto ZuDeclVal_(int) -> typename ZuDeclVal__<T&&>::T;
template <typename T> auto ZuDeclVal_(...) -> typename ZuDeclVal__<T>::T;
template <typename U> decltype(ZuDeclVal_<U>(0)) ZuDeclVal() noexcept;

// sizeof(void) and empty-class handling:
// - ZuSize<T>{} is 0 if T is void or an empty class
// - ZuSize<T>{} is sizeof(T) otherwise
template <typename T, bool = __is_empty(T)>
struct ZuSize__ : public ZuUnsigned<sizeof(T)> { };
template <typename T>
struct ZuSize__<T, true> : public ZuUnsigned<0> { };
template <typename T, typename = void>
struct ZuSize_ : public ZuUnsigned<sizeof(T)> { };
template <typename T>
struct ZuSize_<T, decltype(sizeof(T), (int T::*){}, void())> :
public ZuSize__<T> { };
template <typename T> struct ZuSize : public ZuSize_<T> { };
template <> struct ZuSize<void> : public ZuUnsigned<0> { };

// recursive decay (for pair, tuple, union, etc.)
// - example: ZuRDecay<const ZuTuple<const int &> &> decays to ZuTuple<int>
struct ZuDefaultRDecayer {
  template <typename T_> struct Decay { using T = T_; };
};
ZuDefaultRDecayer ZuRDecayer(...);
template <typename T_>
struct ZuRDecay_ {
  using Decayer = decltype(ZuRDecayer(ZuDeclVal<T_ *>()));
  using T = typename Decayer::template Decay<T_>::T;
};
template <typename T>
using ZuRDecay = typename ZuRDecay_<ZuDecay<T>>::T;

// type list (see ZuTL.hh for implementaion)
template <typename ...Ts> struct ZuTypeList;

// type matching - alternative to std::is_same
template <typename U1, typename U2>
struct ZuIsSame : public ZuFalse { };
template <typename U>
struct ZuIsSame<U, U> : public ZuTrue { };
template <typename U1, typename U2, typename R = void>
using ZuSame = ZuIfT<ZuIsSame<U1, U2>{}, R>;
template <typename U1, typename U2, typename R = void>
using ZuNotSame = ZuIfT<!ZuIsSame<U1, U2>{}, R>;

// hierarchical type matching
// - in most use cases From is the derived candidate, To is the base
namespace Zu_ { template <typename To> void is(To *); }
template <typename From, typename To, typename = void>
struct ZuIs_Composite : public ZuFalse { };
template <typename From, typename To>
struct ZuIs_Composite<From, To,
  decltype(Zu_::is<To>(ZuDeclVal<From *>()))>
: public ZuTrue { };
template <typename From, typename To, typename = void>
struct ZuIs_NotSame : public ZuFalse { };
template <typename From, typename To>
struct ZuIs_NotSame<From, To,
  decltype((int From::*){}, (int To::*){}, void())> :
  public ZuIs_Composite<From, To> { };
template <typename From, typename To>
struct ZuIs_Decayed : public ZuIs_NotSame<From, To> { };
template <typename U> struct ZuIs_Decayed<U, U> : public ZuTrue { };
template <typename From, typename To>
struct ZuIs_ : public ZuIs_Decayed<ZuDecay<From>, ZuDecay<To>> { };
template <typename From, typename To, typename R = void>
using ZuIs = ZuIfT<ZuIs_<From, To>{}, R>;
template <typename From, typename To, typename R = void>
using ZuIsNot = ZuIfT<!ZuIs_<From, To>{}, R>;

template <typename From, typename To>
struct ZuIsBase :
  public ZuBool<bool(ZuIs_<From, To>{}) && !ZuIsSame<From, To>{}> { };
template <typename From, typename To, typename R = void>
using ZuBase = ZuIfT<ZuIsBase<From, To>{}, R>;
template <typename From, typename To, typename R = void>
using ZuNotBase = ZuIfT<!ZuIsBase<From, To>{}, R>;

// alternative to std::is_convertible_v
namespace Zu_ { template <typename To> void cvt(To); }
template <typename From, typename To, typename = void>
struct ZuIsConvertible_ : public ZuFalse { };
template <typename From, typename To>
struct ZuIsConvertible_<From, To,
  decltype(Zu_::cvt<To>(ZuDeclVal<From>()), void())> :
    public ZuTrue { };
template <typename From, typename To>
struct ZuIsConvertible : public ZuIsConvertible_<
  ZuIf<bool(ZuIsLRef<From>{} || ZuIsRRef<From>{}), From, From &>,
  ZuIf<bool(ZuIsLRef<To>{} || ZuIsRRef<To>{}), To,
    ZuIf<ZuIsSame<ZuStrip<To>, To>{}, const To &, To &>>> { };
template <>
struct ZuIsConvertible<void, void> : public ZuTrue { };
template <typename From>
struct ZuIsConvertible<From, void> : public ZuFalse { };
template <typename To>
struct ZuIsConvertible<void, To> : public ZuFalse { };

template <typename From, typename To, typename R = void>
using ZuConvertible = ZuIfT<ZuIsConvertible<From, To>{}, R>;
template <typename From, typename To, typename R = void>
using ZuNotConvertible = ZuIfT<!ZuIsConvertible<From, To>{}, R>;

// alternative to std::is_constructible_v
// - the following are intentional non-goals:
//   - distinguishing throw from nothrow
//   - distinguishing move- from copy-constructible
template <typename From, typename To, typename = void>
struct ZuIsConstructible_NonComposite : public ZuFalse { };
template <typename From, typename To>
struct ZuIsConstructible_NonComposite<From, To,
  decltype(To(ZuDeclVal<From>()), void())> : public ZuTrue { };
template <typename ...Args, typename To>
struct ZuIsConstructible_NonComposite<ZuTypeList<Args...>, To,
  decltype(To(ZuDeclVal<Args...>()), void())> : public ZuTrue { };
template <typename From, typename To, typename = void>
struct ZuIsConstructible_Composite : public ZuFalse { };
template <typename From, typename To>
struct ZuIsConstructible_Composite<From, To,
  decltype(::new To(ZuDeclVal<From>()), void())> : public ZuTrue { };
template <
  typename From, typename To, typename To_ = ZuDecay<To>, typename = void>
struct ZuIsConstructible_ :
  public ZuIsConstructible_NonComposite<From, To> { };
template <typename From, typename To, typename To_>
struct ZuIsConstructible_<From, To, To_, decltype((int To_::*){}, void())> :
  public ZuIsConstructible_Composite<From, To_> { };
template <typename From, typename To>
struct ZuIsConstructible : public ZuIsConstructible_<
  ZuIf<bool(ZuIsLRef<From>{} || ZuIsRRef<From>{}), From, From &&>, To> { };
template <>
struct ZuIsConstructible<void, void> : public ZuTrue { };
template <typename From>
struct ZuIsConstructible<From, void> : public ZuFalse { };
template <typename To>
struct ZuIsConstructible<void, To> : public ZuFalse { };

template <typename From, typename To, typename R = void>
using ZuConstructible = ZuIfT<ZuIsConstructible<From, To>{}, R>;
template <typename From, typename To, typename R = void>
using ZuNotConstructible = ZuIfT<!ZuIsConstructible<From, To>{}, R>;

// alternatives to std::is_no_throw_*_v
template <typename T, typename = void>
struct ZuNXConstruct : public ZuTrue { };
template <typename T>
struct ZuNXConstruct<T, decltype((int T::*){}, void())> :
  public ZuBool<noexcept(T())> { };

template <typename T, typename = void>
struct ZuNXMove_ : public ZuFalse { };
template <typename T>
struct ZuNXMove_<T, decltype(T(ZuDeclVal<T &&>()), void())> :
  public ZuBool<noexcept(T(ZuDeclVal<T &&>()))> { };
template <typename T, typename = void>
struct ZuNXMove : public ZuTrue { };
template <typename T>
struct ZuNXMove<T, decltype((int T::*){}, void())> :
  public ZuNXMove_<T> { };

template <typename T, typename = void>
struct ZuNXCopy_ : public ZuFalse { };
template <typename T>
struct ZuNXCopy_<T, decltype(T(ZuDeclVal<const T &>()), void())> :
  public ZuBool<noexcept(T(ZuDeclVal<const T &>()))> { };
template <typename T, typename = void>
struct ZuNXCopy : public ZuTrue { };
template <typename T>
struct ZuNXCopy<T, decltype((int T::*){}, void())> :
  public ZuNXCopy_<T> { };

template <typename T, typename = void>
struct ZuNXDestroy : public ZuTrue { };
template <typename T>
struct ZuNXDestroy<T, decltype((int T::*){}, void())> :
  public ZuBool<noexcept(ZuDeclVal<T &>().~T())> { };

// alloca() alias

#ifdef _MSC_VER
#define ZuAlloca(n, a) _alloca(n) // MSVC should 16-byte align
#else
#ifndef _WIN32
#include <alloca.h>
#endif
#define ZuAlloca(n, a) __builtin_alloca_with_align(n, (a)<<3)
#endif

// default accessor (pass-through)

constexpr auto ZuDefaultAxor() {
  return []<typename T>(T &&v) -> decltype(auto) { return ZuFwd<T>(v); };
}

// self-referential / recursive lambdas
// - example below prints integers from 10 to 0
// ZuLambda{[i = 10](auto &&self) mutable -> void {
//   std::cout << i << '\n';
//   if (--i >= 0) self();
// }}();
// - works around https://gcc.gnu.org/bugzilla/show_bug.cgi?id=113563
template <typename L>
struct ZuLambda {
  L lambda;

  // regrettably, selectively disabling overloads is required
  // for SFINAE determination of ZuLambda mutability, etc.

  template <
    typename L_ = L,
    decltype(ZuDeclVal<L_ &&>()(ZuDeclVal<ZuLambda &&>()), int()) = 0>
  constexpr decltype(auto) operator ()() && { return lambda(ZuMv(*this)); }
  template <
    typename ...Args,
    typename L_ = L,
    decltype(ZuDeclVal<L_ &&>()(
      ZuDeclVal<ZuLambda &&>(),
      ZuDeclVal<Args &&>()...), int()) = 0>
  constexpr decltype(auto) operator ()(Args &&...args) && {
    return lambda(ZuMv(*this), ZuFwd<Args>(args)...);
  }

  template <
    typename L_ = L,
    decltype(ZuDeclVal<L_ &>()(ZuDeclVal<ZuLambda &>()), int()) = 0>
  constexpr decltype(auto) operator ()() & { return lambda(*this); }
  template <
    typename ...Args,
    typename L_ = L,
    decltype(ZuDeclVal<L_ &>()(
      ZuDeclVal<ZuLambda &>(),
      ZuDeclVal<Args &&>()...), int()) = 0>
  constexpr decltype(auto) operator ()(Args &&...args) & {
    return lambda(*this, ZuFwd<Args>(args)...);
  }

  template <
    typename L_ = L,
    decltype(ZuDeclVal<const L_ &>()(ZuDeclVal<const ZuLambda &>()), int()) = 0>
  constexpr decltype(auto) operator ()() const & { return lambda(*this); }
  template <
    typename ...Args,
    typename L_ = L,
    decltype(ZuDeclVal<const L_ &>()(
      ZuDeclVal<const ZuLambda &>(),
      ZuDeclVal<Args &&>()...), int()) = 0>
  constexpr decltype(auto) operator ()(Args &&...args) const & {
    return lambda(*this, ZuFwd<Args>(args)...);
  }
};
template <typename L> ZuLambda(L) -> ZuLambda<L>;

// generic underlying type access for wrapper types with a cast operator
// (used with ZuBox, ZuBigEndian, ZuMArray::Elem, C++11 scoped enums, etc.)
// - ZuUnder<T>
#ifdef __GNUC__
template <typename U, bool = __is_enum(U)>
struct ZuUnder__ {
  using T = U;
};
template <typename U>
struct ZuUnder__<U, true> {
  using T = __underlying_type(U);
};
#else
template <typename U> struct ZuUnder__ { using T = U; };
#endif
struct ZuUnder_AsIs { };
ZuUnder_AsIs ZuUnderType(...);
template <typename U, typename V = decltype(ZuUnderType(ZuDeclVal<U *>()))>
struct ZuUnder_ {
  using T = V;
};
template <typename U>
struct ZuUnder_<U, ZuUnder_AsIs> {
  using T = typename ZuUnder__<U>::T;
};
template <typename T_, T_ V>
struct ZuUnder_<ZuConstant<T_, V>> {
  using T = T_;
};
template <typename U>
using ZuUnder = typename ZuUnder_<ZuDecay<U>>::T;

template <typename U>
auto ZuUnderlying(U &&v) noexcept { return ZuUnder<U>(ZuFwd<U>(v)); }

// generic "void" type for use where plain void cannot be used
struct ZuVoid { };

// alternative to std::is_constant_evaluated()
constexpr bool ZuConstEval() noexcept {
  return __builtin_is_constant_evaluated();
}

template <typename T>
constexpr T *ZuAddr(T &v) noexcept {
#ifdef __GNUC__
  return __builtin_addressof(v);
#endif
}

// argh, std::construct_at is a function name that is specially recognized
// and privileged by the compiler and also declared inline in <memory>;
// it is effectively a kind of reserved word that should have been
// implemented in the core language or with a compiler intrinsic; regrettably
// there's no good option except to #include <memory> (which drags in a bunch
// of STL cruft), but with C++26 placement new becomes eligible for
// consteval and this misbegotten thing can be consigned to history
#include <memory> // LATER - comment out for C++26
template <
  typename T, typename ...Args,
  decltype(T(ZuDeclVal<Args &&>()...), int()) = 0,
  bool NoExcept = noexcept(T(ZuDeclVal<Args &&>()...))>
constexpr auto ZuNew(T *v, Args && ...args) noexcept(NoExcept) {
  return std::construct_at(v, ZuFwd<Args>(args)...); // comment out for C++26
  // return new (v) T(ZuFwd<Args>(args)...); // uncomment for C++26
}

// alternative to std::bit_cast
template <typename To, typename From>
constexpr To ZuCast(const From &from) noexcept {
#ifdef __GNUC__
  return __builtin_bit_cast(To, from);
#endif
}

// type-punning boilerplate
// - same use cases as ZuCast, but with more explicit control
template <typename In, typename Out>
struct ZuPun {
  ZuPun() { }
  ~ZuPun() { }
  ZuPun(const ZuPun &) = delete;
  ZuPun &operator =(const ZuPun &) = delete;
  ZuPun(ZuPun &&) = delete;
  ZuPun &operator =(ZuPun &&) = delete;
  template <typename ...Args>
  ZuPun(Args &&...args) : in(ZuFwd<Args>(args)...) { }
  union {
    In in;
    Out out;
  };
};

// alternative name
#define ZuCanOverlap [[no_unique_address]]

#endif /* ZuLib_HH */
