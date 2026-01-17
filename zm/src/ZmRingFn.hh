//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ZmRingFn encapsulates a generic lambda payload for use with ZmRing
// ring buffers containing variable-sized messages; it optimizes for the
// stateless case while also handling stateful lambdas with captures;
// ZmRingFn is move-only
//
// ZmRingFn(L &l) stores a pointer to an on-stack lambda instance together
// with function pointers that invoke it, move it, allocate a copy of it
// and free it; while the lambda remains in scope, the ZmRingFn instance
// references the lambda instance on-stack without copying it
//
// in the fast path, no heap allocation or freeing is performed during
// pushing of the lambda onto a ring buffer, shifting it, and invoking it
//
// ZmRingFn move assignment ensures that the lambda becomes heap-allocated;
// this extends its scope and enables deferred execution (timeouts, etc.) -
// the ZmRingFn scope is extended beyond the scope of the original on-stack
// lambda reference that it was constructed with
//
// pushSize() returns the message byte size needed to store the
// lambda in a ring buffer together with its invocation function
//
// push() moves the lambda into a ring buffer together with its
// invocation function
//
// invoke() invokes the lambda directly in-ring from the ring buffer pointer,
// destroys it and returns the size of the message so that a ring buffer
// dequeue can then complete correctly

// ZmHeap is used for heap allocation

#ifndef ZmRingFn_HH
#define ZmRingFn_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZuLambdaTraits.hh>

#include <zlib/ZmHeap.hh>

// NTP (named template parameters):
//
// ZmRingFn<ZuVStream &,			// parameters
//   ZmRingFnHeapID<"HeapID">>			// heap ID

// NTP defaults
struct ZmRingFn_Defaults {
  struct HeapID : public ZuStringT<"ZmRingFn"> { };
  enum { Sharded = false };
};

// ZmRingFnHeapID - the heap ID
template <typename HeapID_, typename NTP = ZmRingFn_Defaults>
struct ZmRingFnHeapID_ : public NTP {
  using HeapID = HeapID_;
};
template <ZuString HeapID, typename NTP = ZmRingFn_Defaults>
using ZmRingFnHeapID = ZmRingFnHeapID_<ZuStringT<HeapID>, NTP>;

// ZmRingFnSharded - sharded heap
template <bool Sharded_, typename NTP = ZmRingFn_Defaults>
struct ZmRingFnSharded : public NTP {
  enum { Sharded = Sharded_ };
};

// run-time encapsulation of generic function/lambda
template <typename NTP = ZmRingFn_Defaults, typename ...Args>
class ZmRingFn_ {
  // 64bit pointer packing - uses bit 63 to indicate on-heap
  static constexpr uintptr_t OnHeap = (static_cast<uintptr_t>(1)<<63);

  typedef unsigned (*InvokeFn)(void *ptr, Args...);
  typedef void (*MoveFn)(void *dst, void *src, bool onHeap);
  typedef uintptr_t (*AllocFn)(uintptr_t ptr);

  using HeapID = NTP::HeapID;
  enum { Sharded = NTP::Sharded };

public:
  ZmRingFn_() = default;

  ZmRingFn_(const ZmRingFn_ &) = delete;
  ZmRingFn_ &operator =(const ZmRingFn_ &) = delete;

  ZmRingFn_(ZmRingFn_ &&fn) :
      m_invokeFn{fn.m_invokeFn}, m_moveFn{fn.m_moveFn}, m_allocFn{fn.m_allocFn},
      m_ptr{fn.m_ptr} {
    fn.clear();
    heapAlloc();
  }
  ZmRingFn_ &operator =(ZmRingFn_ &&fn) {
    this->~ZmRingFn_();
    new (this) ZmRingFn_{ZuMv(fn)};
    return *this;
  }

  template <
    typename L,
    decltype(ZuStatelessLambda<L, ZuTypeList<Args...>>(), int()) = 0>
  ZmRingFn_(L &l) : 
      m_invokeFn{[](void *, Args... args) -> unsigned {
	try {
	  ZuInvokeLambda<L, ZuTypeList<Args...>>(ZuFwd<Args>(args)...);
	} catch (...) { }
	return 0;
      }},
      m_moveFn{nullptr},
      m_allocFn{[](uintptr_t) -> uintptr_t { return 0; }},
      m_ptr{0} { }

  template <
    typename L,
    decltype(ZuNotStatelessLambda<L, ZuTypeList<Args...>>(), int()) = 0>
  ZmRingFn_(L &l) :
      m_invokeFn{[](void *ptr_, Args... args) -> unsigned {
	auto ptr = static_cast<L *>(ptr_);
	try { (*ptr)(ZuFwd<Args>(args)...); } catch (...) { }
	ptr->~L();
	return sizeof(L);
      }},
      m_moveFn{[](void *dst, void *src_, bool onHeap) {
	using Cache = ZmHeapCacheT<HeapID, sizeof(L), alignof(L), Sharded>;
	auto src = static_cast<L *>(src_);
	new (dst) L{ZuMv(*src)};
	src->~L();
	if (ZuUnlikely(onHeap)) Cache::free(src);
      }},
      m_allocFn{[](uintptr_t ptr_) -> uintptr_t {
	using Cache = ZmHeapCacheT<HeapID, sizeof(L), alignof(L), Sharded>;
	// 0 - return sizeof(L) - used in fast path
	if (ZuLikely(!ptr_)) return sizeof(L);
	// 1 - heap allocation - slow path
	if (ZuLikely(ptr_ == 1))
	  return reinterpret_cast<uintptr_t>(Cache::alloc());
	// * - heap free (unless on stack)
	auto ptr = reinterpret_cast<L *>(ptr_);
	ptr->~L();
	Cache::free(ptr);
	return 0;
      }},
      m_ptr{reinterpret_cast<uintptr_t>(&l)} { }

  template <typename L>
  ZmRingFn_ &operator =(L l) {
    this->~ZmRingFn_();
    new (this) ZmRingFn_{l};
    heapAlloc();
    return *this;
  }

  ~ZmRingFn_() {
    if (ZuUnlikely(m_invokeFn && (m_ptr & OnHeap)))
      m_allocFn(m_ptr & ~OnHeap); // destroys and frees
  }

  bool operator !() const { return !m_invokeFn; }
  ZuOpBool

  // ring push()
 
  unsigned pushSize() const {
    return sizeof(InvokeFn) + m_allocFn(0); // m_allocFn(0) returns sizeof(L)
  }
  void push(void *dst_) {
    auto dst = reinterpret_cast<InvokeFn *>(dst_);
    *dst = m_invokeFn;
    if (ZuUnlikely(m_ptr)) m_moveFn(&dst[1], ptr(), onHeap());
    clear();
  }

  // ring shift() - invokes lambda and returns size

  template <typename ...Args_>
  ZuInline static unsigned invoke(void *ptr_, Args_ &&... args) {
    auto ptr = reinterpret_cast<InvokeFn *>(ptr_);
    return
      (**ptr)(static_cast<void *>(&ptr[1]), ZuFwd<Args_>(args)...) +
      sizeof(InvokeFn);
  }

private:
  void *ptr() const { return reinterpret_cast<void *>(m_ptr & ~OnHeap); }

  bool onHeap() const { return m_ptr & OnHeap; }

  void clear() { m_invokeFn = nullptr; }

  void heapAlloc() {
    if (ZuLikely(onHeap())) return; // idempotent
    if (auto stackPtr = ptr()) {
      auto heapPtr = reinterpret_cast<void *>(m_allocFn(1)); // heap allocates
      if (ZuUnlikely(!heapPtr)) throw std::bad_alloc{};
      m_moveFn(heapPtr, stackPtr, false);
      m_ptr = reinterpret_cast<uintptr_t>(heapPtr) | OnHeap;
    }
  }

private:
  InvokeFn	m_invokeFn = nullptr; // invoke lambda, destroy it, return size
  MoveFn	m_moveFn = nullptr;   // move lambda
  AllocFn	m_allocFn = nullptr;  // size+alloc+free (overloaded function)
  uintptr_t	m_ptr = 0;            // pointer to lambda instance
};

// permit use of an optional trailing NTP (named template parameter)
template <typename Args, unsigned N = Args::N>
struct ZmRingFn_MapArgs {
  // distinguish NTP from function signature
  static constexpr ZuFalse match(...);
  static constexpr ZuTrue match(ZmRingFn_Defaults *);

  using T =
    ZuIf<!N, ZmRingFn_<ZmRingFn_Defaults>,
      ZuIf<!decltype(match(ZuDeclVal<ZuType<N - 1, Args> *>())){},
	ZuTypeApply<ZmRingFn_,
	  typename Args::template Unshift<ZmRingFn_Defaults>>,
	ZuTypeApply<ZmRingFn_,
	  typename ZuTypeHead<N - 1, Args>::template Unshift<
	    ZuType<N - 1, Args>>>>>;
};

template <typename ...Args>
using ZmRingFn = typename ZmRingFn_MapArgs<ZuTypeList<Args...>>::T;

#endif /* ZmRingFn_HH */
