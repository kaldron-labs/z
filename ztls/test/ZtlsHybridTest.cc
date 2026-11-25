//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZtlsKEM.hh>

#include "ZtlsTestKeys.hh"

using namespace ZuTestUtil;
using namespace Ztls::PK;

static void vector()
{
  ZuTestScope(vector);
  uint8_t seed[HybridSeedSize], expectedPK[HybridPublicSize];
  uint8_t ciphertext[HybridCiphertextSize];
  uint8_t expectedSecret[HybridSecretSize];
  ZuCheck(ZtlsTestKeys::read("hybrid", seed), return);
  ZuCheck(ZtlsTestKeys::read("hybrid.pub", expectedPK), return);
  ZuCheck(ZtlsTestKeys::read("hybrid.enc", ciphertext), return);
  ZuCheck(ZtlsTestKeys::read("hybrid.shared", expectedSecret), return);
  MLKEM768X25519Private sk{seed};
  uint8_t		pub[HybridPublicSize], secret[HybridSecretSize];
  ZuCheck(!sk.exportPK(pub).template is<ZeException>());
  ZuCheck(!memcmp(pub, expectedPK, sizeof(pub)));
  ZuCheck(!sk.decapsulate(ciphertext, secret).template is<ZeException>());
  ZuCheck(!memcmp(secret, expectedSecret, sizeof(secret)));
  ZuClear(seed, sizeof(seed));
  ZuClear(secret, sizeof(secret));
}

static void roundtrip()
{
  ZuTestScope(roundtrip);
  Ztls::Random rng;
  ZuCheck(rng.init());
  uint8_t seed[HybridSeedSize];
  ZuCheck(rng.random(seed));
  MLKEM768X25519Private sk{seed};
  uint8_t		pub[HybridPublicSize];
  ZuCheck(!sk.exportPK(pub).template is<ZeException>());
  MLKEM768X25519Public pk{pub};
  uint8_t	       ct[HybridCiphertextSize], other[HybridCiphertextSize];
  uint8_t	       secret[HybridSecretSize], recovered[HybridSecretSize];
  ZuCheck(!pk.encapsulate(rng, ct, secret).template is<ZeException>());
  ZuCheck(!sk.decapsulate(ct, recovered).template is<ZeException>());
  ZuCheck(!memcmp(secret, recovered, sizeof(secret)));
  ZuCheck(!pk.encapsulate(rng, other, recovered).template is<ZeException>());
  ZuCheck(memcmp(ct, other, sizeof(ct)) != 0);
  ZuCheck(!sk.decapsulate(other, secret).template is<ZeException>());
  ZuCheck(!memcmp(secret, recovered, sizeof(secret)));
  ZuCheck(pk.exportPK({pub, sizeof(pub) - 1}).template is<ZeException>());
  ZuCheck(pk.encapsulate(rng, {ct, sizeof(ct) - 1}, secret).template is<ZeException>());
  ZuCheck(pk.encapsulate(rng, ct, {secret, sizeof(secret) - 1}).template is<ZeException>());
  ZuCheck(sk.decapsulate({ct, sizeof(ct) - 1}, recovered).template is<ZeException>());
  ZuCheck(sk.decapsulate(ct, {recovered, sizeof(recovered) - 1}).template is<ZeException>());
  uint8_t oversized[HybridPublicSize + 1];
  ZuCheck(pk.exportPK(oversized).template is<ZeException>());
  ZuCheck(pk.encapsulate(rng, {oversized, HybridCiphertextSize + 1}, secret)
      .template is<ZeException>());
  ZuCheck(pk.encapsulate(rng, ct, {oversized, HybridSecretSize + 1}).template is<ZeException>());
  memcpy(oversized, ct, sizeof(ct));
  ZuCheck(
    sk.decapsulate({oversized, HybridCiphertextSize + 1}, recovered).template is<ZeException>());
  ZuCheck(sk.decapsulate(ct, {oversized, HybridSecretSize + 1}).template is<ZeException>());
  bool rejected = false;
  try {
    MLKEM768X25519Private invalid{
      {seed, sizeof(seed) - 1}
    };
  } catch (const ZeException &) {
    rejected = true;
  }
  ZuCheck(rejected);
  rejected = false;
  try {
    MLKEM768X25519Public invalid{
      {pub, sizeof(pub) - 1}
    };
  } catch (const ZeException &) {
    rejected = true;
  }
  ZuCheck(rejected);
  memcpy(oversized, seed, sizeof(seed));
  rejected = false;
  try {
    MLKEM768X25519Private invalid{
      {oversized, HybridSeedSize + 1}
    };
  } catch (const ZeException &) {
    rejected = true;
  }
  ZuCheck(rejected);
  memcpy(oversized, pub, sizeof(pub));
  rejected = false;
  try {
    MLKEM768X25519Public invalid{oversized};
  } catch (const ZeException &) {
    rejected = true;
  }
  ZuCheck(rejected);
  memset(ct + MLKEM768CiphertextSize, 0, Ztls::X25519KeySize);
  ZuCheck(sk.decapsulate(ct, recovered).template is<ZeException>());
  ZuClear(seed, sizeof(seed));
  ZuClear(secret, sizeof(secret));
  ZuClear(recovered, sizeof(recovered));
}

int main()
{
  ZuTestMain();
  ZuCheck(ZtlsTestKeys::init(), return 1);
  ZuTestCall_("vector", vector);
  ZuTestCall_("roundtrip", roundtrip);
}
