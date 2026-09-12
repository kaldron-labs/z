//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// OAuth authorization-server metadata and JWKS serialization

#ifndef ZumDiscovery_HH
#define ZumDiscovery_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/ZumTypes.hh>
#include <zlib/ZumRequest.hh>

namespace Zum {

struct DBContext;

ZumExtern String metadataJSON(ZuCSpan issuer);
ZumExtern String jwksJSON(const StringVec &publicJwks);
ZuDerive(DiscoveryFn, (ZmFn<void(bool, String),
  ZmFnHeapID<"Zum.DiscoveryFn">>));
ZumExtern bool jwksLoad(
  Requests *, ZuTime deadline, DBContext *, int64_t now,
  unsigned maxKeys, DiscoveryFn);

} // namespace Zum

#endif /* ZumDiscovery_HH */
