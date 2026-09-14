//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// authenticated encryption of persisted sensitive fields

#ifndef zumd_secret_HH
#define zumd_secret_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/ZumTypes.hh>
#include <zlib/ZtlsRandom.hh>

namespace Zum {

Bytes serverKeyCheck(ZuBSpan key);

bool serverSecretDecrypt(
  ZuBSpan key, ZuCSpan issuer, ZuCSpan recordType, ZuCSpan recordID,
  ZuCSpan field, ZuBSpan envelope, Bytes &plain);

bool serverSecretEncrypt(
  Ztls::Random &, ZuBSpan key, ZuCSpan issuer, ZuCSpan recordType,
  ZuCSpan recordID, ZuCSpan field, ZuBSpan plain, Bytes &envelope);

// Offline rotation: authenticated new-key fields remain unchanged on resume.
// Failure leaves the original ciphertext untouched; plaintext is cleared.
bool serverSecretRekey(
  Ztls::Random &, ZuBSpan oldKey, ZuBSpan newKey, ZuCSpan issuer,
  ZuCSpan recordType, ZuCSpan recordID, ZuCSpan field, Bytes &envelope);

} // namespace Zum

#endif /* zumd_secret_HH */
