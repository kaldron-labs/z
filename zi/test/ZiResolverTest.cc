//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZiResolver.hh>

using namespace ZuTestUtil;

using Bytes = ZtArray<uint8_t>;

void put16(Bytes &b, uint16_t v)
{
  b.push(v>>8);
  b.push(v);
}

void put32(Bytes &b, uint32_t v)
{
  b.push(v>>24);
  b.push(v>>16);
  b.push(v>>8);
  b.push(v);
}

void name(Bytes &b, ZuCSpan n)
{
  if (n == ".") { b.push(0); return; }
  unsigned start = 0;
  for (unsigned i = 0; i <= n.length(); i++) {
    if (i < n.length() && n[i] != '.') continue;
    b.push(i - start);
    for (unsigned j = start; j < i; j++) b.push(n[j]);
    start = i + 1;
  }
  b.push(0);
}

void ptr(Bytes &b, uint16_t off)
{
  b.push(0xc0 | (off>>8));
  b.push(off);
}

void question(Bytes &b, ZuCSpan owner, uint16_t type = 65)
{
  name(b, owner);
  put16(b, type);
  put16(b, 1);
}

void svcParam(Bytes &b, uint16_t key, ZuBSpan v)
{
  put16(b, key);
  put16(b, v.length());
  for (unsigned i = 0; i < v.length(); i++) b.push(v[i]);
}

Bytes response(
  ZuCSpan owner, uint16_t priority, ZuCSpan target,
  ZmFn<void(Bytes &)> params)
{
  Bytes b;
  put16(b, 0x1234);
  put16(b, 0x8180);
  put16(b, 1);
  put16(b, 1);
  put16(b, 0);
  put16(b, 0);
  unsigned ownerOff = b.length();
  question(b, owner);
  ptr(b, ownerOff);
  put16(b, 65);
  put16(b, 1);
  put32(b, 60);
  unsigned rdlenOff = b.length();
  put16(b, 0);
  unsigned rdataOff = b.length();
  put16(b, priority);
  name(b, target);
  params(b);
  uint16_t rdlen = b.length() - rdataOff;
  b[rdlenOff] = rdlen>>8;
  b[rdlenOff + 1] = rdlen;
  return b;
}

int parseOne(const Bytes &b, ZiResolver::HTTPS &https)
{
  unsigned n = 0;
  return ZiResolver::parse(b, "example.com",
    ZmFn<bool(const ZiResolver::HTTPS &)>{[&](const auto &h) {
      https = h;
      ++n;
      return false;
    }}) == Zi::OK && n == 1 ? Zi::OK : Zi::IOError;
}

void testResolveDelegation()
{
  ZuTestScope(testResolveDelegation);

  unsigned n = 0;
  bool sawLoopback = false;
  ZeError e;
  ZuCheck(ZiResolver::resolve("127.0.0.1",
    ZmFn<bool(ZiIP)>{[&](ZiIP ip) {
      sawLoopback |= ip == ZiIP{0x7f000001U};
      ++n;
      return false;
    }}, &e) == Zi::OK);
  ZuCheck(n == 1);
  ZuCheck(sawLoopback);

  ZiIP ip;
  ZuCheck(ip.resolve("127.0.0.1", &e) == Zi::OK);
  ZuCheck(ip == ZiIP{0x7f000001U});
}

void testValidHTTPS()
{
  ZuTestScope(testValidHTTPS);

  uint8_t alpn[] = { 2, 'h', '3' };
  uint8_t port[] = { 0x20, 0xfb };
  uint8_t hint[] = { 192, 0, 2, 1 };
  auto msg = response("example.com", 1, ".", [&](Bytes &r) {
    svcParam(r, 1, ZuBSpan(alpn, sizeof(alpn)));
    svcParam(r, 3, ZuBSpan(port, sizeof(port)));
    svcParam(r, 4, ZuBSpan(hint, sizeof(hint)));
  });
  ZiResolver::HTTPS https;
  ZuCheck(parseOne(msg, https) == Zi::OK);
  ZuCheck(!https.alias());
  ZuCheck(https.target == "example.com");
  ZuCheck(https.hasALPN);
  ZuCheck(https.hasH3);
  ZuCheck(https.port == 8443);
  ZuCheck(https.hasIPv4Hint);
  ZuCheck(https.nIPv4Hint == 1);
  ZuCheck(https.ipv4Hint[0] == ZiIP{0xc0000201U});
}

void testAliasMode()
{
  ZuTestScope(testAliasMode);

  auto msg = response("example.com", 0, "svc.example.com", [](Bytes &) { });
  ZiResolver::HTTPS https;
  ZuCheck(parseOne(msg, https) == Zi::OK);
  ZuCheck(https.alias());
  ZuCheck(https.target == "svc.example.com");
}

bool bad(Bytes &&msg)
{
  ZiResolver::HTTPS https;
  return parseOne(msg, https) == Zi::IOError;
}

void testMalformed()
{
  ZuTestScope(testMalformed);

  uint8_t alpnBad[] = { 3, 'h', '3' };
  ZuCheck(bad(response("example.com", 1, ".", [&](Bytes &r) {
    svcParam(r, 1, ZuBSpan(alpnBad, sizeof(alpnBad)));
  })));

  uint8_t one[] = { 1 };
  ZuCheck(bad(response("example.com", 1, ".", [&](Bytes &r) {
    svcParam(r, 3, ZuBSpan(one, sizeof(one)));
  })));
  ZuCheck(bad(response("example.com", 1, ".", [&](Bytes &r) {
    svcParam(r, 4, ZuBSpan(one, sizeof(one)));
  })));
  ZuCheck(bad(response("example.com", 1, ".", [&](Bytes &r) {
    svcParam(r, 1, ZuBSpan{});
    svcParam(r, 1, ZuBSpan{});
  })));
  uint8_t port[] = { 0, 1 };
  uint8_t alpn[] = { 2, 'h', '3' };
  ZuCheck(bad(response("example.com", 1, ".", [&](Bytes &r) {
    svcParam(r, 3, ZuBSpan(port, sizeof(port)));
    svcParam(r, 1, ZuBSpan(alpn, sizeof(alpn)));
  })));

  auto truncated = response("example.com", 1, ".", [](Bytes &) { });
  truncated.length(truncated.length() - 1);
  ZuCheck(bad(ZuMv(truncated)));

  Bytes loop;
  put16(loop, 0);
  put16(loop, 0x8180);
  put16(loop, 0);
  put16(loop, 1);
  put16(loop, 0);
  put16(loop, 0);
  ptr(loop, 12);
  put16(loop, 65);
  put16(loop, 1);
  put32(loop, 0);
  put16(loop, 0);
  ZuCheck(bad(ZuMv(loop)));

  Bytes oob;
  put16(oob, 0);
  put16(oob, 0x8180);
  put16(oob, 0);
  put16(oob, 1);
  put16(oob, 0);
  put16(oob, 0);
  ptr(oob, 0x3ff);
  put16(oob, 65);
  put16(oob, 1);
  put32(oob, 0);
  put16(oob, 0);
  ZuCheck(bad(ZuMv(oob)));
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testResolveDelegation);
  ZuTestCall(testValidHTTPS);
  ZuTestCall(testAliasMode);
  ZuTestCall(testMalformed);
  return 0;
}
