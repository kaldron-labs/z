//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// intrusively reference-counted object (base class)
// - plain int reference count (use ZmObject for atomic reference counting)
// - 8 bytes of overhead compared with 32 bytes overhead for std::shared_ptr
//   - std::enable_shared_from_this<T> - weak_ptr - 8 bytes
//   - std::allocate_shared<T>() - control block - 24 bytes

#ifndef ZuObject_HH
#define ZuObject_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <stddef.h>

#include <zlib/ZuObjectTraits.hh>
#include <zlib/ZuRef.hh>

class ZuObject {
public:
  friend ZuObject ZuObjectType(ZuObject *);

  ZuObject() = default;

  ZuObject(const ZuObject &) = delete;
  ZuObject &operator =(const ZuObject &) = delete;
  ZuObject(ZuObject &&) = delete;
  ZuObject &operator =(ZuObject &&) = delete;

  ZuInline void ref() const { ++m_refCount; }
  ZuInline bool deref() const { return !--m_refCount; }

  template <typename O, typename L>
  bool withRef(L &&l) {
    if (m_refCount <= 0) return false;
    ++m_refCount;
    ZuFwd<L>(l)(ZuRef<O>::acquire(static_cast<O *>(this)));
    return true;
  }

  ZuInline int refCount() const { return m_refCount; }

  // apps occasionally need to manipulate the refCount directly
  ZuInline void ref_() const { ++m_refCount; }
  ZuInline bool deref_() const { return !--m_refCount; }

private:
  mutable int	m_refCount = 0;
};

#endif /* ZuObject_HH */
