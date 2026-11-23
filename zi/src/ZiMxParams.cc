//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ZiMultiplexer configuration

#include <zlib/ZuMatcher.hh>
#include <zlib/ZiMxConfig.hh>

struct CxnKeys {
  using Keys = ZuStringTL<"mif", "mreqs">;
};

void ZiCxnFields_load(const ZfCf::AnyNode *cf,
    const ZfCf::AnyNode *(&lookup)[2])
{
  static constexpr auto matcher = ZuMatcher<CxnKeys>();
  const auto &fields = cf->data<ZfCf::AnyNode::Object>();
  for (const auto &field: fields) {
    int id = matcher.exact(field.p<0>());
    if (id >= 0) lookup[id] = field.p<1>().ptr();
  }
}

ZfStructImpl(ZiCxnOptions);
ZfStructImpl(ZiCxnOptions, Cf);
ZfStructImpl(ZmSchedTParams);
ZfStructImpl(ZmSchedTParams, Cf);
ZfStructImpl(ZmSchedParams);
ZfStructImpl(ZmSchedParams, Cf);
ZfStructImpl(ZiMxParams);
ZfStructImpl(ZiMxParams, Cf);
