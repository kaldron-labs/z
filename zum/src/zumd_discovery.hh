//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// OAuth authorization-server metadata and JWKS serialization

#ifndef zumd_discovery_HH
#define zumd_discovery_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/ZumTypes.hh>
#include <zlib/ZumURI.hh>
#include <zlib/zumd_request.hh>

namespace Zum {

template <typename Heap> struct DBContext_;
using DBContext = DBContext_<
  ZmHeap<"Zum.zumd.db.context.DBContext", DBContext_<ZuVoid>>>;

ZumExtern bool appIssuerPath(AppID, String &);
ZumExtern bool appEndpointPath(AppID, ZuCSpan group, ZuCSpan endpoint,
  String &);
ZumExtern bool appNestedEndpointPath(AppID, ZuCSpan group,
  ZuCSpan section, ZuCSpan endpoint, String &);
ZumExtern bool appOIDCMetadataPath(AppID, String &);
ZumExtern bool appOAuthMetadataPath(AppID, String &);
ZumExtern bool appIssuer(ZuCSpan authorizationBase, AppID, String &);

ZumExtern String metadataJSON(
  ZuCSpan authorizationBase, AppID, const StringVec &scopes);
ZumExtern String jwksJSON(const StringVec &publicJwks);
ZuDerive(DiscoveryFn, (ZmFn<void(bool, String),
  ZmFnHeapID<"Zum.DiscoveryFn">>));
ZumExtern bool jwksLoad(
  Requests *, ZuTime deadline, DBContext *, ZuCSpan issuer, int64_t now,
  unsigned maxKeys, DiscoveryFn);

} // namespace Zum

#endif /* zumd_discovery_HH */
