//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <limits.h>
#include <string.h>

#include <zlib/ZmScratch.hh>
#include <zlib/ZmVHeap.hh>

#include <zlib/ZtlsHPKE.hh>
#include <zlib/ZtlsBackend.hh>
#include <zlib/ZtlsPico.hh>

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
  ptls_hash_algorithm_t *hash, ZuBSpan salt, ZuCSpan label,
  ZuBSpan ikm, ZuSpan<uint8_t> output)
{
  size_t prefix = Version.length() + sizeof(Suite) + label.length();
  if (ikm.length() > UINT_MAX - prefix) return false;
  unsigned n = unsigned(prefix + ikm.length());
  auto buf = ZmScratch(uint8_t, n, ScratchHeap);
  if (!buf.data()) return false;
  auto p = buf.data();
  memcpy(p, Version.data(), Version.length());
  p += Version.length();
  memcpy(p, Suite, sizeof(Suite));
  p += sizeof(Suite);
  memcpy(p, label.data(), label.length());
  p += label.length();
  if (ikm) memcpy(p, ikm.data(), ikm.length());
  return !ptls_hkdf_extract(hash, output.data(),
    ptls_iovec_init(salt.data(), salt.length()),
    ptls_iovec_init(buf.data(), n));
}

static bool expand(
  ptls_hash_algorithm_t *hash, ZuBSpan prk, ZuCSpan label,
  ZuBSpan info, ZuSpan<uint8_t> output)
{
  size_t prefix = 2 + Version.length() + sizeof(Suite) + label.length();
  if (info.length() > UINT_MAX - prefix) return false;
  unsigned n = unsigned(prefix + info.length());
  auto buf = ZmScratch(uint8_t, n, ScratchHeap);
  if (!buf.data()) return false;
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
  return !ptls_hkdf_expand(hash, output.data(), output.length(),
    ptls_iovec_init(prk.data(), prk.length()),
    ptls_iovec_init(buf.data(), n));
}

static bool schedule(ZuBSpan shared, ZuBSpan info, KeyNonce &material)
{
  auto hash = Backend::hash_algorithm(SHA256);
  uint8_t secret[HashSize];
  ZuGuard clear{[&secret]() { ZuClear(secret, sizeof(secret)); }};
  uint8_t context[ContextSize];
  context[0] = 0; // mode_base
  memcpy(context + 1, PskIDHash, HashSize);
  if (!extract(hash, {}, "info_hash", info,
        {context + 1 + HashSize, HashSize}) ||
      !extract(hash, shared, "secret", {}, secret))
    return false;
  return expand(hash, secret, "key", context, material.key) &&
    expand(hash, secret, "base_nonce", context, material.nonce);
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
  Pico::AeadCtx aead;
  if (!aead.init(Backend::chacha20poly1305(), true,
      secrets.material.key, secrets.material.nonce))
    return ZeEXCEPT(Error, "ZtlsHPKE", "ChaCha20-Poly1305 setup failed");
  size_t n = ptls_aead_encrypt(aead.get(), ciphertext.data(),
    plaintext.data(), plaintext.length(), 0, aad.data(), aad.length());
  if (n != plaintext.length() + TagSize) {
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
  Pico::AeadCtx aead;
  if (!aead.init(Backend::chacha20poly1305(), false,
      secrets.material.key, secrets.material.nonce))
    return ZeEXCEPT(Error, "ZtlsHPKE", "ChaCha20-Poly1305 setup failed");
  size_t n = ptls_aead_decrypt(aead.get(), plaintext.data(),
    ciphertext.data(), ciphertext.length(), 0, aad.data(), aad.length());
  if (n != ciphertext.length() - TagSize) {
    ZuClear(plaintext.data(), ciphertext.length() - TagSize);
    return ZeEXCEPT(Error, "ZtlsHPKE", "ChaCha20-Poly1305 open failed");
  }
  return n;
}

} // Ztls::HPKE
