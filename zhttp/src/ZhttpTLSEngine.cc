//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z HTTP TLS ALPN policy

#include <zlib/ZuMatcher.hh>

#include <zlib/ZhttpTLSEngine.hh>

namespace Zhttp {
namespace TLS_ {

int8_t version(ZuCSpan alpn, int8_t policy)
{
  static constexpr auto matcher = ZuMatcher<"h2", "http/1.1">();
  switch (matcher.exact(alpn)) {
    case 0:
      return policy != H2Policy::Disable ? Version::H2 : int8_t(-1);
    case 1:
      return policy != H2Policy::Force ? Version::H1 : int8_t(-1);
    default:
      return -1;
  }
}

} // namespace TLS_
} // namespace Zhttp
