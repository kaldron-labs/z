//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZutcpTFO.hh>

using namespace ZuTestUtil;

namespace Zutcp {

// Deterministic entropy is confined to the isolated primitive test.
struct TFOTest {
  static bool init(TFOKey &key, ZuBSpan secret) { return key.init(secret); }
  static bool entropy(TFOKey &key, int (*random)(uint8_t *, int)) {
    return key.initRandom(random);
  }
  static bool encode(const TFOCookieKey &key, uint8_t *out) {
    return TFOKey::encode(key,out);
  }
};

} // namespace Zutcp

static void secretBytes(uint8_t *out)
{
  for (unsigned i = 0; i < Zutcp::TFOKey::SecretSize; ++i) out[i] = uint8_t(i);
}

static Zutcp::TFOCookieKey peer(unsigned last, bool v6 = false, uint16_t port = 18191)
{
  ZiIP client{v6 ? "2001:db8::20" : "198.18.0.20"};
  ZiIP server{v6 ? "2001:db8::21" : "198.18.0.21"};
  if (!v6) server.inAddr().s_addr = htonl(0xc6120000U | last);
  else server.in6Addr().s6_addr[15] = uint8_t(last);
  return {client,ZiSockAddr{server,port}};
}

static bool equal(const Zutcp::TFOCookie &a, const Zutcp::TFOCookie &b)
{
  return a.length == b.length && !memcmp(a.bytes,b.bytes,sizeof(a.bytes));
}

static void testTFOVectors()
{
  ZuTestScope(testTFOVectors);
  uint8_t secret[Zutcp::TFOKey::SecretSize];
  secretBytes(secret);
  Zutcp::TFOKey key;
  bool initialized = Zutcp::TFOTest::init(key,{secret,sizeof(secret)});
  uint8_t input[Zutcp::TFOKey::InputSize];
  const uint8_t expected[] = {
    'Z','u','t','c','p','.','T','F','O','.','v','1',4,
    0,0,0,0,0,0,0,0,0,0,0,0,198,18,0,20,
    0,0,0,0,0,0,0,0,0,0,0,0,198,18,0,21,71,15
  };
  const uint8_t v4[] = {
    0x76,0x94,0x6f,0x10,0x1f,0xdf,0xb5,0x00,0xd7,0x2c,0xe5,0x87,0x97,0xdf,0xf4,0x10
  };
  const uint8_t v6[] = {
    0x56,0x79,0x79,0x44,0x65,0x21,0xd9,0xf7,0xa0,0xf3,0x71,0x8d,0x42,0x9a,0x7d,0xeb
  };
  Zutcp::TFOCookie cookie;
  ZuCHECK(initialized && sizeof(input) == sizeof(expected) &&
      Zutcp::TFOTest::encode(peer(21),input) && !memcmp(input,expected,sizeof(input)) &&
      key.issue(peer(21),cookie) && cookie.length == 16 && !memcmp(cookie.bytes,v4,16) &&
      key.validate(peer(21),cookie.span()),
    "canonical47-byte IPv4 input and independent first16 HMAC-SHA256 vector");
  ZuCHECK(key.issue(peer(0x21,true),cookie) && cookie.length == 16 &&
      !memcmp(cookie.bytes,v6,16) && key.validate(peer(0x21,true),cookie.span()) &&
      Zutcp::TFOTest::encode(peer(0x21,true),input) && input[12] == 6 &&
      input[13] == 0x20 && input[14] == 1 && input[28] == 0x20 && input[44] == 0x21,
    "IPv6 uses complete canonical addresses and an explicit family byte");
}

static void testTFOBinding()
{
  ZuTestScope(testTFOBinding);
  uint8_t secret[Zutcp::TFOKey::SecretSize];
  secretBytes(secret);
  Zutcp::TFOKey key;
  bool initialized = Zutcp::TFOTest::init(key,{secret,sizeof(secret)});
  auto tuple = peer(21);
  Zutcp::TFOCookie cookie;
  bool issued = key.issue(tuple,cookie);
  auto otherClient = tuple;
  otherClient.p<0>() = ZiIP{"198.18.0.22"};
  auto otherPort = peer(21,false,18192);
  auto reversed = Zutcp::TFOCookieKey{tuple.p<1>().ip(),ZiSockAddr{tuple.p<0>(),18191}};
  ZiSockAddr first{tuple.p<0>(),49152}, later{tuple.p<0>(),49153};
  Zutcp::TFOCookie reused;
  ZuCHECK(initialized && issued && !key.validate(otherClient,cookie.span()) &&
      !key.validate(peer(22),cookie.span()) && !key.validate(otherPort,cookie.span()) &&
      !key.validate(reversed,cookie.span()) && !key.validate(peer(0x21,true),cookie.span()) &&
      key.issue({later.ip(),tuple.p<1>()},reused) && equal(cookie,reused) &&
      key.validate({first.ip(),tuple.p<1>()},reused.span()),
    "cookies bind clientIP/serverIP/port/family and survive a changed client ephemeral port");
  for (unsigned i : {0u,15u}) {
    auto wrong = cookie;
    wrong.bytes[i] ^= 1;
    ZuCHECK(!key.validate(tuple,wrong.span()),
      "wrong authenticators at either end must fail constant-time crypto comparison");
  }
  bool lengths = true;
  for (unsigned i = 0; i <= 16; ++i)
    if (i != 16) lengths &= !key.validate(tuple,{cookie.bytes,i});
  uint8_t oversized[18] = {};
  ZuCHECK(lengths && !key.validate(tuple,{oversized,sizeof(oversized)}),
    "all non16 server cookie lengths are unusable without a connection-failure policy");
}

static void testTFOKeyLifetime()
{
  ZuTestScope(testTFOKeyLifetime);
  uint8_t secret[Zutcp::TFOKey::SecretSize];
  secretBytes(secret);
  Zutcp::TFOKey key, replacement, production;
  Zutcp::TFOCookie sentinel;
  memset(sentinel.bytes,0xa5,sizeof(sentinel.bytes)); sentinel.length = 16;
  auto unchanged = sentinel;
  bool failure = !key.issue(peer(21),sentinel) && equal(sentinel,unchanged) &&
    !Zutcp::TFOTest::entropy(key,[](uint8_t *out,int n) { memset(out,0xa5,n); return 0; }) &&
    !Zutcp::TFOTest::entropy(key,[](uint8_t *,int) { return -1; }) &&
    !Zutcp::TFOTest::init(key,{secret,sizeof(secret) - 1});
  bool initialized = Zutcp::TFOTest::init(key,{secret,sizeof(secret)});
  Zutcp::TFOCookie first, same;
  bool issued = key.issue(peer(21),first);
  memset(secret,0xa5,sizeof(secret));
  bool different = Zutcp::TFOTest::init(replacement,{secret,sizeof(secret)});
  auto bad = peer(21);
  bad.p<1>() = ZiSockAddr{bad.p<1>().ip(),0};
  ZuCHECK(failure && initialized && issued && different &&
      !Zutcp::TFOTest::init(key,{secret,sizeof(secret)}) &&
      key.issue(peer(21),same) && equal(first,same) && key.validate(peer(21),first.span()) &&
      !replacement.validate(peer(21),first.span()) && !key.issue(bad,sentinel) &&
      equal(sentinel,unchanged) && production.init() && !production.init() &&
      production.issue(peer(21),same) && production.validate(peer(21),same.span()),
    "key bytes are owned inline, retained for one instance and replaced only by a new instance");
}

static void testTFOOpaqueCache()
{
  ZuTestScope(testTFOOpaqueCache);
  Zutcp::TFOCache cache{8};
  uint8_t opaque[18];
  for (unsigned i = 0; i < sizeof(opaque); ++i) opaque[i] = uint8_t(i * 17 + 3);
  bool stored = true;
  for (unsigned n = 4; n <= 16; n += 2) {
    stored &= cache.store(peer(n),{opaque,n});
    Zutcp::TFOCookie copy;
    stored &= cache.find(peer(n),copy) && copy.length == n && !memcmp(copy.bytes,opaque,n);
    copy.bytes[0] ^= 1;
    Zutcp::TFOCookie again;
    stored &= cache.find(peer(n),again) && again.bytes[0] == opaque[0];
  }
  Zutcp::TFOCache scoped{4};
  auto local = peer(21);
  local.p<0>() = ZiIP{"198.18.0.22"};
  Zutcp::TFOCookieKey mapped{ZiIP{"::ffff:198.18.0.20"},
    ZiSockAddr{ZiIP{"::ffff:198.18.0.21"},18191}};
  bool distinct = true;
  Zutcp::TFOCookieKey keys[] = {peer(21),local,mapped,peer(21,false,18192)};
  for (unsigned i = 0; i < 4; ++i) {
    uint8_t value[] = {uint8_t(i),1,2,3};
    distinct &= scoped.store(keys[i],value);
  }
  for (unsigned i = 0; i < 4; ++i) {
    Zutcp::TFOCookie value;
    distinct &= scoped.find(keys[i],value) && value.length == 4 && value.bytes[0] == i;
  }
  ZuCHECK(distinct && scoped.count() == 4,
    "cache lookup separates selected localIP, server port and mappedIPv6 versus IPv4 family");
  Zutcp::TFOCookie request;
  bool requestOnly = request.assign({}) && !request.length && !cache.store(peer(30),{});
  bool rejected = true;
  for (unsigned n : {1u,2u,3u,5u,7u,9u,11u,13u,15u,17u,18u})
    rejected &= !cache.store(peer(30),{opaque,n});
  Zutcp::TFOCookie snapshot;
  bool found = cache.find(peer(4),snapshot);
  memset(opaque,0xa5,sizeof(opaque));
  Zutcp::TFOCookie retained;
  ZuCHECK(stored && requestOnly && rejected && cache.count() == 7 && found &&
      cache.find(peer(4),retained) && equal(snapshot,retained) &&
      !cache.find(peer(30),retained),
    "opaque even4..16 peer cookies and copied snapshots are independent of request0 "
    "and caller storage");
}

static void testTFOCacheFIFO()
{
  ZuTestScope(testTFOCacheFIFO);
  uint8_t bytes[] = {0,1,2,3};
  Zutcp::TFOCache cache{3};
  bool ready = cache.store(peer(21),bytes) && cache.store(peer(22),bytes) &&
    cache.store(peer(23),bytes);
  bytes[0] = 9;
  bool update = cache.store(peer(21),bytes);
  Zutcp::TFOCookie cookie;
  bool replacement = cache.store(peer(24),bytes);
  bool oldest = !cache.find(peer(21),cookie) && cache.find(peer(22),cookie);
  // Remove an interior slot, then consume that exact free slot before eviction.
  bool hole = cache.erase(peer(23)) && !cache.erase(peer(23)) && cache.count() == 2;
  bool refill = cache.store(peer(25),bytes) && cache.store(peer(24),bytes) &&
    cache.find(peer(22),cookie) && cache.find(peer(24),cookie) && cache.find(peer(25),cookie);
  bool evicted = cache.store(peer(26),bytes) && !cache.find(peer(22),cookie) &&
    cache.find(peer(24),cookie) && cache.find(peer(25),cookie) && cache.find(peer(26),cookie);
  ZuCHECK(ready && update && replacement && oldest && hole && refill && evicted &&
      cache.count() == 3,
    "update keeps FIFO order; interior erase recycles its slot without overwriting a live entry");
  bool wrap = true;
  for (unsigned i = 27; i < 127; ++i) {
    wrap &= cache.store(peer(i),bytes) && cache.count() == 3;
    if (i >= 29) wrap &= !cache.find(peer(i - 3),cookie) && cache.find(peer(i - 2),cookie) &&
      cache.find(peer(i - 1),cookie) && cache.find(peer(i),cookie);
  }
  ZuCHECK(wrap, "fixed-capacity FIFO wrap keeps precisely the newest three insertion keys");
}

static void testTFOCacheResize()
{
  ZuTestScope(testTFOCacheResize);
  uint8_t bytes[] = {1,2,3,4};
  Zutcp::TFOCache cache{3};
  bool ready = cache.store(peer(21),bytes) && cache.store(peer(22),bytes) &&
    cache.store(peer(23),bytes);
  Zutcp::TFOCookie cookie;
  bool stable = cache.resize(3) && cache.find(peer(21),cookie) &&
    cache.find(peer(22),cookie) && cache.find(peer(23),cookie);
  bool shrink = cache.resize(2) && cache.capacity() == 2 && cache.count() == 2 &&
    !cache.find(peer(21),cookie) && cache.find(peer(22),cookie) && cache.find(peer(23),cookie);
  bool grow = cache.resize(4) && cache.count() == 2 && cache.store(peer(24),bytes) &&
    cache.store(peer(25),bytes) && cache.erase(peer(24)) && cache.store(peer(26),bytes) &&
    cache.find(peer(22),cookie) && cache.find(peer(23),cookie) && cache.find(peer(25),cookie) &&
    cache.find(peer(26),cookie);
  auto bad = peer(21);
  bad.p<1>() = ZiSockAddr{ZiIP{"2001:db8::21"},18191};
  auto snapshot = cookie;
  ZuCHECK(ready && stable && shrink && grow && !cache.resize(0) &&
      !cache.store(bad,bytes) && !cache.find(bad,cookie) && equal(cookie,snapshot) &&
      cache.capacity() == 4 && cache.count() == 4,
    "stopped resize preserves newest evidence and free slots; invalid config/key "
    "leaves state intact");
}

int main(int argc, char **argv)
{
  parse(argc,argv);
  ZuTestMain();
  ZuTestCall(testTFOVectors);
  ZuTestCall(testTFOBinding);
  ZuTestCall(testTFOKeyLifetime);
  ZuTestCall(testTFOOpaqueCache);
  ZuTestCall(testTFOCacheFIFO);
  ZuTestCall(testTFOCacheResize);
}
