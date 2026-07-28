//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z HTTP wire-neutral types

#ifndef ZhttpTypes_HH
#define ZhttpTypes_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <zlib/ZtEnum.hh>

namespace Zhttp {

ZtEnumNS(Method, int8_t,
  GET, POST, PUT, DELETE, PATCH, HEAD, OPTIONS, CONNECT, TRACE);

inline bool earlyDataSafeMethod(Method::T method)
{
  switch (method) {
    case Method::GET:
    case Method::HEAD:
    case Method::OPTIONS:
      return true;
    default:
      return false;
  }
}

inline bool earlyDataSafeRequest(Method::T method, bool hasBody)
{
  return !hasBody && earlyDataSafeMethod(method);
}

// deprecated transfer-encoding compression
ZtEnumNS(XferCompression, int8_t, compress, deflate, gzip);

} // namespace Zhttp

#endif /* ZhttpTypes_HH */
