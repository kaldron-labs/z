//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <limits.h>
#include <string.h>

#include <zlib/ZmScratch.hh>
#include <zlib/ZmVHeap.hh>

#include <zlib/ZtlsHPKE.hh>
#include <zlib/ZtlsKDF.hh>
#include <zlib/ZtlsAEAD.hh>

namespace Ztls::HPKE {

using ScratchHeap = ZmVHeap<"Ztls.HPKE.Scratch">;

enum {
  HashSize = 32,
  KeySize = 32,
  NonceSize = 12,
  ContextSize = 1 + 2 * HashSize
};

constexpr auto Version = "HPKE-v1"_z;
constexpr uint8_t Suite[] = {
  'H', 'P', 'K', 'E', 0x64, 0x7a, 0x00, 0x01, 0x00, 0x03
};
// LabeledExtract("", "psk_id_hash", "") for this fixed suite.
constexpr uint8_t PskIDHash[HashSize] = {
  0x36, 0xb5, 0xff, 0x55, 0x87, 0xa0, 0x30, 0x0f,
  0xc5, 0x55, 0x73, 0x47, 0xee, 0x59, 0x90, 0x66,
  0xaf, 0x3e, 0xd8, 0x97, 0x74, 0x5f, 0x8c, 0xa8,
  0xca, 0x19, 0xb4, 0x5f, 0x13, 0xab, 0xd9, 0x77
};

struct KeyNonce {
  uint8_t key[KeySize];
  uint8_t nonce[NonceSize];
};

static bool extract(
  KDF::HKDF &kdf, ZuBSpan salt, ZuCSpan label,
  ZuBSpan ikm, ZuSpan<uint8_t> output)
{
  size_t prefix = Version.length() + sizeof(Suite) + label.length();
  if (ikm.length() > UINT_MAX - prefix) return false;
  unsigned n = unsigned(prefix + ikm.length());
  auto buf = ZmScratch(uint8_t, n, ScratchHeap);
  auto p = buf.data();
  memcpy(p, Version.data(), Version.length());
  p += Version.length();
  memcpy(p, Suite, sizeof(Suite));
  p += sizeof(Suite);
  memcpy(p, label.data(), label.length());
  p += label.length();
  if (ikm) memcpy(p, ikm.data(), ikm.length());
  return kdf.extract(salt, {buf.data(), n}, output);
}

static bool expand(
  KDF::HKDF &kdf, ZuBSpan prk, ZuCSpan label,
  ZuBSpan info, ZuSpan<uint8_t> output)
{
  size_t prefix = 2 + Version.length() + sizeof(Suite) + label.length();
  if (info.length() > UINT_MAX - prefix) return false;
  unsigned n = unsigned(prefix + info.length());
  auto buf = ZmScratch(uint8_t, n, ScratchHeap);
  auto p = buf.data();
  p[0] = uint8_t(output.length() >> 8);
  p[1] = uint8_t(output.length());
  p += 2;
  memcpy(p, Version.data(), Version.length());
  p += Version.length();
  memcpy(p, Suite, sizeof(Suite));
  p += sizeof(Suite);
  memcpy(p, label.data(), label.length());
  p += label.length();
  if (info) memcpy(p, info.data(), info.length());
  return kdf.expand(prk, {buf.data(), n}, output);
}

static bool schedule(ZuBSpan shared, ZuBSpan info, KeyNonce &material)
{
  KDF::HKDF kdf;
  if (!kdf.init()) return false;
  uint8_t secret[HashSize];
  ZuGuard clear{[&secret]() { ZuClear(secret, sizeof(secret)); }};
  uint8_t context[ContextSize];
  context[0] = 0; // mode_base
  memcpy(context + 1, PskIDHash, HashSize);
  if (!extract(kdf, {}, "info_hash", info,
        {context + 1 + HashSize, HashSize}) ||
      !extract(kdf, shared, "secret", {}, secret))
    return false;
  return expand(kdf, secret, "key", context, material.key) &&
    expand(kdf, secret, "base_nonce", context, material.nonce);
}

ZuUnion<void, ZeException> sealBase(
  Random &rng, const PK::PK_MLKEM768_X25519 &recipient,
  ZuBSpan info, ZuBSpan aad, ZuBSpan plaintext,
  ZuSpan<uint8_t> enc, ZuSpan<uint8_t> ciphertext)
{
  if (plaintext.length() > SIZE_MAX - TagSize ||
      ciphertext.length() < plaintext.length() + TagSize)
    return ZeEXCEPT(Error, "ZtlsHPKE", "insufficient ciphertext capacity");
  struct {
    uint8_t shared[PK::HybridSecretSize];
    KeyNonce material;
  } secrets;
  ZuGuard clear{[&secrets]() { ZuClear(&secrets, sizeof(secrets)); }};
  auto r = recipient.encapsulate(rng, enc, secrets.shared);
  if (r.template is<ZeException>())
    return ZuMv(r).template p<ZeException>();
  if (!schedule(secrets.shared, info, secrets.material))
    return ZeEXCEPT(Error, "ZtlsHPKE", "base-mode key schedule failed");
  ChaCha20Poly1305 aead;
  if (!aead.init(true, secrets.material.key))
    return ZeEXCEPT(Error, "ZtlsHPKE", "ChaCha20-Poly1305 setup failed");
  if (!aead.seal(secrets.material.nonce, aad, plaintext, ciphertext)) {
    ZuClear(ciphertext.data(), plaintext.length() + TagSize);
    return ZeEXCEPT(Error, "ZtlsHPKE", "ChaCha20-Poly1305 seal failed");
  }
  return {};
}

ZuUnion<size_t, ZeException> openBase(
  const PK::SK_MLKEM768_X25519 &identity,
  ZuBSpan info, ZuBSpan aad, ZuBSpan enc, ZuBSpan ciphertext,
  ZuSpan<uint8_t> plaintext)
{
  if (ciphertext.length() < TagSize ||
      plaintext.length() < ciphertext.length() - TagSize)
    return ZeEXCEPT(Error, "ZtlsHPKE", "insufficient plaintext capacity");
  size_t size = ciphertext.length() - TagSize;
  ZuGuard clearOutput{[output = plaintext.data(), size]() { ZuClear(output, size); }};
  struct {
    uint8_t shared[PK::HybridSecretSize];
    KeyNonce material;
  } secrets;
  ZuGuard clear{[&secrets]() { ZuClear(&secrets, sizeof(secrets)); }};
  auto r = identity.decapsulate(enc, secrets.shared);
  if (r.template is<ZeException>())
    return ZuMv(r).template p<ZeException>();
  if (!schedule(secrets.shared, info, secrets.material))
    return ZeEXCEPT(Error, "ZtlsHPKE", "base-mode key schedule failed");
  ChaCha20Poly1305 aead;
  if (!aead.init(false, secrets.material.key))
    return ZeEXCEPT(Error, "ZtlsHPKE", "ChaCha20-Poly1305 setup failed");
  if (!aead.open(secrets.material.nonce, aad, ciphertext, plaintext))
    return ZeEXCEPT(Error, "ZtlsHPKE", "ChaCha20-Poly1305 open failed");
  clearOutput.cancel();
  return size;
}

} // Ztls::HPKE
