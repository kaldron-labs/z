//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZiIP.hh>

using namespace ZuTestUtil;

ZiIP ip4(uint32_t n)
{
  in_addr addr;
  addr.s_addr = htonl(n);
  return ZiIP{addr};
}

void testParseResolveAndPrint()
{
  ZuTestScope(testParseResolveAndPrint);

  ZiIP ip = ip4(0x7f000001U);
  ZtString<> s;
  s << ip;
  ZuCheck(s == "127.0.0.1");

  ZeError e;
  ZiIP ip2;
  ZuCheck(ip2.resolve("127.0.0.1", &e) == Zi::OK);
  ZuCheck(ip2 == ip);

  ZiIP ip6;
  ZuCheck(ip6.resolve("::1", &e) == Zi::OK);
  ZuCheck(ip6.v6());
  ZuCheck(ip6.loopback());
  s.null();
  s << ip6;
  ZuCheck(s == "::1");

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

  ZiIP a = ip4(0xDF000001U); // 223.x.x.x
  ZiIP b = ip4(0xE0000001U); // 224.x.x.x
  ZiIP c = ip4(0xEF000001U); // 239.x.x.x
  ZiIP d = ip4(0xF0000001U); // 240.x.x.x

  ZuCheck(!a.multicast());
  ZuCheck(b.multicast());
  ZuCheck(c.multicast());
  ZuCheck(!d.multicast());

  ZuCheck(!ZiIP{"feff::1"}.multicast());
  ZuCheck(ZiIP{"ff00::1"}.multicast());
  ZuCheck(ZiIP{"ffff::1"}.multicast());
}

void testSockAddrHelpers()
{
  ZuTestScope(testSockAddrHelpers);

  ZiSockAddr sa;
  ZuCheck(!sa);
  ZuCheck(sa.type() == ZiIPType::Null);
  ZuCheck(sa.len() == ZiSockAddr::MaxLen);

  ZiIP ip = ip4(0x0A000001U); // 10.0.0.1
  sa.init(ip, 4242);
  ZuCheck(!!sa);
  ZuCheck(sa.type() == ZiIPType::V4);
  ZuCheck(sa.len() == sizeof(sockaddr_in));
  ZuCheck(sa.ip() == ip);
  ZuCheck(sa.port() == 4242);

  ZiIP ip6{"2001:db8::1"};
  sa.init(ip6, 4243);
  ZuCheck(!!sa);
  ZuCheck(sa.type() == ZiIPType::V6);
  ZuCheck(sa.len() == sizeof(sockaddr_in6));
  ZuCheck(sa.ip() == ip6);
  ZuCheck(sa.port() == 4243);

  sa.null();
  ZuCheck(!sa);
}

void testNullWildcardCompareHash()
{
  ZuTestScope(testNullWildcardCompareHash);

  ZiIP nil;
  ZiIP v4{"0.0.0.0"};
  ZiIP v6{"::"};

  ZuCheck(!nil);
  ZuCheck(!!v4);
  ZuCheck(!!v6);
  ZuCheck(!nil);
  ZuCheck(v4.wildcard());
  ZuCheck(v6.wildcard());
  ZuCheck(nil != v4);
  ZuCheck(nil != v6);
  ZuCheck(v4 != v6);
  ZuCheck(nil.hash() != v4.hash());
  ZuCheck(nil.hash() != v6.hash());
  ZuCheck(v4.hash() != v6.hash());
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testParseResolveAndPrint);
  ZuTestCall(testMulticastBoundaries);
  ZuTestCall(testSockAddrHelpers);
  ZuTestCall(testNullWildcardCompareHash);
  return 0;
}
