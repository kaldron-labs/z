//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmThread.hh>

#include <zlib/ZiIP.hh>
#include <zlib/ZiResolver.hh>

using namespace ZuTestUtil;

namespace {

ZiIP ip4(uint32_t n)
{
  in_addr addr;
  addr.s_addr = htonl(n);
  return ZiIP{addr};
}

class FakeDNS {
public:
  FakeDNS()
  {
    m_socket = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (Zi::nullSocket(m_socket)) return;

    timeval tv{0, 100000};
    ::setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(
	m_socket, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
      return;

    socklen_t len = sizeof(addr);
    if (::getsockname(
	m_socket, reinterpret_cast<sockaddr *>(&addr), &len) != 0)
      return;
    m_port = ntohs(addr.sin_port);

    m_thread = ZmThread{[this]() { run(); }};
    m_ok = true;
  }

  ~FakeDNS()
  {
    m_stopping.store_(1);
    if (!Zi::nullSocket(m_socket)) Zi::closeSocket(m_socket);
    m_thread.join();
  }

  Zi::Name servers() const {
    Zi::Name s;
    s << "127.0.0.1:" << ZuBoxed(m_port);
    return s;
  }

  bool ok() const { return m_ok; }

private:
  static uint16_t u16(const uint8_t *ptr) {
    return (uint16_t(ptr[0]) << 8) | uint16_t(ptr[1]);
  }

  static void put16(uint8_t *&ptr, uint16_t v) {
    *ptr++ = uint8_t(v >> 8);
    *ptr++ = uint8_t(v);
  }

  static void put32(uint8_t *&ptr, uint32_t v) {
    *ptr++ = uint8_t(v >> 24);
    *ptr++ = uint8_t(v >> 16);
    *ptr++ = uint8_t(v >> 8);
    *ptr++ = uint8_t(v);
  }

  static void put(uint8_t *&ptr, ZuBSpan data) {
    for (unsigned i = 0; i < data.length(); i++) *ptr++ = data[i];
  }

  bool question(
    const uint8_t *buf, unsigned len, unsigned &end, uint16_t &type, bool &name)
  {
    static const uint8_t resolver[] = {
      8, 'r', 'e', 's', 'o', 'l', 'v', 'e', 'r',
      4, 't', 'e', 's', 't', 0
    };

    unsigned off = 12;
    while (off < len && buf[off]) off += unsigned(buf[off]) + 1;
    if (off >= len || off + 5 > len) return false;
    ++off;
    type = u16(buf + off);
    end = off + 4;
    name = end >= 12 + sizeof(resolver) &&
      !memcmp(buf + 12, resolver, sizeof(resolver));
    return true;
  }

  void run()
  {
    while (!m_stopping.load_()) {
      uint8_t buf[512];
      sockaddr_in from;
      socklen_t fromLen = sizeof(from);
      int n = ::recvfrom(
	m_socket, buf, sizeof(buf), 0,
	reinterpret_cast<sockaddr *>(&from), &fromLen);
      if (n <= 0) continue;

      unsigned qEnd = 0;
      uint16_t qType = 0;
      bool qName = false;
      if (n < 12 || !question(buf, unsigned(n), qEnd, qType, qName)) continue;

      uint8_t out[512];
      uint8_t *ptr = out;
      put(ptr, ZuBSpan{buf, 2});
      put16(ptr, 0x8180);
      put16(ptr, 1);
      put16(ptr, (qName && qType == ZiDNSType::A) ? 1 : 0);
      put16(ptr, 0);
      put16(ptr, 0);
      put(ptr, ZuBSpan{buf + 12, qEnd - 12});
      if (qName && qType == ZiDNSType::A) {
	put16(ptr, 0xc00c);
	put16(ptr, ZiDNSType::A);
	put16(ptr, ZiDNSClass::IN);
	put32(ptr, 0);
	put16(ptr, 4);
	*ptr++ = 192; *ptr++ = 0; *ptr++ = 2; *ptr++ = 53;
      }
      ::sendto(
	m_socket, out, ptr - out, 0,
	reinterpret_cast<sockaddr *>(&from), fromLen);
    }
  }

private:
  Zi::Socket		m_socket = Zi::nullSocket();
  uint16_t		m_port = 0;
  ZmThread		m_thread;
  bool			m_ok = false;
  ZmAtomic<unsigned>	m_stopping = 0;
};

} // namespace

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

  FakeDNS dns;
  ZuCheck(dns.ok());
  ZiResolver::final();
  ZiResolver::init(ZiResolverParams{}.ipv4(true).ipv6(false).
    timeoutMS(500).tries(1).servers(dns.servers()));

  ZiIP ip3;
  ZuCheck(ip3.resolve("resolver.test", &e) == Zi::OK);
  ZuCheck(ip3 == ip4(0xc0000235U));

  auto name = ip.name(&e);
  ZuCheck(!!name);

  ZiIP bad;
  ZuCheck(bad.resolve("missing.test", &e) == Zi::IOError);
  ZiResolver::stop();
  ZiResolver::final();
}

void testSpanParse()
{
  ZuTestScope(testSpanParse);

  static const ZuCSpan valid[] = {
    "127.0.0.1", "::", "::1", "1::", "2001:db8::1",
    "1:2:3:4:5:6:7:8", "::ffff:192.0.2.1",
    "1:2:3:4:5:6:192.0.2.1"
  };
  for (auto value : valid) {
    ZiIP ip;
    ZuCHECK(ZiIP::parse(ip, value), value);
  }

  static const ZuCSpan invalid[] = {
    "", "127.0.0", "01.2.3.4", "256.0.0.1", ":", "1:", ":::1",
    "1::2::3", "1:2:3:4:5:6:7", "1:2:3:4:5:6:7:8:9",
    "0x1::", "0X1::", "12345::",
    "1:2:3:4:5:6:7:192.0.2.1", "::ffff:999.0.0.1",
    "::ffff:01.2.3.4", "::ffff:1a.2.3.4", "::ffff:1234.2.3.4",
    "fe80::1%1"
  };
  for (auto value : invalid) {
    ZiIP ip;
    ZuCHECK(!ZiIP::parse(ip, value), value);
  }

  const char framed[] = "x2001:db8::1y";
  ZiIP ip;
  ZuCHECK(ZiIP::parse(ip, ZuCSpan{framed + 1, sizeof(framed) - 3}),
    "non-NUL-terminated subspan");
  ZuCHECK(ip.v6(), "subspan is IPv6");
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
  ZuTestCall(testSpanParse);
  ZuTestCall(testMulticastBoundaries);
  ZuTestCall(testSockAddrHelpers);
  ZuTestCall(testNullWildcardCompareHash);
  return 0;
}
