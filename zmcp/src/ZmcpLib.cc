//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z MCP library

#include <zlib/ZmcpLib.hh>
#include <zlib/Zmcp.hh>

ZmcpExtern const char ZmcpLib[] = "@(#) Z MCP Library v" Z_VERNAME;

namespace Zmcp {

ZmcpAPI int logLevel(ZuCSpan name)
{
  struct IDs { using Keys = LogLevels; };
  static constexpr auto matcher = ZuMatcher<IDs>();
  return matcher.exact(name);
}

ZmcpAPI int serverMethod(ZuCSpan name)
{
  struct IDs { using Keys = ZuStringTL<
    "server/discover", "initialize", "notifications/initialized",
    "ping", "tools/list", "tools/call", "notifications/cancelled",
    "notifications/progress", "notifications/message", "logging/setLevel">;
  };
  static constexpr auto matcher = ZuMatcher<IDs>();
  return matcher.exact(name);
}

namespace Client_ {

ZmcpAPI int notification(ZuCSpan method)
{
  struct IDs {
    using Keys = ZuStringTL<"notifications/progress", "notifications/message">;
  };
  static constexpr auto matcher = ZuMatcher<IDs>();
  return matcher.exact(method);
}

} // Client_

} // Zmcp
