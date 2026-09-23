//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZdbusServer.hh>

using namespace ZuTestUtil;

namespace Zdbus_ {

static void route()
{
  ZuTestScope(route);
  Server srv;
  ZuCheck(!srv.route("/org/example/Test", "org.example.Test", "Run",
    [](ZmRef<ZiIOBuf>, FrameInfo) { }));
}

} // Zdbus_

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(Zdbus_::route);
  return 0;
}
