//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// heap-allocated move-only array
// - simple and small wrapper around {pointer, length}
// - no custom allocator (use ZtArray with ZmVHeap)
// - primarily intended for use in startup configuration, etc.
// - not intended for use in latency-sensitive code paths

#ifndef ZuMvArray_HH
#define ZuMvArray_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <initializer_list>

#include <zlib/ZuSpan.hh>
#include <zlib/ZuArrayFn.hh>
#include <zlib/ZuPrint.hh>

template <typename T> struct ZuMvArray_ { };
template <> struct ZuMvArray_<char> {
  friend ZuPrintString ZuPrintType(ZuMvArray_ *);
};

template <typename T_>
class ZuMvArray : public ZuMvArray_<ZuStrip<T_>>, public ZuArrayFn<T_> {
  ZuMvArray(const ZuMvArray &) = delete;
  ZuMvArray &operator =(const ZuMvArray &) = delete;

public:
  using T = T_;
  using Cmp = ZuCmp<T>;

  ZuMvArray() = default;

  ZuMvArray(std::initializer_list<T> a_) : m_length{a_.size()},
    m_data{!a_.size() ?
      static_cast<T *>(nullptr) :
      static_cast<T *>(::malloc(a_.size() * sizeof(T)))}
  {
    auto n = a_.size();
    if (ZuUnlikely(n && !m_data)) throw std::bad_alloc();
    this->copyElems(m_data, a_.begin(), n);
  }
  ZuMvArray &operator =(std::initializer_list<T> a_) {
    this->~ZuMvArray();
    new (this) ZuMvArray(a_);
    return *this;
  }

  ZuMvArray(uint64_t n) :
    m_length{n},
    m_data{!n ?
      static_cast<T *>(nullptr) :
      static_cast<T *>(::malloc(n * sizeof(T)))}
  {
    if (ZuUnlikely(n && !m_data)) throw std::bad_alloc();
    this->initElems(m_data, n);
  }
  ZuMvArray(T *data, uint64_t n) :
    m_length{n},
    m_data{!n ?
      static_cast<T *>(nullptr) :
      static_cast<T *>(::malloc(n * sizeof(T)))}
  {
    if (ZuUnlikely(n && !m_data)) throw std::bad_alloc();
    this->moveElems(m_data, data, n);
  }
  ~ZuMvArray() noexcept(ZuNXDestroy<T>{}) {
    if (!m_data) return;
    this->destroyElems(m_data, m_length);
    ::free(m_data);
  }

  ZuMvArray(ZuMvArray &&a) noexcept : m_length{a.m_length}, m_data{a.m_data} {
    a.m_length = 0;
    a.m_data = nullptr;
  }
  ZuMvArray &operator =(ZuMvArray &&a) noexcept {
    m_length = a.m_length;
    m_data = a.m_data;
    a.m_length = 0;
    a.m_data = nullptr;
    return *this;
  }

// array/ptr operators
  T &operator [](uint64_t i) { return m_data[i]; }
  const T &operator [](uint64_t i) const { return m_data[i]; }

// accessors
  uint64_t length() const { return m_length; }

  T *data() { return m_data; }
  const T *data() const { return m_data; }

// release / free
  T *release() && {
    auto ptr = m_data;
    m_length = 0;
    m_data = nullptr;
    return ptr;
  }
  void free(const T *ptr) { ::free(ptr); }

// reset to null
  void null() { ::free(m_data); m_length = 0;  m_data = nullptr;}

// set length
  void length(uint64_t newLength) {
    T *oldData = m_data;
    uint64_t oldLength = m_length;
    m_length = newLength;
    m_data = static_cast<T *>(::malloc(newLength * sizeof(T)));
    uint64_t mvLength = oldLength < newLength ? oldLength : newLength;
    uint64_t initLength = newLength - mvLength;
    if (mvLength) this->moveElems(m_data, oldData, mvLength);
    if (initLength) this->initElems(m_data + mvLength, initLength);
    ::free(oldData);
  }

// iteration
  template <bool Mutable = false, typename L>
  ZuIfT<!Mutable> all(L &&l) const {
    for (uint64_t i = 0, n = m_length; i < n; i++) ZuFwd<L>(l)(m_data[i]);
  }
  template <bool Mutable, typename L>
  ZuIfT<Mutable> all(L &&l) {
    for (uint64_t i = 0, n = m_length; i < n; i++) ZuFwd<L>(l)(m_data[i]);
  }

// buffer access
  auto span() { return ZuSpan(data(), length()); }
  auto cspan() const { return ZuSpan(data(), length()); }

// comparison
  bool operator !() const { return !m_length; }
  ZuOpBool

  bool same(const ZuMvArray &a) const { return this == &a; }
  template <typename A> constexpr bool same(const A &) const { return false; }

  template <typename A>
  bool equals(const A &a) const {
    return same(a) || cspan().equals(a);
  }
  template <typename A>
  int cmp(const A &a) const {
    if (same(a)) return 0;
    return cspan().cmp(a);
  }
  template <typename L, typename R>
  friend inline ZuIfT<ZuIs_<L, ZuMvArray>{}, bool>
  operator ==(const L &l, const R &r) { return l.equals(r); }
  template <typename L, typename R>
  friend inline ZuIfT<ZuIs_<L, ZuMvArray>{}, int>
  operator <=>(const L &l, const R &r) { return l.cmp(r); }

// hash
  uint32_t hash() const { return ZuHash<ZuMvArray>::hash(*this); }

// traits
  struct Traits : public ZuBaseTraits<ZuMvArray> {
    using Elem = T;
    enum {
      IsArray = 1, IsPrimitive = 0, IsPOD = 0,
      IsString =
	bool(ZuIsSame<ZuDecay<T>, char>{}) ||
	bool(ZuIsSame<ZuDecay<T>, wchar_t>{}),
      IsWString = bool(ZuIsSame<ZuDecay<T>, wchar_t>{})
    };
    static T *data(ZuMvArray &a) { return a.data(); }
    static const T *data(const ZuMvArray &a) { return a.data(); }
    static uint64_t length(const ZuMvArray &a) { return a.length(); }
  };
  friend Traits ZuTraitsType(ZuMvArray *);

// STL cruft
  using iterator = T *;
  using const_iterator = const T *;
  using iterator_category = std::contiguous_iterator_tag;
  const T *begin() const { return m_data; }
  const T *end() const { return m_data + m_length; }
  const T *cbegin() const { return m_data; } // sigh
  const T *cend() const { return m_data + m_length; }
  T *begin() { return m_data; }
  T *end() { return m_data + m_length; }

private:
  uint64_t	m_length = 0;
  T		*m_data = nullptr;
};

#endif /* ZuMvArray_HH */
