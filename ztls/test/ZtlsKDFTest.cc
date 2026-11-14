//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuHex.hh>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZtlsKDF.hh>

using namespace ZuTestUtil;

static bool matches(ZuBSpan data, ZuCSpan hex)
{
  if (hex.length() != data.length() << 1) return false;
  for (unsigned i = 0, n = data.length(); i < n; ++i) {
    char encoded[2];
    ZuHex::encode<false>(encoded, {data.data() + i, 1});
    if (encoded[0] != hex[i << 1] || encoded[1] != hex[(i << 1) + 1]) return false;
  }
  return true;
}

static void vectors()
{
  ZuTestScope(vectors);
  // RFC 5869 A.1 and A.3. Reusing one owner checks parameter reset.
  uint8_t ikm[22], salt[13], info[10], prk[32], output[42];
  memset(ikm, 0x0b, sizeof(ikm));
  for (unsigned i = 0; i < sizeof(salt); ++i) salt[i] = i;
  for (unsigned i = 0; i < sizeof(info); ++i) info[i] = 0xf0 + i;
  Ztls::KDF::HKDF kdf;
  ZuCheck(kdf.init());
  ZuCheck(kdf.extract(salt, ikm, prk));
  ZuCheck(matches(prk,
    "077709362c2e32df0ddc3f0dc47bba6390b6c73bb50f9c3122ec844ad7c2b3e5"));
  ZuCheck(kdf.expand(prk, info, output));
  ZuCheck(matches(output,
    "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf"
    "34007208d5b887185865"));
  ZuCheck(kdf.derive(salt, ikm, info, output));
  ZuCheck(matches(output,
    "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf"
    "34007208d5b887185865"));
  ZuCheck(kdf.extract({}, ikm, prk));
  ZuCheck(matches(prk,
    "19ef24a32c717b167f33a91d6f648bdf96596776afdb6377ac434c1c293ccb04"));
  ZuCheck(kdf.expand(prk, {}, output));
  ZuCheck(matches(output,
    "8da4e775a563c18f715f802a063c5a31b8a11f5c5ee1879ec3454e5f3c738d2d"
    "9d201395faa4b61a96c8"));
  ZuCheck(kdf.derive({}, ikm, {}, output));
  ZuCheck(matches(output,
    "8da4e775a563c18f715f802a063c5a31b8a11f5c5ee1879ec3454e5f3c738d2d"
    "9d201395faa4b61a96c8"));
}

static void limits()
{
  ZuTestScope(limits);
  Ztls::KDF::HKDF kdf;
  ZuCheck(kdf.init());
  uint8_t prk[Ztls::KDF::HKDF::HashSize];
  ZuCheck(kdf.extract({}, {}, prk));
  ZuCheck(matches(prk,
    "b613679a0814d9ec772f95d778c35fc5ff1697c493715653c6c712144292c5ad"));
  uint8_t max[Ztls::KDF::HKDF::MaxSize];
  ZuCheck(kdf.expand(prk, {}, max));
  // Independent Python hashlib/HMAC reference, last RFC 5869 expansion block.
  ZuCheck(matches({max + sizeof(max) - sizeof(prk), sizeof(prk)},
    "d088fc3911508c67b985be209d5e5720b9e9d4d98c145952a1aa4362caf53fce"));
  ZuCheck(kdf.derive({}, {}, {}, {}));
  ZuCheck(kdf.expand(prk, {}, {}));
  uint8_t tooLarge[Ztls::KDF::HKDF::MaxSize + 1];
  memset(tooLarge, 0xa5, sizeof(tooLarge));
  ZuCheck(!kdf.expand(prk, {}, tooLarge));
  bool zero = true;
  for (auto c : tooLarge) zero &= !c;
  ZuCheck(zero);
  memset(prk, 0xa5, sizeof(prk));
  ZuCheck(!kdf.extract({}, {}, prk)); // Failure invalidated the context.
  ZuCheck(matches(prk,
    "0000000000000000000000000000000000000000000000000000000000000000"));
  ZuCheck(kdf.init());
  ZuCheck(!kdf.extract({}, {}, {prk, sizeof(prk) - 1}));
  ZuCheck(kdf.init());
  ZuCheck(kdf.extract({}, {}, prk));
}

static void scrypt()
{
  ZuTestScope(scrypt);
  uint8_t output[64];
  // RFC 7914 section 12, first test vector.
  ZuCheck(Ztls::KDF::scrypt({}, {}, 16, 1, 1, 1U << 20, output));
  ZuCheck(matches(output,
    "77d6576238657b203b19ca42c18a0497f16b4844e3074ae8dfdffa3fede21442"
    "fcd0069ded0948f8326a753a0fc81f17e8d3e0fb2e0d3628cf35e20c38d18906"));
  memset(output, 0xa5, sizeof(output));
  ZuCheck(!Ztls::KDF::scrypt({}, {}, 15, 1, 1, 1U << 20, output));
  bool zero = true;
  for (auto c : output) zero &= !c;
  ZuCheck(zero);
}

int main()
{
  ZuTestMain();
  ZuTestCall_("vectors", vectors);
  ZuTestCall_("limits", limits);
  ZuTestCall_("scrypt", scrypt);
}
