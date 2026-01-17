//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// atomic operations

#ifndef ZmAtomic_HH
#define ZmAtomic_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#ifdef _MSC_VER
#include <intrin.h>
#pragma warning(push)
#pragma warning(disable:4996 4311 4312)
#endif

#include <zlib/ZuInt.hh>
#include <zlib/ZuTraits.hh>
#include <zlib/ZuAssert.hh>

// Memory Barriers
// ---------------
// gcc (including MinGW-w64) -
//   >= 4.7 - use atomic builtins
// Visual Studio -
//   >= 2013 - use atomic intrinsics
#ifdef __GNUC__
#define ZmAtomic_load(ptr) __atomic_load_n(ptr, __ATOMIC_RELAXED)
#define ZmAtomic_store(ptr, val) __atomic_store_n(ptr, val, __ATOMIC_RELAXED)
#define ZmAtomic_acquire() __atomic_thread_fence(__ATOMIC_ACQUIRE)
#define ZmAtomic_release() __atomic_thread_fence(__ATOMIC_RELEASE)
#endif

#ifdef _MSC_VER
#include <atomic>
#define ZmAtomic_load(ptr) \
  (reinterpret_cast<const std::atomic<decltype(*ptr)> *>(ptr)->load( \
	std::memory_order_relaxed))
#define ZmAtomic_store(ptr, val) \
  (reinterpret_cast<std::atomic<decltype(*ptr)> *>(ptr)->store( \
	val, std::memory_order_relaxed))
#define ZmAtomic_acquire() std::atomic_thread_fence(std::memory_order_acquire)
#define ZmAtomic_release() std::atomic_thread_fence(std::memory_order_release)
#endif

// Atomic Operations (compare and exchange, etc.)
// -----------------
// gcc or clang - use sync builtins
// Visual Studio - old-school inline assembler
#ifdef __GNUC__
#define ZmAtomic_GccBuiltins32
#if !defined(__i386__) || defined(__x86_64__)
#define ZmAtomic_GccBuiltins64
#define ZmAtomic_GccBuiltins128
#endif
#endif

template <typename Int, int Size> struct ZmAtomicOps;

// 32bit atomic operations
template <typename Int32> struct ZmAtomicOps<Int32, 4> {
  using S = int32_t;
  using U = uint32_t;

  static Int32 load_(const Int32 *ptr) {
    return ZmAtomic_load(ptr);
  }
  static Int32 load(const Int32 *ptr) {
    Int32 i = ZmAtomic_load(ptr);
    ZmAtomic_acquire();
    return i;
  }
  static void store_(Int32 *ptr, Int32 value) {
    ZmAtomic_store(ptr, value);
  }
  static void store(Int32 *ptr, Int32 value) {
    ZmAtomic_release();
    ZmAtomic_store(ptr, value);
  }

#if defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
  static Int32 atomicXch(volatile Int32 *ptr, Int32 value) {
#ifdef ZmAtomic_GccBuiltins32
    return __sync_lock_test_and_set(ptr, value);
#else
    Int32 old;

    __asm__ __volatile__(	"xchgl %0, %1"
				: "=r" (old), "=m" (*ptr)
				: "0" (value), "m" (*ptr));
    return old;
#endif
  }

  static Int32 atomicXchAdd(volatile Int32 *ptr, Int32 value) {
#ifdef ZmAtomic_GccBuiltins32
    return __sync_fetch_and_add(ptr, value);
#else
    Int32 old;

    __asm__ __volatile__(	"lock; xaddl %0, %1"
				: "=r" (old), "=m" (*ptr)
				: "0" (value), "m" (*ptr));
    return old;
#endif
  }

  static Int32 atomicCmpXch(
	volatile Int32 *ptr, Int32 value, Int32 cmp) {
#ifdef ZmAtomic_GccBuiltins32
    return __sync_val_compare_and_swap(ptr, cmp, value);
#else
    Int32 old;

    __asm__ __volatile__(	"lock; cmpxchgl %2, %1"
				: "=a" (old), "=m" (*ptr)
				: "r" (value), "m" (*ptr), "0" (cmp) : "cc");
    return old;
#endif
  }
#endif /* GNUC && (i386 || x86_64) */

#if defined(_WIN32) && defined(_MSC_VER)
  static Int32 atomicXch(volatile Int32 *ptr, Int32 value) {
    return _InterlockedExchange(reinterpret_cast<volatile long *>(ptr), value);
  }

  static Int32 atomicCmpXch(
      volatile Int32 *ptr, Int32 value, Int32 cmp) {
    return _InterlockedCompareExchange(
	reinterpret_cast<volatile long *>(ptr), value, cmp);
  }

  static Int32 atomicXchAdd(volatile Int32 *ptr, int32_t value) {
    return _InterlockedExchangeAdd(
	reinterpret_cast<volatile long *>(ptr), value);
  }
#endif /* _WIN32 && _MSC_VER */
};

// 64bit atomic operations
template <typename Int64> struct ZmAtomicOps<Int64, 8> {
  using S = int64_t;
  using U = uint64_t;

  static Int64 load_(const Int64 *ptr) {
    return ZmAtomic_load(ptr);
  }
  static Int64 load(const Int64 *ptr) {
    Int64 i = ZmAtomic_load(ptr);
    ZmAtomic_acquire();
    return i;
  }
  static void store_(Int64 *ptr, Int64 value) {
    ZmAtomic_store(ptr, value);
  }
  static void store(Int64 *ptr, Int64 value) {
    ZmAtomic_release();
    ZmAtomic_store(ptr, value);
  }

#if defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
  static Int64 atomicXch(volatile Int64 *ptr, Int64 value) {
#ifdef ZmAtomic_GccBuiltins64
    return __sync_lock_test_and_set(ptr, value);
#else
    Int64 old;

#ifdef __x86_64__
    __asm__ __volatile__(	"xchgq %0, %1"
				: "=q" (old), "=m" (*ptr)
				: "0" (value), "m" (*ptr));
#else
    do { old = *ptr; } while (atomicCmpXch(ptr, value, old) != old);
#endif
    return old;
#endif
  }

  static Int64 atomicXchAdd(volatile Int64 *ptr, Int64 value) {
#ifdef ZmAtomic_GccBuiltins64
    return __sync_fetch_and_add(ptr, value);
#else
    Int64 old;

#ifdef __x86_64__
    __asm__ __volatile__(	"lock; xaddq %0, %1"
				: "=q" (old), "=m" (*ptr)
				: "0" (value), "m" (*ptr));
#else
    do { old = *ptr; } while (atomicCmpXch(ptr, old + value, old) != old);
#endif
    return old;
#endif
  }

  static Int64 atomicCmpXch(
      volatile Int64 *ptr, Int64 value, Int64 cmp) {
#ifdef ZmAtomic_GccBuiltins64
    return __sync_val_compare_and_swap(ptr, cmp, value);
#else
    Int64 old;

#ifdef __x86_64__
    __asm__ __volatile__(	"lock; cmpxchgq %2, %1"
				: "=a" (old), "=m" (*ptr)
				: "r" (value), "m" (*ptr), "0" (cmp) : "cc");
#else
#if __PIC__
    __asm__ __volatile__(	"pushl %%ebx;"
				"movl %2, %%ebx;"
				"lock; cmpxchg8b %1;"
				"pop %%ebx"
				: "=A" (old), "=m" (*ptr)
				: "r" ((uint32_t)value),
				  "c" ((uint32_t)(value>>32)),
				  "m" (*ptr), "0" (cmp) : "cc");
#else
    __asm__ __volatile__(	"lock; cmpxchg8b %1"
				: "=A" (old), "=m" (*ptr)
				: "b" ((uint32_t)value),
				  "c" ((uint32_t)(value>>32)),
				  "m" (*ptr), "0" (cmp) : "cc");
#endif
#endif
    return old;
#endif
  }
#endif /* GNUC && (i386 || x86_64) */

#if defined(_WIN32) && defined(_MSC_VER)
#ifdef _WIN64
  static Int64 atomicXch(volatile Int64 *ptr, Int64 value) {
    return _InterlockedExchange64(
	reinterpret_cast<volatile long long *>(ptr), value);
  }

  static Int64 atomicXchAdd(volatile Int64 *ptr, int64_t value) {
    return _InterlockedExchangeAdd64(
	reinterpret_cast<volatile long long *>(ptr), value);
  }
#else
  static Int64 atomicXch(volatile Int64 *ptr, Int64 value) {
    Int64 old;

    do {
      old = *ptr;
    } while (_InterlockedCompareExchange64(
	  reinterpret_cast<volatile long long *>(ptr), old, old) != old);
    return old;
  }

  static Int64 atomicXchAdd(volatile Int64 *ptr, int64_t value) {
    Int64 old;

    do {
      old = *ptr;
    } while (_InterlockedCompareExchange64(
	  reinterpret_cast<volatile long long *>(ptr), old + value, old)
	!= old);
    return old;
  }
#endif

  static Int64 atomicCmpXch(volatile Int64 *ptr, Int64 value, Int64 cmp) {
    return _InterlockedCompareExchange64(
	reinterpret_cast<volatile long long *>(ptr), value, cmp);
  }
#endif /* _WIN32 && _MSC_VER */
};

// 128bit atomic operations
#ifdef ZmAtomic_GccBuiltins128
#pragma GCC diagnostic push
#ifdef __llvm__
#pragma GCC diagnostic ignored "-Watomic-alignment"
#endif
template <typename Int128> struct ZmAtomicOps<Int128, 16> {
  using S = int128_t;
  using U = uint128_t;

  static Int128 load_(const Int128 *ptr) {
    return ZmAtomic_load(ptr);
  }
  static Int128 load(const Int128 *ptr) {
    Int128 i = ZmAtomic_load(ptr);
    ZmAtomic_acquire();
    return i;
  }
  static void store_(Int128 *ptr, Int128 value) {
    ZmAtomic_store(ptr, value);
  }
  static void store(Int128 *ptr, Int128 value) {
    ZmAtomic_release();
    ZmAtomic_store(ptr, value);
  }

  static Int128 atomicXch(volatile Int128 *ptr, Int128 value) {
    return __sync_lock_test_and_set(ptr, value);
  }

  static Int128 atomicXchAdd(volatile Int128 *ptr, Int128 value) {
    return __sync_fetch_and_add(ptr, value);
  }

  static Int128 atomicCmpXch(
      volatile Int128 *ptr, Int128 value, Int128 cmp) {
    return __sync_val_compare_and_swap(ptr, cmp, value);
  }
};
#pragma GCC diagnostic pop
#endif /* ZmAtomic_GccBuiltins128 */

template <typename T> class ZmAtomic {
  ZuAssert(ZuTraits<T>::IsPrimitive && ZuTraits<T>::IsIntegral);

public:
  using Ops = ZmAtomicOps<T, sizeof(T)>;

private:
  using S = typename Ops::S;
  using U = typename Ops::U;

public:
  ZmAtomic() : m_val{0} { };

  // store/relaxed when first creating new objects
  ZmAtomic(const ZmAtomic &a) {
    Ops::store_(&m_val, Ops::load(&a.m_val));
  };
  ZmAtomic(T val) {
    Ops::store_(&m_val, val);
  };

  // store/release (release before store)
  ZmAtomic &operator =(const ZmAtomic &a) {
    Ops::store(&m_val, Ops::load(&a.m_val));
    return *this;
  }
  ZmAtomic &operator =(T val) {
    Ops::store(&m_val, val);
    return *this;
  }

  // store/relaxed
  void store_(T val) { Ops::store_(&m_val, val); }

  // load/acquire (acquire after load)
  operator T() const { return Ops::load(&m_val); }

  // load/relaxed
  T load_() const { return Ops::load_(&m_val); }

  T xch(T val) { return Ops::atomicXch(&m_val, val); }
  T xchAdd(T val) { return Ops::atomicXchAdd(&m_val, val); }
  T xchSub(T val) { return Ops::atomicXchAdd(&m_val, -val); }
  T cmpXch(T val, T cmp) {
    return Ops::atomicCmpXch(&m_val, val, cmp);
  }

  T operator ++() { return Ops::atomicXchAdd(&m_val, 1) + 1; }
  T operator --() { return Ops::atomicXchAdd(&m_val, -1) - 1; }

  T operator ++(int) { return Ops::atomicXchAdd(&m_val, 1); }
  T operator --(int) { return Ops::atomicXchAdd(&m_val, -1); }

  T operator +=(S val) {
    return Ops::atomicXchAdd(&m_val, val) + val;
  }
  T operator -=(S val) {
    return Ops::atomicXchAdd(&m_val, -val) - val;
  }

  T xchOr(T val) {
    T old;
    do {
      if (((old = m_val) & val) == val) return old;
    } while (Ops::atomicCmpXch(&m_val, old | val, old) != old);
    return old;
  }
  T xchAnd(T val) {
    T old;
    do {
      if (!((old = m_val) & ~val)) return old;
    } while (Ops::atomicCmpXch(&m_val, old & val, old) != old);
    return old;
  }

  T operator |=(T val) { return xchOr(val) | val; }
  T operator &=(T val) { return xchAnd(val) & val; }

  T minimum(T val) {
    T old;
    do {
      if ((old = m_val) <= val) return old;
    } while (Ops::atomicCmpXch(&m_val, val, old) != old);
    return val;
  }
  T maximum(T val) {
    T old;
    do {
      if ((old = m_val) >= val) return old;
    } while (Ops::atomicCmpXch(&m_val, val, old) != old);
    return val;
  }

private:
  T	m_val;
};

template <typename T> class ZmAtomic<T *> {
public:
  using Ops = ZmAtomicOps<uintptr_t, sizeof(T *)>;

private:
  using S = typename Ops::S;
  using U = typename Ops::U;

public:
  ZmAtomic() : m_val{0} { }
 
  // store/relaxed when first creating new objects
  ZmAtomic(const ZmAtomic &a) {
    Ops::store_(&m_val, Ops::load(&a.m_val));
  }
  ZmAtomic(T *val) {
    Ops::store_(&m_val, reinterpret_cast<uintptr_t>(val));
  }

  // store/release (release before store)
  ZmAtomic &operator =(const ZmAtomic &a) {
    Ops::store(&m_val, Ops::load(&a.m_val));
    return *this;
  }
  ZmAtomic &operator =(T *val) {
    Ops::store(&m_val, reinterpret_cast<uintptr_t>(val));
    return *this;
  }

  // store/relaxed
  void store_(T *val) { Ops::store_(&m_val, reinterpret_cast<U>(val)); }

  // load/acquire (acquire after load)
  operator T *() const { return reinterpret_cast<T *>(Ops::load(&m_val)); }
  T *operator ->() const { return reinterpret_cast<T *>(Ops::load(&m_val)); }

  // load/relaxed
  T *load_() const { return reinterpret_cast<T *>(Ops::load_(&m_val)); }

  T *xch(T *val) {
    return reinterpret_cast<T *>(Ops::atomicXch(&m_val,
	  reinterpret_cast<U>(val)));
  }
  T *xchAdd(S val) {
    return reinterpret_cast<T *>(Ops::atomicXchAdd(&m_val, val));
  }
  T *cmpXch(T *val, T *cmp) {
    return reinterpret_cast<T *>(Ops::atomicCmpXch(&m_val,
	reinterpret_cast<U>(val), reinterpret_cast<U>(cmp)));
  }

  T *operator ++() {
    return reinterpret_cast<T *>(
      Ops::atomicXchAdd(&m_val, sizeof(T))) + sizeof(T);
  }
  T *operator --() {
    return reinterpret_cast<T *>(
      Ops::atomicXchAdd(&m_val, -sizeof(T))) - sizeof(T);
  }

  T *operator ++(int) {
    return reinterpret_cast<T *>(Ops::atomicXchAdd(&m_val, sizeof(T)));
  }
  T *operator --(int) {
    return reinterpret_cast<T *>(Ops::atomicXchAdd(&m_val, -sizeof(T)));
  }

  T *operator +=(S val) {
    val *= sizeof(T);
    return reinterpret_cast<T *>(Ops::atomicXchAdd(&m_val, val)) + val;
  }
  T *operator -=(S val) {
    val *= sizeof(T);
    return reinterpret_cast<T *>(Ops::atomicXchAdd(&m_val, -val)) - val;
  }

private:
  uintptr_t	m_val;
};

template <typename T_> struct ZuTraits<ZmAtomic<T_> > : public ZuTraits<T_> {
  enum { IsPrimitive = 0 };
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif /* ZmAtomic_HH */
