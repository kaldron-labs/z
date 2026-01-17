//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// intrusively reference-counted object (explicit base class)
// - ZmAtomic<int> reference count (multithread-safe)
// - 8 bytes of overhead compared with 32 bytes overhead for std::shared_ptr
//   - std::enable_shared_from_this<T> - weak_ptr - 8 bytes
//   - std::allocate_shared<T>() - control block - 24 bytes

#ifndef ZmObject_HH
#define ZmObject_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <stddef.h>

#include <zlib/ZmAtomic.hh>
#ifdef ZmObject_DEBUG
#include <zlib/ZmObjectDebug.hh>
#endif

class ZmObject 
#ifdef ZmObject_DEBUG
: public ZmObjectDebug
#endif
{
public:
  friend ZmObject ZuObjectType(ZmObject *);

  ZmObject() = default;

  ZmObject(const ZmObject &) = delete;
  ZmObject &operator =(const ZmObject &) = delete;
  ZmObject(ZmObject &&) = delete;
  ZmObject &operator =(ZmObject &&) = delete;

  ZuInline ~ZmObject() noexcept {
#ifdef ZmObject_DEBUG
    this->del_();
#endif
  }

  ZuInline int refCount() const { return m_refCount.load_(); }

#ifdef ZmObject_DEBUG
  void ref(const void *referrer = 0) const
#else
  ZuInline void ref() const
#endif
  {
#ifdef ZmObject_DEBUG
    if (ZuUnlikely(this->deleted_())) return;
    if (ZuUnlikely(this->debugging_())) ZmObject_ref(this, referrer);
#endif
    this->ref_();
  }
#ifdef ZmObject_DEBUG
  bool deref(const void *referrer = 0) const
#else
  ZuInline bool deref() const
#endif
  {
#ifdef ZmObject_DEBUG
    if (ZuUnlikely(this->deleted_())) return false;
    if (ZuUnlikely(this->debugging_())) ZmObject_deref(this, referrer);
#endif
    return this->deref_();
  }

#ifdef ZmObject_DEBUG
  void mvref(const void *prev, const void *next) const {
    if (ZuUnlikely(this->debugging_())) {
      ZmObject_ref(this, next);
      ZmObject_deref(this, prev);
    }
  }
#endif

  // apps occasionally need to manipulate the refCount directly
  ZuInline void ref_() const { ++m_refCount; }
  ZuInline void ref2_() const { m_refCount += 2; }
  ZuInline bool deref_() const { return !--m_refCount; }

private:
#ifdef ZmObject_DEBUG
  ZuInline bool deleted_() const { return m_refCount.load_() < 0; }
  ZuInline void del_() const { m_refCount.store_(-1); }
#endif

  mutable ZmAtomic<int>		m_refCount = 0;
};

#endif /* ZmObject_HH */
