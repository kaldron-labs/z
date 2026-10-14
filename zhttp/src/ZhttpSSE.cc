//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZhttpSSE.hh>

#include <zlib/ZuMatcher.hh>

namespace Zhttp { namespace Private {

ZhttpAPI int sseField(ZuCSpan field)
{
  struct IDs { using Keys = ZuStringTL<"data", "id", "retry">; };
  static constexpr auto matcher = ZuMatcher<IDs>();
  return matcher.exact(field);
}

} } // Zhttp::Private
