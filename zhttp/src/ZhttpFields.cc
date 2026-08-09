//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z HTTP wire-neutral field matchers

#include <zlib/ZuMatcher.hh>

#include <zlib/ZhttpFields.hh>

namespace Zhttp {

ZtEnumImplNS(FieldSection);

namespace Fields {

bool forbidden(ZuCSpan name)
{
  static constexpr auto matcher = ZuMatcher<
    "connection", "proxy-connection", "keep-alive",
    "transfer-encoding", "upgrade">();
  return matcher.exact(name) >= 0;
}

int pseudo(ZuCSpan name)
{
  static constexpr auto matcher =
    ZuMatcher<":method", ":path", ":scheme", ":authority", ":protocol">();
  return matcher.exact(name);
}

} // namespace Fields
} // namespace Zhttp
