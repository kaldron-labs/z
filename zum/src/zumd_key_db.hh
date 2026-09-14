//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// typed server tables

#ifndef zumd_key_db_HH
#define zumd_key_db_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/Zdb.hh>
#include <zlib/ZtlsRandom.hh>
#include <zlib/zumd.hh>
#include <zlib/zumd_db_context.hh>
#include <zlib/ZumJWTVerify.hh>

namespace Zum {

ZdbTableDerive(SignKeyTable, SignKey);

ZumExtern bool signKeyPublic(const SignKey &, Bytes &);
ZumExtern bool signKeyMatch(Ztls::Random &, const SignKey &, ZuBSpan privateKey);
// Empty audience is issuer-only verification for the provider's UserInfo.
ZumExtern bool signKeyVerify(const SignKey &, ZuCSpan token,
  ZuCSpan issuer, ZuCSpan audience, int64_t now,
  const JWTLimits &, Principal &);
ZuDerive(SignKeyFn, (ZmFn<void(SignKey),
  ZmFnHeapID<"Zum.SignKeyFn">>));
ZumExtern void signKeyLoad(
  DBContext *, String issuer, int64_t now, int64_t expires,
  unsigned maxKeys, SignKeyFn);

} // namespace Zum

#endif /* zumd_key_db_HH */
