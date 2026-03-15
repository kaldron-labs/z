//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZiIP.hh>

using namespace ZuTestUtil;

void testParseResolveAndPrint()
{
  ZuTestScope(testParseResolveAndPrint);

  ZiIP ip{0x7f000001U};
  ZtString<> s;
  s << ip;
  ZuCheck(s == "127.0.0.1");

  ZeError e;
  ZiIP ip2;
  ZuCheck(ip2.resolve("127.0.0.1", &e) == Zi::OK);
  ZuCheck(ip2 == ip);

  ZiIP ip3;
  ZuCheck(ip3.resolve("localhost", &e) == Zi::OK);
  ZuCheck(!!ip3);

  auto name = ip.name(&e);
  ZuCheck(!!name);

  ZiIP bad;
  ZuCheck(bad.resolve("definitely.invalid.localhost.zed", &e) == Zi::IOError);
}

void testMulticastBoundaries()
{
  ZuTestScope(testMulticastBoundaries);

  ZiIP a{0xDF000001U}; // 223.x.x.x
  ZiIP b{0xE0000001U}; // 224.x.x.x
  ZiIP c{0xEF000001U}; // 239.x.x.x
  ZiIP d{0xF0000001U}; // 240.x.x.x

  ZuCheck(!a.multicast());
  ZuCheck(b.multicast());
  ZuCheck(c.multicast());
  ZuCheck(!d.multicast());
}

void testSockAddrHelpers()
{
  ZuTestScope(testSockAddrHelpers);

  ZiSockAddr sa;
  ZuCheck(!sa);

  ZiIP ip{0x0A000001U}; // 10.0.0.1
  sa.init(ip, 4242);
  ZuCheck(!!sa);
  ZuCheck(sa.ip() == ip);
  ZuCheck(sa.port() == 4242);

  sa.null();
  ZuCheck(!sa);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testParseResolveAndPrint);
  ZuTestCall(testMulticastBoundaries);
  ZuTestCall(testSockAddrHelpers);
  return 0;
}
