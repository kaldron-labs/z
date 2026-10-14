//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Offline secret-key rotation mutations; no keys or plaintext in saga payloads.

#ifndef zumd_rekey_HH
#define zumd_rekey_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/ZuTuple.hh>
#include <zlib/zumd_identity_db.hh>
#include <zlib/zumd_provider_db.hh>
#include <zlib/zumd_key_db.hh>
#include <zlib/zumd_ssf_db.hh>
#include <zlib/zumd_secret.hh>

namespace Zum {

struct DB;
ZuDerive(RekeyFn, (ZmFn<void(bool), ZmFnHeapID<"Zum.RekeyFn">>));

// Caller has stopped all service writers; completion precedes store shutdown.
void serverRekey(DB *, Ztls::Random &, String issuer,
  Bytes oldKey, Bytes newKey, RekeyFn);

} // namespace Zum

#endif /* zumd_rekey_HH */
