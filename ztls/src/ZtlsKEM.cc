//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZtlsKEM.hh>

namespace Ztls::PK::Load_ {

ZuUnion<ZmRef<AnyKey>, ZeException> loadMLKEM768PK(ZuBSpan pubKey)
{
  try {
    return ZmRef<AnyKey>{new PK_MLKEM768{pubKey}};
  } catch (const ZeException &e) {
    return e;
  }
}

ZuUnion<ZmRef<AnyKey>, ZeException> loadMLKEM768SK(ZuBSpan key)
{
  try {
    return ZmRef<AnyKey>{new SK_MLKEM768{key}};
  } catch (const ZeException &e) {
    return e;
  }
}

} // Ztls::PK::Load_
