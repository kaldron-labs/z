//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuICmp.hh>

#include <zlib/ZhttpTransport.hh>

namespace Zhttp {

ZtEnumImplStruct(Transport);
ZtEnumImplStruct(Migration);
ZtEnumImplStruct(H2Policy);

Migration::T migrationMode(ZuCSpan s, Migration::T deflt)
{
  if (ZuICmp<ZuCSpan>::equals(s, "disabled") ||
      ZuICmp<ZuCSpan>::equals(s, "disable"))
    return Migration::Disabled;
  if (ZuICmp<ZuCSpan>::equals(s, "passive")) return Migration::Passive;
  if (ZuICmp<ZuCSpan>::equals(s, "active")) return Migration::Active;
  return deflt;
}

} // namespace Zhttp
