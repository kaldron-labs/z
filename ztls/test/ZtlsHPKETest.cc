//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZtlsHPKE.hh>

#include "ZtlsTestKeys.hh"

using namespace ZuTestUtil;
using namespace Ztls::PK;

static void vector()
{
  ZuTestScope(vector);
  uint8_t seed[HybridSeedSize], enc[HybridCiphertextSize];
  constexpr auto info = "fixture info"_z;
  constexpr auto aad = "fixture aad"_z;
  constexpr auto expected = "test payload"_z;
  uint8_t ct[expected.length() + Ztls::HPKE::TagSize], plain[expected.length()];
  ZuCheck(ZtlsTestKeys::read("hybrid", seed), return);
  ZuCheck(ZtlsTestKeys::read("hpke.enc", enc), return);
  ZuCheck(ZtlsTestKeys::read("hpke.ct", ct), return);
  MLKEM768X25519Private sk{seed};
  auto			r = Ztls::HPKE::openBase(sk, info, aad, enc, ct, plain);
  ZuCheck(!r.template is<ZeException>());
  if (!r.template is<ZeException>()) {
    ZuCheck(r.template p<0>() == sizeof(plain));
    ZuCheck(!memcmp(plain, expected.data(), sizeof(plain)));
  }
  ZuClear(seed, sizeof(seed));
  ZuClear(plain, sizeof(plain));
}

static void roundtrip()
{
  ZuTestScope(roundtrip);
  Ztls::Random rng;
  ZuCheck(rng.init());
  uint8_t seed[HybridSeedSize], pub[HybridPublicSize];
  ZuCheck(rng.random(seed));
  MLKEM768X25519Private sk{seed};
  ZuCheck(!sk.exportPK(pub).template is<ZeException>());
  MLKEM768X25519Public pk{pub};
  constexpr uint8_t    info[] = {1, 0, 2};
  constexpr uint8_t    aad[] = {3, 0, 4};
  constexpr uint8_t    input[] = {5, 0, 6, 7};
  uint8_t	       enc[HybridCiphertextSize];
  uint8_t	       ct[sizeof(input) + Ztls::HPKE::TagSize];
  uint8_t	       plain[sizeof(input)];
  ZuCheck(!Ztls::HPKE::sealBase(rng, pk, info, aad, input, enc, ct).template is<ZeException>());
  auto r = Ztls::HPKE::openBase(sk, info, aad, enc, ct, plain);
  ZuCheck(!r.template is<ZeException>());
  if (!r.template is<ZeException>()) {
    ZuCheck(r.template p<0>() == sizeof(input));
    ZuCheck(!memcmp(plain, input, sizeof(input)));
  }
  ZuCheck(
    Ztls::HPKE::openBase(sk, info, {info, sizeof(info)}, enc, ct, plain)
      .template is<ZeException>());
  ZuCheck(Ztls::HPKE::openBase(sk, aad, aad, enc, ct, plain).template is<ZeException>());
  // Encapsulation rejection happens before key derivation or AEAD writes.
  // Every failure clears the expected plaintext prefix, preserving extra room.
  uint8_t guarded[sizeof(input) + 8];
  memset(guarded, 0xA5, sizeof(guarded));
  ZuCheck(
    Ztls::HPKE::openBase(sk, info, aad, {enc, sizeof(enc) - 1}, ct, guarded)
      .template is<ZeException>());
  bool prefixClear = true, tailUnchanged = true;
  for (unsigned i = 0; i < sizeof(input); ++i) prefixClear &= !guarded[i];
  for (unsigned i = sizeof(input); i < sizeof(guarded); ++i) tailUnchanged &= guarded[i] == 0xA5;
  ZuCheck(prefixClear);
  ZuCheck(tailUnchanged);
  ZuCheck(
    Ztls::HPKE::openBase(sk, info, aad, enc, {ct, sizeof(ct) - 1}, plain)
      .template is<ZeException>());
  ZuCheck(
    Ztls::HPKE::openBase(sk, info, aad, enc, {ct, Ztls::HPKE::TagSize - 1}, plain)
      .template is<ZeException>());
  ZuCheck(
    Ztls::HPKE::openBase(sk, info, aad, enc, ct, {plain, sizeof(plain) - 1})
      .template is<ZeException>());
  ct[sizeof(ct) - 1] ^= 1;
  memset(plain, 0xA5, sizeof(plain));
  ZuCheck(Ztls::HPKE::openBase(sk, info, aad, enc, ct, plain).template is<ZeException>());
  uint8_t zero[sizeof(plain)] = {};
  ZuCheck(!memcmp(plain, zero, sizeof(plain)));
  ct[sizeof(ct) - 1] ^= 1;
  ZuCheck(
    Ztls::HPKE::sealBase(rng, pk, info, aad, input, {enc, sizeof(enc) - 1}, ct)
      .template is<ZeException>());
  ZuCheck(
    Ztls::HPKE::sealBase(rng, pk, info, aad, input, enc, {ct, sizeof(ct) - 1})
      .template is<ZeException>());
  uint8_t emptyCT[Ztls::HPKE::TagSize], emptyPlain[1];
  ZuCheck(!Ztls::HPKE::sealBase(rng, pk, info, aad, {}, enc, emptyCT).template is<ZeException>());
  auto empty = Ztls::HPKE::openBase(sk, info, aad, enc, emptyCT, {emptyPlain, 0});
  ZuCheck(!empty.template is<ZeException>());
  if (!empty.template is<ZeException>()) ZuCheck(empty.template p<0>() == 0);
  memcpy(ct, input, sizeof(input));
  ZuCheck(!Ztls::HPKE::sealBase(rng, pk, info, aad, {ct, sizeof(input)}, enc, ct)
      .template is<ZeException>());
  auto inPlace = Ztls::HPKE::openBase(sk, info, aad, enc, ct, {ct, sizeof(input)});
  ZuCheck(!inPlace.template is<ZeException>());
  if (!inPlace.template is<ZeException>()) {
    ZuCheck(inPlace.template p<0>() == sizeof(input));
    ZuCheck(!memcmp(ct, input, sizeof(input)));
  }
  ZuClear(seed, sizeof(seed));
  ZuClear(plain, sizeof(plain));
}

int main()
{
  ZuTestMain();
  ZuCheck(ZtlsTestKeys::init(), return 1);
  ZuTestCall_("vector", vector);
  ZuTestCall_("roundtrip", roundtrip);
}
