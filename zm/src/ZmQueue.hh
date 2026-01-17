//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// simple fast dynamic-sized queue (supports FIFO and LIFO) for types
// with a sentinel null value (defaults to ZuCmp<T>::null())
// - ZmVHeap-allocated
// - initial size is configurable
// - supports FIFO shift without moving queue contents
// - supports intra-queue deletion for cancel-on-queue (aka abort-on-queue)
// - automatic defragmentation controlled by maxFrag configuration parameter
//   - regular FIFO use cases do not cause fragmentation

#ifndef ZmQueue_HH
#define ZmQueue_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZuTraits.hh>
#include <zlib/ZuArrayFn.hh>
#include <zlib/ZuCmp.hh>

#include <zlib/ZmAssert.hh>
#include <zlib/ZmGuard.hh>
#include <zlib/ZmLock.hh>
#include <zlib/ZmNoLock.hh>
#include <zlib/ZmVHeap.hh>

// defaults
#define ZmQueueMaxFrag		50.0

class ZmQueueParams {
public:
  ZmQueueParams &&initial(unsigned v) { m_initial = v; return ZuMv(*this); }
  ZmQueueParams &&maxFrag(double v) { m_maxFrag = v; return ZuMv(*this); }

  unsigned initial() const { return m_initial; }
  double maxFrag() const { return m_maxFrag; }

private:
  unsigned	m_initial = 0;
  double	m_maxFrag = ZmQueueMaxFrag;
};

// uses NTP (named template parameters):
//
// ZmQueue<ZtString<>,			// ring of ZtString<>s
//   ZmQueueCmp<ZuICmp> >		// case-insensitive comparison

// NTP defaults
struct ZmQueue_Defaults {
  static constexpr auto KeyAxor = ZuDefaultAxor();
  template <typename T> using CmpT = ZuCmp<T>;
  template <typename T> using KeyCmpT = ZuCmp<T>;
  template <typename T> using OpsT = ZuArrayFn<T>;
  using Lock = ZmNoLock;
  struct HeapID : public ZuStringT<"ZmQueue"> { };
  enum { HeapMin = 32 };
  enum { HeapMax = 1024 };
  enum { Sharded = 0 };
};

// ZmQueueKey - key accessor
template <auto KeyAxor_, typename NTP = ZmQueue_Defaults>
struct ZmQueueKey : public NTP {
  static constexpr auto KeyAxor = KeyAxor_;
};

// ZmQueueCmp - the comparator
template <template <typename> class Cmp_, typename NTP = ZmQueue_Defaults>
struct ZmQueueCmp : public NTP {
  template <typename T> using CmpT = Cmp_<T>;
  template <typename T> using OpsT = ZuArrayFn<T, Cmp_<T>>;
};

// ZmQueueKeyCmp - the optional value comparator
template <template <typename> class KeyCmp_, typename NTP = ZmQueue_Defaults>
struct ZmQueueKeyCmp : public NTP {
  template <typename T> using KeyCmpT = KeyCmp_<T>;
};

// ZmQueueLock - the lock type
template <typename Lock_, class NTP = ZmQueue_Defaults>
struct ZmQueueLock : public NTP {
  using Lock = Lock_;
};

// ZmQueueHeapID - the heap ID
template <typename HeapID_, typename NTP = ZmQueue_Defaults>
struct ZmQueueHeapID_ : public NTP {
  using HeapID = HeapID_;
};
template <ZuString HeapID_, typename NTP = ZmQueue_Defaults>
using ZmQueueHeapID = ZmQueueHeapID_<ZuStringT<HeapID_>, NTP>;

// ZmQueueHeapMin - min size of heap allocation
template <unsigned Min_, typename NTP = ZmQueue_Defaults>
struct ZmQueueHeapMin : public NTP {
  enum { HeapMin = Min_ };
};

// ZmQueueHeapMax - max size of heap allocation
template <unsigned Max_, typename NTP = ZmQueue_Defaults>
struct ZmQueueHeapMax : public NTP {
  enum { HeapMax = Max_ };
};

// ZmQueueSharded - sharded heap
template <bool Sharded_, typename NTP = ZmQueue_Defaults>
struct ZmQueueSharded : public NTP {
  enum { Sharded = Sharded_ };
};

// only provide delPtr and findPtr methods to callers of unlocked ZmQueues
// since they are intrinsically not thread-safe

template <typename T, class NTP> class ZmQueue;

template <typename Ring> struct ZmQueue_Unlocked;
template <typename T, class NTP>
struct ZmQueue_Unlocked<ZmQueue<T, NTP> > {
  using Ring = ZmQueue<T, NTP>;

  template <typename P>
  T *findPtr(P &&v) {
    return static_cast<Ring *>(this)->findPtr_(ZuFwd<P>(v));
  }
  void delPtr(T *ptr) {
    return static_cast<Ring *>(this)->delPtr_(ptr);
  }
};

template <typename Ring, class Lock> struct ZmQueue_Base { };
template <typename Ring>
struct ZmQueue_Base<Ring, ZmNoLock> : public ZmQueue_Unlocked<Ring> { };

// derives from Ops so that a ZmQueue includes an *instance* of Ops

template <typename T_, class NTP = ZmQueue_Defaults>
class ZmQueue :
  private ZmVHeap_<
    typename NTP::HeapID,
    sizeof(T_) * NTP::HeapMin,
    sizeof(T_) * NTP::HeapMax,
    alignof(T_),
    NTP::Sharded>,
  public ZmQueue_Base<ZmQueue<T_, NTP>, typename NTP::Lock>,
  public NTP::template OpsT<T_>
{
public:
friend ZmQueue_Unlocked<ZmQueue>;

  using T = T_;
  static constexpr auto KeyAxor = NTP::KeyAxor;
  using Key = ZuRDecay<decltype(KeyAxor(ZuDeclVal<const T &>()))>;
  using Ops = typename NTP::template OpsT<T>;
  using Cmp = typename NTP::template CmpT<T>;
  using KeyCmp = typename NTP::template KeyCmpT<T>;
  using Lock = typename NTP::Lock;
  using HeapID = NTP::HeapID;
  enum { HeapMin = NTP::HeapMin };
  enum { HeapMax = NTP::HeapMax };
  enum { Sharded = NTP::Sharded };
  using VHeap = ZmVHeap_<
    HeapID,
    sizeof(T) * HeapMin,
    sizeof(T) * HeapMax,
    alignof(T),
    Sharded>;

  using Guard = ZmGuard<Lock>;
  using ReadGuard = ZmReadGuard<Lock>;

  ZmQueue(ZmQueueParams params = {}) :
    m_defrag{1.0 - double(params.maxFrag()) / 100.0}
  {
    if (params.initial()) extend(params.initial());
  }

  ~ZmQueue() {
    clean_();
    vfree(m_data);
  }

  ZmQueue(const ZmQueue &) = delete;
  ZmQueue &operator =(const ZmQueue &) = delete;

  ZmQueue(ZmQueue &&ring) noexcept :
      m_defrag{ring.m_defrag} {
    Guard guard(ring.m_lock);
    m_data = ring.m_data;
    m_offset = ring.m_offset;
    m_size = ring.m_size;
    m_length = ring.m_length;
    m_count = ring.m_count;
    ring.m_data = nullptr;
    ring.m_offset = 0;
    ring.m_size = 0;
    ring.m_length = 0;
    ring.m_count = 0;
  }
  ZmQueue &operator =(ZmQueue &&ring) noexcept {
    this->~ZmQueue();
    new (this) ZmQueue{ZuMv(ring)};
  }

  unsigned maxFrag() const {
    return unsigned((1.0 - m_defrag) * 100.0);
  }

  unsigned size() const { ReadGuard guard(m_lock); return m_size; }
  unsigned length() const { ReadGuard guard(m_lock); return m_length; }
  // unsigned count() const { ReadGuard guard(m_lock); return m_count; }
  unsigned size_() const { return m_size; }
  unsigned length_() const { return m_length; }
  unsigned count_() const { return m_count; }
  unsigned offset_() const { return m_offset; }

private:
  using VHeap::valloc;
  using VHeap::vfree;

  void clean_() {
    if (!m_data) return;
    unsigned o = m_offset + m_length;
    if (o > m_size) {
      int n = m_size - m_offset;
      Ops::destroyElems(m_data + m_offset, n);
      Ops::destroyElems(m_data, o - m_size);
    } else {
      Ops::destroyElems(m_data + m_offset, m_length);
    }
  }

  void extend(unsigned size) {
    T *data = static_cast<T *>(valloc(size * sizeof(T)));
    ZmAssert(data);
    if (ZuUnlikely(!data)) throw std::bad_alloc{};
    if (m_data) {
      unsigned o = m_offset + m_length;
      if (o > m_size) {
	int n = m_size - m_offset;
	Ops::moveElems(data, m_data + m_offset, n);
	Ops::moveElems(data + n, m_data, o - m_size);
      } else {
	Ops::moveElems(data, m_data + m_offset, m_length);
      }
      vfree(m_data);
    }
    m_data = data, m_size = size, m_offset = 0;
  }

public:
  void init(ZmQueueParams params = ZmQueueParams()) {
    Guard guard(m_lock);
    if (params.initial() > m_size) extend(params.initial());
    m_defrag = 1.0 - double(params.maxFrag()) / 100.0;
  }

  void clean() {
    Guard guard(m_lock);
    clean_();
    m_offset = m_length = m_count = 0;
  }

private:
  void push() {
    if (m_count >= m_size) {
      auto n = m_count + 1;
      extend(ZmGrow(m_size * sizeof(T), n * sizeof(T)) / sizeof(T));
    }
  }
  unsigned offset(unsigned i) {
    if ((i += m_offset) >= m_size) i -= m_size;
    return i;
  }

public:
  template <typename P>
  void push(P &&v) {
    Guard guard(m_lock);

    push();
    Ops::initElem(m_data + offset(m_length++), ZuFwd<P>(v));
    m_count++;
  }

  T pop() {
    Guard guard(m_lock);

    if (m_count <= 0) return Cmp::null();
    --m_count;
    unsigned o = offset(--m_length);
    T v = ZuMv(m_data[o]);
    Ops::destroyElem(m_data + o);
    {
      int i = m_length;
      while (--i >= 0) {
	o = offset(i);
	if (!Cmp::null(m_data[o])) break;
	Ops::destroyElem(m_data + o);
      }
      m_length = ++i;
    }
    return v;
  }

  template <typename P>
  void unshift(P &&v) {
    Guard guard(m_lock);

    push();
    unsigned o = offset(m_size - 1);
    m_offset = o, m_length++;
    Ops::initElem(m_data + o, ZuFwd<P>(v));
    m_count++;
  }

  T shift() {
    Guard guard(m_lock);

    if (m_count <= 0) return Cmp::null();
    --m_count;
    int i = m_length, j = 0, o = m_offset;
    T v = ZuMv(m_data[o]);
    do {
      Ops::destroyElem(m_data + o);
    } while (--i > 0 && Cmp::null(m_data[o = offset(++j)]));
    m_offset = o, m_length = i;
    return v;
  }

  T head() {
    Guard guard(m_lock);

    if (m_length <= 0) return Cmp::null();
    return m_data[m_offset];
  }
  T tail() {
    Guard guard(m_lock);

    if (m_length <= 0) return Cmp::null();
    return m_data[offset(m_length - 1)];
  }

  template <typename P>
  T find(const P &v) {
    Guard guard(m_lock);
    for (int i = m_length; --i >= 0; ) {
      unsigned o = offset(i);
      if (KeyCmp::equals(KeyAxor(m_data[o]), v)) return m_data[o];
    }
    return Cmp::null();
  }

private:
  template <typename P>
  T *findPtr_(const P &v) {
    for (int i = m_length; --i >= 0; ) {
      unsigned o = offset(i);
      if (Cmp::equals(m_data[o], v)) return &m_data[o];
    }
    return nullptr;
  }

public:
  template <typename P>
  T del(const P &v) {
    Guard guard(m_lock);
    T *ptr = findPtr_(v);
    if (!ptr) return Cmp::null();
    T data = ZuMv(*ptr);
    delPtr_(ptr);
    return data;
  }

private:
  void delPtr_(T *ptr) {
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4244)
#endif
    int i = ptr - m_data;
#ifdef _MSC_VER
#pragma warning(pop)
#endif

    m_count--;

    int o = offset(m_length - 1);
    if (i == o) {
      Ops::destroyElem(ptr);
      i = --m_length;
      while (--i >= 0) {
	if (!Cmp::null(m_data[o = offset(i)])) break;
	Ops::destroyElem(m_data + o);
      }
      m_length = ++i;
    } else if (i == int(m_offset)) {
      Ops::destroyElem(ptr);
      ++m_offset, --m_length, i = -1;
      while (++i < int(m_length)) {
	if (!Cmp::null(m_data[o = offset(i)])) break;
	Ops::destroyElem(m_data + o);
      }
      if (--i > 0) m_offset = offset(i), m_length -= i;
    } else {
      *ptr = Cmp::null();
      if (double(m_count) < double(m_length) * m_defrag) {
	i = m_length - 1;
	while (--i >= 0) {
	  if (Cmp::null(m_data[offset(i)])) {
	    int j = i;
	    while (--j >= 0 && Cmp::null(m_data[offset(j)])); ++i, ++j;
	    for (int n = j; n < i; n++)
	      Ops::destroyElem(m_data + offset(n));
	    for (int n = m_length - i; --n >= 0; i++, j++) {
	      Ops::initElem(m_data + offset(j), ZuMv(m_data[offset(i)]));
	      Ops::destroyElem(m_data + offset(i));
	    }
	    m_length -= (i - j);
	    i = j;
	  }
	}
      }
    }
  }

  class Iter_;
friend Iter_;
  class Iter_ : private Guard {
  protected:
    using Ring = ZmQueue<T, NTP>;

    Iter_(Ring &ring, unsigned i) :
	Guard(ring.m_lock), m_ring(ring), m_i(i) { }

    Iter_(const Iter_ &) = delete;
    Iter_ &operator =(const Iter_ &) = delete;

    Ring	&m_ring;
    int		m_i;
  };
public:
  class Iter;
friend Iter;
  class Iter : private Iter_ {
    using Iter_::m_ring;
    using Iter_::m_i;
  public:
    Iter(typename Iter_::Ring &ring) : Iter_(ring, 0) { }
    T *ptr() {
      unsigned o;
      do {
	if (m_i >= m_ring.m_length) return nullptr;
	o = m_ring.offset(m_i++);
      } while (Cmp::null(m_ring.m_data[o]));
      return m_ring.m_data + o;
    }
    const T &operator ()() {
      unsigned o;
      do {
	if (m_i >= (int)m_ring.m_length) return ZuNullRef<T, Cmp>();
	o = m_ring.offset(m_i++);
      } while (Cmp::null(m_ring.m_data[o]));
      return m_ring.m_data[o];
    }
  };
  auto iter() { return Iter{*this}; }
  class RevIter;
friend RevIter;
  class RevIter : private Iter_ {
    using Iter_::m_ring;
    using Iter_::m_i;
  public:
    RevIter(typename Iter_::Ring &ring) :
	Iter_(ring, ring.m_length) { }
    T *ptr() {
      unsigned o;
      do {
	if (m_i <= 0) return nullptr;
	o = m_ring.offset(--m_i);
      } while (Cmp::null(m_ring.m_data[o]));
      return m_ring.m_data + o;
    }
    const T &operator ()() {
      unsigned o;
      do {
	if (m_i <= 0) return ZuNullRef<T, Cmp>();
	o = m_ring.offset(--m_i);
      } while (Cmp::null(m_ring.m_data[o]));
      return m_ring.m_data[o];
    }
  };
  auto revIter() { return RevIter{*this}; }

private:
  Lock		m_lock;
    T		  *m_data = nullptr;
    unsigned	  m_offset = 0;
    unsigned	  m_size = 0;
    unsigned	  m_length = 0;
    unsigned	  m_count = 0;
    double	  m_defrag;
};

#endif /* ZmQueue_HH */
