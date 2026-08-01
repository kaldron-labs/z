//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// safe alloca() array:
// - stack allocates if requested size is less than 50% of the remaining stack space
// - falls back to RAII heap
// - run-time fixed-size: cannot grow
//
// WARNING: ZmScratch(T, N, VHEAP) is a macro that evaluates N multiple times

#ifndef ZmScratch_HH
#define ZmScratch_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <iterator>
#include <limits.h>

#include <zlib/ZuTraits.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuArrayFn.hh>
#include <zlib/ZuCmp.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZuUTF.hh>
#include <zlib/ZuBox.hh>
#include <zlib/ZuAlt.hh>

#include <zlib/ZmStackAvail.hh>

template <typename> struct ZmScratch_Print { };
template <> struct ZmScratch_Print<char> {
  friend ZuPrintString ZuPrintType(ZmScratch_Print *);
};

template <typename T_, typename VHeap_ = ZuVoid>
class ZmScratch_ :
  public ZmScratch_Print<ZuStrip<T_>>, public ZuArrayFn<T_> {
  ZmScratch_() = delete;
  ZmScratch_(const ZmScratch_ &) = delete;
  ZmScratch_ &operator =(const ZmScratch_ &) = delete;
  ZmScratch_(ZmScratch_ &&) = delete;
  ZmScratch_ &operator =(ZmScratch_ &&a) = delete;

public:
  using T = T_;
  using VHeap = VHeap_;
  using AltChar = ZuAlt<T>;
  using Cmp = ZuCmp<T>;
  using Fn = ZuArrayFn<T>;

  ZmScratch_(T *data, unsigned size) noexcept :
    m_size(size), m_data(data) { }
  ~ZmScratch_() noexcept(ZuNXDestroy<T>{}) {
    if (ZuUnlikely(!m_data)) return;
    destroyElems(m_data, m_length);
    uint8_t *ptr_ = reinterpret_cast<uint8_t *>(m_data);
    auto self = ZmSelf();
    auto addr = reinterpret_cast<uint8_t *>(self->stackAddr());
    auto size = self->stackSize();
    if (ZuLikely(ptr_ >= addr && ptr_ < (addr + size)))
      ++self->m_allocStack;
    else {
      ++self->m_allocHeap;
      if constexpr (!ZuIsSame<VHeap, ZuVoid>{})
	VHeap::vfree(ptr_);
      else
	Zm::alignedFree(ptr_);
    }
  }

  using Fn::initElem;
  using Fn::initElems;
  using Fn::destroyElem;
  using Fn::moveElems;
  using Fn::copyElems;
  using Fn::destroyElems;

  // from some string with same char (including string literals)
  template <typename U, typename V = T>
  struct IsString : public ZuBool<
    (ZuTraits<U>::IsSpan || ZuTraits<U>::IsString) &&
    bool(ZuEquiv<typename ZuTraits<U>::Elem, V>{})> { };
  template <typename U, typename R = void>
  using MatchString = ZuIfT<IsString<U>{}, R>;

  // from char2 string (requires conversion)
  template <typename U, typename V = AltChar>
  struct IsAltString : public ZuBool<
    !ZuIsSame<V, void>{} && bool(IsString<U, V>{})> { };
  template <typename U, typename R = void>
  using MatchAltString = ZuIfT<IsAltString<U>{}, R>;

  // from any array type with convertible element type (not a string)
  template <typename U, typename V = T>
  struct IsSpan : public ZuBool<
    !IsString<U>{} &&
    !IsAltString<U>{} &&
    !ZuIsSame<U, V>{} &&
    ZuTraits<U>::IsSpan &&
    ZuIsConvertible<typename ZuTraits<U>::Elem, V>{}> { };
  template <typename U, typename R = void>
  using MatchSpan = ZuIfT<IsSpan<U>{}, R>;

  // is this array a string?
  template <typename V = T>
  struct ThisIsString : public ZuBool<
    (bool(ZuEquiv<V, char>{}) || bool(ZuIsSame<ZuDecay<V>, wchar_t>{}))> { };

  // from individual elem
  template <typename U, typename V = T>
  struct IsElem_ : public ZuBool<
    bool(ZuIsSame<V, wchar_t>{}) ?
      bool(ZuIsSame<ZuDecay<U>, V>{}) :
      bool(ZuEquiv<U, V>{})> { };

  // from individual char2 (requires conversion)
  template <typename U, typename V = AltChar>
  struct IsAltChar : public ZuBool<
    !ZuIsSame<V, void>{} && bool(IsElem_<U, V>{})> { };
  template <typename U, typename R = void>
  using MatchAltChar = ZuIfT<IsAltChar<U>{}, R>;

  // from printable type (if this is a char array)
  template <typename U, typename V = T>
  struct IsPrint : public ZuBool<
    bool(ThisIsString<V>{}) &&
    ZuPrint<U>::OK && !ZuPrint<U>::String> { };
  template <typename U, typename V = T>
  struct IsPDelegate : public ZuBool<
    bool(ThisIsString<V>{}) && ZuPrint<U>::Delegate> { };
  template <typename U, typename R = void>
  using MatchPDelegate = ZuIfT<IsPDelegate<U>{}, R>;
  template <typename U, typename V = T>
  struct IsPBuffer : public ZuBool<
    bool(ThisIsString<V>{}) && ZuPrint<U>::Buffer> { };
  template <typename U, typename R = void>
  using MatchPBuffer = ZuIfT<IsPBuffer<U>{}, R>;

  // from any STL iterable with convertible element type (not array or string)
  template <typename U, typename = void>
  struct IsIterable_ : public ZuFalse { };
  template <typename U>
  struct IsIterable_<U, decltype(
    ZuDeclVal<const U &>().end() - ZuDeclVal<const U &>().begin(), void())> :
      public ZuTrue { };
  template <typename U, typename V = T>
  struct IsIterable : public ZuBool<
    !IsString<U>{} &&
    !IsAltString<U>{} &&
    !IsPrint<U>{} &&
    !ZuIsSame<U, V>{} &&
    !ZuTraits<U>::IsSpan &&
    bool(IsIterable_<ZuDecay<U>>{}) &&
    ZuIsConstructible<typename ZuTraits<U>::Elem, V>{}> { };
  template <typename U, typename R = void>
  using MatchIterable = ZuIfT<IsIterable<U>{}, R>;

  // from real primitive types other than chars (if this is a string)
  template <typename U, typename V = T>
  struct IsReal : public ZuBool<
    bool(ThisIsString<V>{}) &&
    !IsElem_<U>{} && !IsAltChar<U>{} &&
    ZuTraits<U>::IsReal && ZuTraits<U>::IsPrimitive &&
    !ZuTraits<U>::IsArray> { };
  template <typename U, typename R = void>
  using MatchReal = ZuIfT<IsReal<U>{}, R>;

  // from primitive pointer (not an array, string, or otherwise printable)
  template <typename U, typename V = T>
  struct IsPtr : public ZuBool<
    bool(ThisIsString<V>{}) &&
    ZuTraits<U>::IsPointer && ZuTraits<U>::IsPrimitive &&
    !ZuTraits<U>::IsArray && !ZuTraits<U>::IsString> { };
  template <typename U, typename R = void>
  using MatchPtr = ZuIfT<IsPtr<U>{}, R>;

  // from individual element
  template <typename U, typename V = T>
  struct IsElem : public ZuBool<
    bool(ZuIsSame<U, V>{}) ||
      (!IsString<U>{} &&
       !ZuTraits<U>::IsArray &&
       !IsAltString<U>{} &&
       !IsAltChar<U>{} &&
       !IsPDelegate<U>{} &&
       !IsPBuffer<U>{} &&
       !IsReal<U>{} &&
       ZuIsConvertible<U, V>{})> { };
  template <typename U, typename R = void>
  using MatchElem = ZuIfT<IsElem<U>{}, R>;

  // limit member operator <<() overload resolution to supported types
  template <typename U>
  struct IsStreamable : public ZuBool<
    bool(IsString<U>{}) ||
    bool(IsSpan<U>{}) ||
    bool(IsAltString<U>{}) ||
    bool(IsAltChar<U>{}) ||
    bool(IsPDelegate<U>{}) ||
    bool(IsPBuffer<U>{}) ||
    bool(IsIterable<U>{}) ||
    bool(IsReal<U>{}) ||
    bool(IsElem<U>{})> { };
  template <typename U, typename R = void>
  using MatchStreamable = ZuIfT<IsStreamable<U>{}, R>;

// accessors

  ZuInline unsigned size() const { return m_size; }
  ZuInline unsigned length() const { return m_length; }

// array/ptr operators

  ZuInline auto &&operator [](this auto &&self, unsigned i) {
    return ZuFwdLike<decltype(self)>(self.m_data[i]);
  }
  ZuInline auto &&operator *(this auto &&self) {
    return ZuFwdLike<decltype(self)>(self.m_data[0]);
  }

// raw data access

  ZuInline T *data() { return m_data; }
  ZuInline const T *data() const { return m_data; }

  ZuInline auto span() { return ZuSpan(m_data, m_size); }
  ZuInline auto cspan() const { return ZuSpan(m_data, m_length); }

  const T *terminate() {
    if (ZuUnlikely(!m_size)) return m_data;
    if (ZuUnlikely(m_length >= m_size)) m_length = m_size - 1;
    m_data[m_length] = 0;
    return m_data;
  }

// comparisons

  ZuInline bool operator !() const { return !m_length; }
  ZuOpBool

protected:
  ZuInline bool same(const ZmScratch_ &a) const { return this == &a; }
  template <typename A>
  ZuInline bool same(const A &) const { return false; }

public:
  template <typename A>
  ZuInline bool equals(const A &a) const {
    return same(a) || cspan().equals(a);
  }
  template <typename A>
  ZuInline int cmp(const A &a) const {
    if (same(a)) return 0;
    return cspan().cmp(a);
  }
  template <typename L, typename R>
  friend ZuInline ZuIfT<ZuIs_<L, ZmScratch_>{}, bool>
  operator ==(const L &l, const R &r) { return l.equals(r); }
  template <typename L, typename R>
  friend ZuInline ZuIfT<ZuIs_<L, ZmScratch_>{}, int>
  operator <=>(const L &l, const R &r) { return l.cmp(r); }

// hash

  uint32_t hash() const { return Fn::hash(m_data, m_length); }

// iteration

  template <bool Mutable = false, typename L>
  ZuIfT<!Mutable> all(L &&l) const {
    for (unsigned i = 0, n = m_length; i < n; i++)
      ZuFwd<L>(l)(m_data[i]);
  }
  template <bool Mutable, typename L>
  ZuIfT<Mutable> all(L &&l) {
    for (unsigned i = 0, n = m_length; i < n; i++)
      ZuFwd<L>(l)(m_data[i]);
  }

// find element - lambda should return true on match
  template <typename L>
  int64_t find(L &&l) const {
    for (unsigned i = 0, n = m_length; i < n; i++)
      if (ZuFwd<L>(l)(m_data[i])) return i;
    return -1;
  }

// match at start
  template <typename A>
  bool match(const A &a) const { return cspan().match(a); }

// reset to null

  void clear() { null(); }
  void null() {
    destroyElems(m_data, m_length);
    m_length = 0;
  }

// set length

  template <bool InitElems = !ZuTraits<T>::IsPrimitive>
  void length(unsigned length) {
    if (length > m_size) length = m_size;
    if constexpr (InitElems) {
      if (length > m_length)
	initElems(m_data + m_length, length - m_length);
      else if (length < m_length)
	destroyElems(m_data + length, m_length - length);
    }
    m_length = length;
  }

// push/pop/shift/unshift

  // push() intentionally returns uninitialized storage
  // - recommended usage:
  //   auto o = new (array.push()) T(...)
  T *push() {
    if (m_length >= m_size) return nullptr;
    return &m_data[m_length++];
  }
  template <typename I>
  T *push(I &&i) {
    auto ptr = push();
    if (ZuLikely(ptr)) initElem(ptr, ZuFwd<I>(i));
    return ptr;
  }
  T pop() {
    if (!m_length) return Cmp::null();
    T v = ZuMv(m_data[--m_length]);
    destroyElem(m_data + m_length);
    return v;
  }
  T shift() {
    if (!m_length) return Cmp::null();
    T v = ZuMv(m_data[0]);
    --m_length;
    destroyElem(m_data);
    moveElems(m_data, m_data + 1, m_length);
    return v;
  }
  template <typename I>
  T *unshift(I &&i) {
    if (m_length >= m_size) return nullptr;
    moveElems(m_data + 1, m_data, m_length++);
    initElem(m_data, ZuFwd<I>(i));
    return m_data;
  }

  void shift(unsigned n) {
    if (ZuUnlikely(!n)) return;
    if (n > m_length) n = m_length;
    destroyElems(m_data, n);
    if (m_length -= n) moveElems(m_data, m_data + n, m_length);
  }

// append operations

  template <typename U>
  MatchStreamable<U &&, ZmScratch_ &> operator <<(U &&v) {
    append(ZuFwd<U>(v));
    return *this;
  }

  template <typename U>
  ZmScratch_ &operator +=(U &&v) {
    return *this << ZuFwd<U>(v);
  }

  template <typename A>
  MatchSpan<A> append(A &&a) {
    auto length = ZuTraits<A>::length(a);
    if (m_length + length > m_size) length = m_size - m_length;
    if constexpr (ZuIsLRef<A>{})
      copyElems(m_data + m_length, &a[0], length);
    else
      this->template moveElems<false>(m_data + m_length, &a[0], length);
    m_length += length;
  }

  template <typename A>
  MatchIterable<A> append(A &&a_) {
    auto length = a_.end() - a_.begin();
    if (m_length + length > m_size) length = m_size - m_length;
    auto a = a_.begin();
    for (unsigned i = 0; i < length; i++)
      initElem(&m_data[i + m_length], ZuFwdLike<A>(*a++));
    m_length += length;
  }

  template <typename S>
  MatchString<S> append(const S &s) {
    auto length = ZuTraits<S>::length(s);
    if (m_length + length > m_size) length = m_size - m_length;
    copyElems(m_data + m_length, &s[0], length);
    m_length += length;
  }

  template <typename E>
  MatchElem<E> append(E &&e) {
    if (m_length >= m_size) return;
    initElem(m_data + m_length, ZuFwd<E>(e));
    ++m_length;
  }

  template <typename S>
  MatchAltString<S> append(const S &s) {
    if (m_length >= m_size) return;
    m_length += ZuUTF<T, AltChar>::cvt(
      {m_data + m_length, m_size - m_length}, s);
  }
  template <typename C>
  MatchAltChar<C> append(C c) {
    if (m_length >= m_size) return;
    m_length += ZuUTF<T, AltChar>::cvt(
      {m_data + m_length, m_size - m_length}, {&c, 1});
  }

  template <typename P>
  MatchPDelegate<P> append(P &&p) {
    ZuPrint<P>::print(*this, ZuFwd<P>(p));
  }
  template <typename P>
  MatchPBuffer<P> append(const P &p) {
    unsigned length = ZuPrint<P>::length(p);
    if (!length || m_length + length >= m_size) return;
    if constexpr (ZuEquiv<T, char>{}) {
      m_length += ZuPrint<P>::print(
	reinterpret_cast<char *>(m_data) + m_length, length, p);
    } else {
      auto buf = static_cast<char *>(ZuAlloca(length, 1));
      if (!buf) return;
      ZuCSpan s(buf, ZuPrint<P>::print(buf, length, p));
      m_length += ZuUTF<T, AltChar>::cvt(
	{m_data + m_length, m_size - m_length}, s);
    }
  }

  template <typename V>
  MatchReal<V> append(V v) {
    append(ZuBoxed(v));
  }
  template <typename V>
  MatchPtr<V> append(V v) {
    append(ZuBoxPtr(v).hex<false, ZuFmt::Alt<>>());
  }

// splice operations
// - see comments in `ZuArray.hh` for description of functionality

  template <typename L, typename = void>
  struct IsCallable : public ZuFalse { };
  template <typename L>
  struct IsCallable<L, decltype(ZuDeclVal<L &>()(ZuDeclVal<ZuSpan<T>>()))> :
    public ZuTrue { };

  template <typename Removed, typename Replace>
  void splice(
    Removed &&removed, int offset, int length,
    Replace &&replace, unsigned rlength)
  {
    if (ZuUnlikely(!length)) return;
    if (offset < 0) { if ((offset += m_length) < 0) offset = 0; }
    if (length < 0) { if ((length += (m_length - offset)) <= 0) return; }

    // replacement starts beyond the current data
    if (offset > int(m_length)) {
      length = 0;
      if (offset > int(m_size)) offset = m_size;
      if (offset + rlength > int(m_size)) rlength = m_size - offset;
      if constexpr (IsCallable<Removed>{})
	removed(ZuSpan<T>());
      else
	removed = {};
      initElems(m_data + m_length, offset - m_length);
      if (rlength)
	rlength = replace(ZuSpan(m_data + offset, rlength));
      m_length = offset + rlength;
      return;
    }

    if (offset + rlength > int(m_size)) rlength = m_size - offset;
    if (offset + length > int(m_length)) {
      length = int(m_length) - offset;
      if (length < 0) length = 0;
    }

    auto shift = int(rlength) - length;
    auto tail = int(m_length) - (offset + length);
    if (tail < 0)
      tail = 0;
    else if (tail > int(m_size) - (offset + rlength)) {
      auto oldTail = tail;
      tail = int(m_size) - (offset + rlength);
      auto base = offset + length;
      destroyElems(m_data + base + tail, oldTail - tail);
    }
    if constexpr (IsCallable<Removed>{})
      removed(ZuSpan(m_data + offset, length));
    else
      removed = ZuSpan(m_data + offset, length);
    auto ptr = m_data + offset;
    if (length) destroyElems(ptr, length);
    if (tail) moveElems(ptr + length + shift, ptr + length, tail);
    auto nrlength = replace(ZuSpan(m_data + offset, rlength));
    if (nrlength < rlength && tail) {
      int rshift = int(nrlength) - int(rlength);
      moveElems(ptr + nrlength, ptr + rlength, tail);
      shift += rshift;
    }
    m_length += shift;
  }
  void splice(int offset) {
    splice([](ZuSpan<T>) { }, offset, INT_MAX,
      [](ZuSpan<T>) { return 0; }, 0);
  }
  void splice(int offset, int length) {
    splice([](ZuSpan<T>) { }, offset, length,
      [](ZuSpan<T>) { return 0; }, 0);
  }
  template <typename Removed>
  void splice(Removed &&removed, int offset, int length) {
    splice(ZuFwd<Removed>(removed), offset, length,
      [](ZuSpan<T>) { return 0; }, 0);
  }

  // traits
  struct Traits : public ZuBaseTraits<ZmScratch_> {
    using Elem = T;
    enum {
      IsArray = 1, IsSpan = 1, IsPrimitive = 0,
      IsString =
	bool(ZuIsSame<ZuDecay<T>, char>{}) ||
	bool(ZuIsSame<ZuDecay<T>, wchar_t>{}),
      IsWString = bool(ZuIsSame<ZuDecay<T>, wchar_t>{})
    };
    ZuInline static Elem *data(ZmScratch_ &a) { return a.data(); }
    ZuInline static const Elem *data(const ZmScratch_ &a) {
      return a.data();
    }
    ZuInline static unsigned length(const ZmScratch_ &a) {
      return a.length();
    }
  };
  friend Traits ZuTraitsType(ZmScratch_ *);

// STL cruft

  using iterator = T *;
  using const_iterator = const T *;
  using iterator_category = std::contiguous_iterator_tag;
  ZuInline const T *begin() const { return m_data; }
  ZuInline const T *end() const { return m_data + m_length; }
  ZuInline const T *cbegin() const { return m_data; }
  ZuInline const T *cend() const { return m_data + m_length; }
  ZuInline T *begin() { return m_data; }
  ZuInline T *end() { return m_data + m_length; }

private:
  unsigned	m_size;
  unsigned	m_length = 0;
  T		*m_data = nullptr;
};

#define ZmScratch_1(T, n) \
  ZmScratch_<T>{static_cast<T *>(!(n) ? nullptr : \
    (((ZmStackAvail()>>1) < ((n) * sizeof(T) + alignof(T))) ? \
      Zm::alignedAlloc<alignof(T)>((n) * sizeof(T)) : \
	ZuAlloca((n) * sizeof(T), alignof(T)))), n}
// ZuDecay enables the caller to pass VHeap as `typename T::VHeap`
#define ZmScratch_2(T, n, VHeap) \
  ZmScratch_<T, VHeap>{static_cast<T *>(!(n) ? nullptr : \
    (((ZmStackAvail()>>1) < ((n) * sizeof(T) + alignof(T))) ? \
      ZuDecay<VHeap>::valloc((n) * sizeof(T)) : \
	ZuAlloca((n) * sizeof(T), alignof(T)))), n}
#define ZmScratch_N(_0, _1, Fn, ...) Fn
#define ZmScratch__(T, ...) \
  ZmScratch_N(__VA_ARGS__, \
    ZmScratch_2(T, __VA_ARGS__), \
    ZmScratch_1(T, __VA_ARGS__))
#define ZmScratch(...) \
  ZuPP_Eval(ZuPP_Defer(ZmScratch__)(__VA_ARGS__))

#endif /* ZmScratch_HH */
