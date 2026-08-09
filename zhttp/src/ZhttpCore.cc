//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZhttpCore.hh>

namespace Zhttp {

ZtEnumImplNS(Method);
ZtEnumImplStruct(BodyType);
ZtEnumImplStruct(Version);
ZtEnumImplStruct(RequestErrorCode);
ZtEnumImplStruct(RequestErrorScope);

unsigned requestErrorStatus(RequestErrorCode::T code)
{
  switch (code) {
    case RequestErrorCode::ContentTooLarge: return 413;
    case RequestErrorCode::TargetTooLong: return 414;
    case RequestErrorCode::HeadersTooLarge: return 431;
    case RequestErrorCode::NotImplemented: return 501;
    case RequestErrorCode::VersionUnsupported: return 505;
    default: return 400;
  }
}

} // namespace Zhttp
