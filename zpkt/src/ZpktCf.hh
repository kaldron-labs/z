//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Private configuration-map allocation rollback.

#ifndef ZpktCf_HH
#define ZpktCf_HH

#ifndef ZpktLib_HH
#include <zlib/ZpktLib.hh>
#endif

#include <zlib/ZuPtr.hh>
#include <zlib/ZfCf.hh>

namespace Zpkt {

struct CfMap : ZfCf::AsMap<ZfFieldTC::UDT> {
  template <typename O, typename Facet>
  struct Handler : ZfCf::AsMap<ZfFieldTC::UDT>::Handler<O, Facet> {
    using Base = ZfCf::AsMap<ZfFieldTC::UDT>::Handler<O, Facet>;
    using Base::Base;

    O *alloc() const {
      ZuPtr<O> owner = new O;
      Base::load(*owner);
      return ZuMv(owner).release();
    }
  };
};

} // namespace Zpkt
#endif /* ZpktCf_HH */
