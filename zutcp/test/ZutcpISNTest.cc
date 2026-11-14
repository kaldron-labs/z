//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZutcpISN.hh>

using namespace ZuTestUtil;

namespace Zutcp {

// Deterministic injection stays private to this isolated primitive fixture.
struct ISNTest {
  static bool entropy(ISN &isn, int (*random)(uint8_t *, int)) {
    return isn.init(7, ZuTime{100}, random);
  }
  static bool init(ISN &isn, ZuBSpan secret, ZuTime origin, unsigned interface = 7) {
    return isn.init(secret, origin, interface);
  }
  static bool generate(
    const ISN &isn, const ZiSockAddr &local, const ZiSockAddr &remote,
    ZuTime now, uint32_t &out)
  {
    return isn.generate(local, remote, now, out);
  }
  static bool encode(
    const ISN &isn, const ZiSockAddr &local, const ZiSockAddr &remote, uint8_t *out)
  {
    return isn.encode(local, remote, out);
  }
};

} // namespace Zutcp

static void fillSecret(uint8_t *secret)
{
  for (unsigned i = 0; i < Zutcp::ISN::SecretSize; ++i) secret[i] = uint8_t(i);
}

static void testISNVector()
{
  ZuTestScope(testISNVector);
  uint8_t secret[Zutcp::ISN::SecretSize];
  fillSecret(secret);
  Zutcp::ISN isn;
  bool initialized = Zutcp::ISNTest::init(isn, {secret, sizeof(secret)}, ZuTime{100});
  ZiSockAddr local{ZiIP{"198.18.0.20"}, 49152}, remote{ZiIP{"198.18.0.21"}, 18191};
  uint8_t input[Zutcp::ISN::InputSize];
  const uint8_t expected[] = {
    'Z','u','t','c','p','.','I','S','N','.','v','1', 0,0,0,7,4,
    0,0,0,0,0,0,0,0,0,0,0,0,198,18,0,20,192,0,
    0,0,0,0,0,0,0,0,0,0,0,0,198,18,0,21,71,15
  };
  uint32_t value = 0;
  ZuCHECK(initialized && sizeof(expected) == sizeof(input) &&
      Zutcp::ISNTest::encode(isn, local, remote, input) &&
      !memcmp(input, expected, sizeof(input)) &&
      Zutcp::ISNTest::generate(isn, local, remote, ZuTime{100}, value) && value == 0x9533e1d1,
    "canonical fixed-width IPv4 input and independent HMAC-SHA256 truncation vector");
  local = ZiSockAddr{ZiIP{"2001:db8::20"}, 49152};
  remote = ZiSockAddr{ZiIP{"2001:db8::21"}, 18191};
  ZuCHECK(Zutcp::ISNTest::generate(isn, local, remote, ZuTime{100}, value) &&
      value == 0xbe76ee15,
    "IPv6 addresses must use their full wire-order width and explicit family");
}

static void testISNClock()
{
  ZuTestScope(testISNClock);
  uint8_t secret[Zutcp::ISN::SecretSize];
  fillSecret(secret);
  Zutcp::ISN isn;
  bool initialized = Zutcp::ISNTest::init(isn, {secret, sizeof(secret)}, ZuTime{100});
  ZiSockAddr local{ZiIP{"198.18.0.20"}, 49152}, remote{ZiIP{"198.18.0.21"}, 18191};
  uint32_t first = 0, before = 0, step = 0, later = 0, wrapped = 0, backwards = 0;
  bool valid = initialized &&
    Zutcp::ISNTest::generate(isn, local, remote, ZuTime{100}, first) &&
    Zutcp::ISNTest::generate(isn, local, remote, ZuTime{100, 3000}, before) &&
    Zutcp::ISNTest::generate(isn, local, remote, ZuTime{100, 4000}, step) &&
    Zutcp::ISNTest::generate(isn, local, remote, ZuTime{100, 8000}, later) &&
    Zutcp::ISNTest::generate(isn, local, remote, ZuTime{17279, 869184000}, wrapped) &&
    Zutcp::ISNTest::generate(isn, local, remote, ZuTime{99, 999996000}, backwards);
  ZuCHECK(valid && first == before && step == uint32_t(first + 1) &&
      later == uint32_t(first + 2) && wrapped == first && backwards == uint32_t(first - 1),
    "the retained origin advances in 4us units modulo32 without a replacement clock or clamp");
  uint32_t replay = 0;
  ZuCHECK(Zutcp::ISNTest::generate(isn, local, remote, ZuTime{200}, later) &&
      Zutcp::ISNTest::generate(isn, local, remote, ZuTime{100}, replay) &&
      replay == first && later == uint32_t(first + 25000000),
    "later generation must preserve the origin and repeated inputs must reproduce the primitive");
}

static void testISNTuples()
{
  ZuTestScope(testISNTuples);
  uint8_t secret[Zutcp::ISN::SecretSize];
  fillSecret(secret);
  Zutcp::ISN isn, otherScope;
  bool valid = Zutcp::ISNTest::init(isn, {secret, sizeof(secret)}, ZuTime{100}) &&
    Zutcp::ISNTest::init(otherScope, {secret, sizeof(secret)}, ZuTime{100}, 8);
  ZiSockAddr local{ZiIP{"198.18.0.20"}, 49152}, remote{ZiIP{"198.18.0.21"}, 18191};
  struct Vector { ZiSockAddr local, remote; uint32_t expected; };
  Vector vectors[] = {
    {remote, local, 0xdfd0c388},
    {{local.ip(), 49153}, remote, 0xf3490236},
    {local, {remote.ip(), 18192}, 0x47ac1a4d},
    {{ZiIP{"198.18.0.22"}, local.port()}, remote, 0x06dff2e1},
    {local, {ZiIP{"198.18.0.22"}, remote.port()}, 0x042d2505}
  };
  for (const auto &v : vectors) {
    uint32_t value = 0;
    valid &= Zutcp::ISNTest::generate(isn, v.local, v.remote, ZuTime{100}, value) &&
      value == v.expected;
  }
  uint32_t value = 0;
  ZuCHECK(valid && Zutcp::ISNTest::generate(otherScope, local, remote, ZuTime{100}, value) &&
      value == 0xaeb50318,
    "fixed vectors separate local/remote addresses, ports, direction and interface scope");
}

static void testISNFailure()
{
  ZuTestScope(testISNFailure);
  Zutcp::ISN isn;
  ZiSockAddr local{ZiIP{"198.18.0.20"}, 49152}, remote{ZiIP{"198.18.0.21"}, 18191};
  uint8_t secret[Zutcp::ISN::SecretSize];
  fillSecret(secret);
  uint32_t value = 0x12345678;
  bool refused = !Zutcp::ISNTest::entropy(isn, [](uint8_t *p, int n) {
      memset(p, 0xa5, n);
      return 0;
    }) && !Zutcp::ISNTest::entropy(isn, [](uint8_t *, int) { return -1; }) &&
    !Zutcp::ISNTest::generate(isn, local, remote, ZuTime{100}, value) &&
    !Zutcp::ISNTest::init(isn, {secret, sizeof(secret) - 1}, ZuTime{100}) &&
    !Zutcp::ISNTest::init(isn, {secret, sizeof(secret)}, ZuTime{}) &&
    !Zutcp::ISNTest::init(isn, {secret, sizeof(secret)}, ZuTime{100}, 0);
  bool initialized = Zutcp::ISNTest::init(isn, {secret, sizeof(secret)}, ZuTime{100});
  ZiSockAddr v6{ZiIP{"2001:db8::21"}, 18191}, zeroPort{local.ip(), 0};
  ZuCHECK(refused && initialized &&
      !Zutcp::ISNTest::init(isn, {secret, sizeof(secret)}, ZuTime{200}) &&
      !Zutcp::ISNTest::generate(isn, local, v6, ZuTime{100}, value) &&
      !Zutcp::ISNTest::generate(isn, zeroPort, remote, ZuTime{100}, value) &&
      value == 0x12345678,
    "uninitialized, invalid setup or invalid tuple must fail without publishing a fallback ISN");
}

static void testISNSecret()
{
  ZuTestScope(testISNSecret);
  uint8_t secret[Zutcp::ISN::SecretSize];
  fillSecret(secret);
  Zutcp::ISN isn, production;
  bool initialized = Zutcp::ISNTest::init(isn, {secret, sizeof(secret)}, ZuTime{100});
  memset(secret, 0xa5, sizeof(secret));
  ZiSockAddr local{ZiIP{"198.18.0.20"}, 49152}, remote{ZiIP{"198.18.0.21"}, 18191};
  uint32_t value = 0;
  ZuCHECK(initialized && Zutcp::ISNTest::generate(isn, local, remote, ZuTime{100}, value) &&
      value == 0x9533e1d1 && production.init(7) && !production.init(7) &&
      production.generate(local, remote, value),
    "secret must be owned inline and production entropy initialization must succeed once");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testISNVector);
  ZuTestCall(testISNClock);
  ZuTestCall(testISNTuples);
  ZuTestCall(testISNFailure);
  ZuTestCall(testISNSecret);
}
