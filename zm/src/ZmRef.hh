//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// intrusively reference-counted smart pointer class
// - atomic reference count
// - no shared_ptr control block overhead (8 bytes vs 32 bytes)
// - interoperates freely with raw pointers
// - ZmHeap enables custom allocator for whole object including reference count
// - cache-friendly (like enable_shared_from_this but without wasting 24 bytes)
// - no weak_ptr

#ifndef ZmRef_HH
#define ZmRef_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZuTraits.hh>

#ifdef ZmObject_DEBUG
#include <zlib/ZmObjectDebug.hh>
#endif

// ZmRef can be used with ZmPolymorph, ZmObject, ZuPolymorph or ZuObject
// - ZmRef adds reference leak debugging (in debug builds)
// - ZmObject is atomically reference-counted i.e. is MT-safe
// - ZuObject is for single-threaded use
// - above the Zu layer there is no reason to prefer ZuRef over ZmRef

// rules for using ZmRef
// - always point to objects allocated using new (use ZmHeap to optimize)
// - always point to objects which derive from ZmObject
// - be careful to maintain a positive reference count when mixing
//   with real pointers - objects will delete themselves from
//   under you if they think they are referenced by nothing!
// - can pass by raw pointer or const ZmRef &, but always return by value

void ZmRefType(...);

template <typename> class ZmRef;

#ifdef ZmObject_DEBUG
struct ZmRef__ {
  template <typename O> static ZuIs<O, ZmObjectDebug>
  ZmREF_(const O *o, const void *p) { o->ref(p); }
  template <typename O> static ZuIs<O, ZmObjectDebug>
  ZmREF_(const ZmRef<O> &o, const void *p) { o->ref(p); }
  template <typename O> static ZuIs<O, ZmObjectDebug>
  ZmDEREF_(const O *o, const void *p) { if (o->deref(p)) delete o; }
  template <typename O> static ZuIs<O, ZmObjectDebug>
  ZmDEREF_(const ZmRef<O> &o, const void *p)
    { if (o->deref(p)) delete o.ptr(); }
  template <typename O> static ZuIs<O, ZmObjectDebug>
  ZmMVREF_(const O *o, const void *p, const void *n) { o->mvref(p, n); }
  template <typename O> static void
  ZmMVREF_(const ZmRef<O> &o, const void *p, const void *n) { o->mvref(p, n); }
  template <typename O> static ZuIsNot<O, ZmObjectDebug>
  ZmREF_(const O *o, const void *) { o->ref(); }
  template <typename O> static ZuIsNot<O, ZmObjectDebug>
  ZmREF_(const ZmRef<O> &o, const void *) { o->ref(); }
  template <typename O> static ZuIsNot<O, ZmObjectDebug>
  ZmDEREF_(const O *o, const void *) { if (o->deref()) delete o; }
  template <typename O> static ZuIsNot<O, ZmObjectDebug>
  ZmDEREF_(const ZmRef<O> &o, const void *) { if (o->deref()) delete o.ptr(); }
  template <typename O> static ZuIsNot<O, ZmObjectDebug>
  ZmMVREF_(const O *, const void *, const void *) { }
  template <typename O> static ZuIsNot<O, ZmObjectDebug>
  ZmMVREF_(const ZmRef<O> &, const void *, const void *) { }
};
#define ZmREF(o) ZmRef__::ZmREF_((o), this)
#define ZmDEREF(o) ZmRef__::ZmDEREF_((o), this)
#define ZmMVREF(o, p, n) ZmRef__::ZmMVREF_((o), (p), (n))
#else
struct ZmRef__ {
  template <typename O>
  static void ZmDEREF_(const O *o) { if (o->deref()) delete o; }
  template <typename O>
  static void ZmDEREF_(const ZmRef<O> &o) { if (o->deref()) delete o.ptr(); }
};
#define ZmREF(o) ((o)->ref())
#define ZmDEREF(o) ZmRef__::ZmDEREF_(o)
#define ZmMVREF(o, p, n) (void())
#endif

template <typename T_> class ZmRef {
template <typename> friend class ZmRef;
  friend T_ ZmRefType(ZmRef *);

public:
  using T = T_;

private:
  struct Acquire { T *o; };

  ZmRef(Acquire acquire) noexcept : m_object{acquire.o} { }

  // matches ZmRef<U> where U is not T, but is in the same type hierarchy as T
  template <typename V> struct IsOtherRef_ :
      public ZuBool<ZuIsBase<V, T>{} || ZuIsBase<T, V>{}> { };
  template <typename U> struct IsOtherRef :
      public IsOtherRef_<decltype(ZmRefType(ZuDeclVal<U *>()))> { };
  template <typename U, typename = void, bool = IsOtherRef<U>{}>
  struct MatchOtherRef_ { };
  template <typename U, typename R>
  struct MatchOtherRef_<U, R, true> { using T = R; };
  template <typename U, typename R = void>
  using MatchOtherRef = typename MatchOtherRef_<U, R>::T;

  // matches ZmRef<U> where U is either T or in the same type hierarchy as T
  template <typename V> struct IsRef_ :
      public ZuBool<ZuIs_<V, T>{} || ZuIs_<T, V>{}> { };
  template <typename U> struct IsRef :
      public IsRef_<decltype(ZmRefType(ZuDeclVal<U *>()))> { };
  template <typename U, typename = void, bool = IsRef<U>{}>
  struct MatchRef_ { };
  template <typename U, typename R>
  struct MatchRef_<U, R, true> { using T = R; };
  template <typename U, typename R = void>
  using MatchRef = typename MatchRef_<U, R>::T;

  // matches U * where U is either T or in the same type hierarchy as T
  template <typename U> struct IsPtr :
    public ZuBool<(bool(ZuIs_<U, T>{}) || bool(ZuIs_<T, U>{}))> { };
  template <typename U, typename R = void>
  using MatchPtr = ZuIfT<IsPtr<U>{}, R>;

public:
  ZmRef() = default;
  ZmRef(const ZmRef &r) noexcept : m_object{r.m_object} {
    if (T *o = m_object) ZmREF(o);
  }
  ZmRef(ZmRef &&r) noexcept : m_object{r.m_object} {
    r.m_object = nullptr;
#ifdef ZmObject_DEBUG
    if (T *o = m_object) ZmMVREF(o, &r, this);
#endif
  }
  template <typename R, MatchOtherRef<ZuDeref<R>, int> = 0>
  ZmRef(R &&r) noexcept : m_object{
	static_cast<T *>(const_cast<typename ZuDeref<R>::T *>(r.m_object))} {
    if constexpr (ZuIsLRef<R>{}) {
      if (T *o = m_object) ZmREF(o);
    } else {
#ifdef ZmObject_DEBUG
      if (T *o = m_object) ZmMVREF(o, &r, this);
#endif
      r.m_object = nullptr;
    }
  }
  ZmRef(T *o) noexcept : m_object{o} {
    if (o) ZmREF(o);
  }
  template <typename O, MatchPtr<O, int> = 0>
  ZmRef(O *o) noexcept : m_object{static_cast<T *>(o)} {
    if (o) ZmREF(o);
  }
  ~ZmRef() noexcept {
    if (T *o = m_object) ZmDEREF(o);
  }

  template <typename R> MatchRef<R> swap(R &r) noexcept {
    T *o = m_object;
    m_object = static_cast<T *>(r.m_object);
    r.m_object = static_cast<typename R::T *>(o);
#ifdef ZmObject_DEBUG
    if (o) ZmMVREF(o, this, &r);
    if (m_object) ZmMVREF(m_object, &r, this);
#endif
  }

  template <typename R>
  friend MatchRef<R> swap(ZmRef &r1, R &r2) noexcept {
    r1.swap(r2);
  }

  ZmRef &operator =(ZmRef r) noexcept {
    swap(r);
    return *this;
  }
  template <typename R>
  MatchOtherRef<R, ZmRef &> operator =(R r) noexcept {
    swap(r);
    return *this;
  }

  template <typename O>
  MatchPtr<O, ZmRef &> operator =(O *n) {
    if (m_object != n) {
      if (n) ZmREF(n);
      T *o = m_object;
      m_object = n;
      if (o) ZmDEREF(o);
    }
    return *this;
  }

  ZuInline operator T *() const { return m_object; }
  ZuInline T *operator ->() const { return m_object; }

  template <typename O = T>
  MatchRef<ZmRef<O>, O *> ptr() const {
    return static_cast<O *>(m_object);
  }
  T *ptr_() const { return m_object; }

  static ZmRef acquire(T *o) {
    return ZmRef{Acquire{o}};
  }
  template <typename O = T>
  MatchRef<ZmRef<O>, O *> release() && {
    T *o = m_object;
    m_object = nullptr;
    return static_cast<O *>(o);
  }

  // const casting
  const T *constPtr() const { return const_cast<const T *>(m_object); }
  ZmRef<const T> constRef() const & { return {constPtr()}; }
  ZmRef<const T> constRef() && {
    return ZmRef<const T>::acquire(
      const_cast<const T *>(ZuMv(*this).release()));
  }
  ZuStrip<T> *mutablePtr() const { return const_cast<ZuStrip<T> *>(m_object); }
  ZmRef<ZuStrip<T>> mutableRef() const & { return {mutablePtr()}; }
  ZmRef<ZuStrip<T>> mutableRef() && {
    return ZmRef<ZuStrip<T>>::acquire(
      const_cast<ZuStrip<T> *>(ZuMv(*this).release()));
  }

  bool operator !() const { return !m_object; }

  struct Traits : public ZuTraits<T *> {
    enum { IsPrimitive = 0, IsPOD = 0 };
  };
  friend Traits ZuTraitsType(ZmRef *);

protected:
  T		*m_object = nullptr;
};

// deduction guide
template <typename T>
ZmRef(T *) -> ZmRef<T>;

template <typename T> struct ZuCmp;
template <typename T>
struct ZuCmp<ZmRef<T> > : public ZuCmp<T *> {
  static bool null(const ZmRef<T> &r) { return !r; }
  static const ZmRef<T> &null() { static const ZmRef<T> v; return v; }
};

template <typename T> struct ZuHash;
template <typename T>
struct ZuHash<ZmRef<T> > : public ZuHash<T *> { };

#endif /* ZmRef_HH */
