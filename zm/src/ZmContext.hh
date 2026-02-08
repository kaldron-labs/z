//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// generic type-erased context pointer for callbacks
// - effectively a discriminated union of a raw pointer and a ZmRef<ZmPolymorph>
// - the discriminator is the high bit of a pointer-packed 64bit uintptr_t
// - if constructed with a ZmRef<T> where T is ZmPolymorph-derived, will
//   maintain a positive reference count during its lifetime, pinning the
//   object in memory
// - if constructed with a raw pointer will not attempt to manipulate
//   the reference count of the object
// - enables pinning a referenced object while the context remains in scope
//   (e.g. while a one-shot callback remains pending)

#ifndef ZmContext_HH
#define ZmContext_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZmRef.hh>
#include <zlib/ZmPolymorph.hh>

#ifdef ZmObject_DEBUG
#include <zlib/ZmObjectDebug.hh>
#endif

class ZmContext {
  // 64bit pointer-packing
  // - uses bit 63
  // - safe on x86_64, ARM, etc.
  static constexpr uintptr_t Owned = (uintptr_t(1)<<63);

protected:
  static constexpr bool owned(uintptr_t o) { return o & Owned; }
  static uintptr_t own(uintptr_t o) { return o | Owned; }
  static uintptr_t disown(uintptr_t o) { return o & ~Owned; }

  template <typename O = ZmPolymorph>
  static O *ptr(uintptr_t o) {
    return reinterpret_cast<O *>(o & ~Owned);
  }

public:
  ZmContext() = default;

  ~ZmContext() {
    if (ZuUnlikely(owned(m_object))) ZmDEREF(ptr(m_object));
  }

  ZmContext(const ZmContext &c) : m_object(c.m_object) {
    if (ZuUnlikely(owned(m_object))) ZmREF(ptr(m_object));
  }

  ZmContext(ZmContext &&c) : m_object(c.m_object) {
    c.m_object = 0;
#ifdef ZmObject_DEBUG
    if (ZuUnlikely(owned(m_object))) ZmMVREF(ptr(m_object), &c, this);
#endif
  }

protected:
  void copy(const ZmContext &c) {
    if (ZuUnlikely(owned(c.m_object))) ZmREF(ptr(c.m_object));
    if (ZuUnlikely(owned(m_object))) ZmDEREF(ptr(m_object));
    m_object = c.m_object;
  }

  void move(ZmContext &&c) {
    if (ZuUnlikely(owned(m_object))) ZmDEREF(ptr(m_object));
    m_object = c.m_object;
    c.m_object = 0;
#ifdef ZmObject_DEBUG
    if (ZuUnlikely(owned(m_object))) ZmMVREF(ptr(m_object), &c, this);
#endif
  }

public:
  ZmContext &operator =(const ZmContext &c) {
    if (ZuLikely(this != &c)) copy(c);
    return *this;
  }

  ZmContext &operator =(ZmContext &&c) {
    move(ZuMv(c));
    return *this;
  }

  template <typename O>
  ZmContext(O *o) : m_object((uintptr_t)o) { }
  template <typename O, typename = ZuBase<O, ZmPolymorph>>
  ZmContext(ZmRef<O> o) {
    new (&m_object) ZmRef<O>(ZuMv(o));
    m_object = own(m_object);
  }

  // access captured object
  template <typename O> O *object() const {
    return ptr<O>(m_object);
  }
  template <typename O> ZmRef<O> mvObject() {
    if (ZuUnlikely(!owned(m_object))) return ZmRef<O>{object<O>()};
    m_object = disown(m_object);
    return ZmRef<O>::acquire(object<O>());
  }
  template <typename O> void object(O *o) {
    if (ZuUnlikely(owned(m_object))) ZmDEREF(ptr(m_object));
    m_object = reinterpret_cast<uintptr_t>(o);
  }
  template <typename O> void object(ZmRef<O> o) {
    if (ZuLikely(owned(m_object))) ZmDEREF(ptr(m_object));
    new (&m_object) ZmRef<O>(ZuMv(o));
    m_object = own(m_object);
  }

  bool equals(const ZmContext &c) const {
    return m_object == c.m_object;
  }
  int cmp(const ZmContext &c) const {
    return ZuCmp<uintptr_t>::cmp(m_object, c.m_object);
  }
  friend inline bool operator ==(const ZmContext &l, const ZmContext &r) {
    return l.equals(r);
  }
  friend inline int operator <=>(const ZmContext &l, const ZmContext &r) {
    return l.cmp(r);
  }

  bool operator !() const { return !m_object; }
  ZuOpBool

  uint32_t hash() const {
    return ZuHash<uintptr_t>::hash(m_object);
  }

  struct Traits : public ZuBaseTraits<ZmContext> { enum { IsPOD = 1 }; };
  friend Traits ZuTraitsType(ZmContext *);

protected:
  uintptr_t &object_() const { return const_cast<uintptr_t &>(m_object); }

private:
  uintptr_t	m_object = 0;
};

#endif /* ZmContext_HH */
