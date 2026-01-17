//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// intrusively reference-counted polymorphic object (base class)

#ifndef ZuPolymorph_HH
#define ZuPolymorph_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <stddef.h>

#include <zlib/ZuObjectTraits.hh>

class ZuPolymorph {
  ZuPolymorph(const ZuPolymorph &) = delete;
  ZuPolymorph &operator =(const ZuPolymorph &) = delete;

  friend ZuPolymorph ZuObjectType(ZuPolymorph *);

public:
  ZuInline ZuPolymorph() noexcept : m_refCount(0) { }
 
  virtual ~ZuPolymorph() noexcept { }

  ZuInline void ref() const { m_refCount++; }
  ZuInline bool deref() const { return !--m_refCount; }
  ZuInline int refCount() const { return m_refCount; }

private:
  mutable int		m_refCount;
};

#endif /* ZuPolymorph_HH */
