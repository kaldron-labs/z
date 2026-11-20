//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Concrete libcrypto key ownership and shared key operations

#ifndef ZtlsPKOps_HH
#define ZtlsPKOps_HH

#ifndef ZtlsLib_HH
#include <zlib/ZtlsLib.hh>
#endif

#include <openssl/types.h>

#include <zlib/ZuSpan.hh>

#include <zlib/ZtlsAlg.hh>

namespace Ztls {

enum { X25519KeySize = 32 }; // RFC 7748 raw public and private keys

namespace PK {

class Ztls_API Key {
public:
  Key() = default;
  ~Key();
  Key(const Key &) = delete;
  Key &operator=(const Key &) = delete;
  Key(Key &&) = delete;
  Key &operator=(Key &&) = delete;

  EVP_PKEY *get() const { return m_key; }
  void reset(EVP_PKEY *);
private:
  EVP_PKEY	*m_key = nullptr;
};

Ztls_Extern bool check(const Key &, bool privateKey);
Ztls_Extern bool loadPEM(Key &, const char *path);

Ztls_Extern size_t rsaSize(const Key &);
Ztls_Extern bool rsaGenerate(Key &, unsigned bits);
Ztls_Extern bool rsaImportPublic(Key &, ZuBSpan modulus, ZuBSpan pubExp);
Ztls_Extern bool rsaImportPrivate(
  Key &, ZuBSpan modulus, ZuBSpan pubExp, ZuBSpan prvExp, ZuBSpan prime1, ZuBSpan prime2,
  ZuBSpan exp1, ZuBSpan exp2, ZuBSpan coeff);
Ztls_Extern bool rsaImportSSHPrivate(
  Key &, ZuBSpan modulus, ZuBSpan pubExp, ZuBSpan prvExp, ZuBSpan coeff, ZuBSpan prime1,
  ZuBSpan prime2);
Ztls_Extern bool rsaExportPublic(const Key &, ZuSpan<uint8_t> modulus, ZuSpan<uint8_t> pubExp);
Ztls_Extern bool rsaExportPrivate(
  const Key &, ZuSpan<uint8_t> modulus, ZuSpan<uint8_t> pubExp, ZuSpan<uint8_t> prvExp,
  ZuSpan<uint8_t> prime1, ZuSpan<uint8_t> prime2, ZuSpan<uint8_t> exp1, ZuSpan<uint8_t> exp2,
  ZuSpan<uint8_t> coeff);
Ztls_Extern bool rsaOaepEncrypt(
  const Key &, ZuBSpan plaintext, ZuBSpan label, ZuSpan<uint8_t> ciphertext, size_t *length);
Ztls_Extern bool rsaOaepDecrypt(
  const Key &, ZuBSpan ciphertext, ZuBSpan label, ZuSpan<uint8_t> plaintext, size_t *length);

Ztls_Extern size_t ecKeySize(const Key &);
Ztls_Extern size_t ecPublicSize(const Key &);
Ztls_Extern size_t ecOIDSize(const Key &);
Ztls_Extern bool ecGenerate(Key &, ZuBSpan oid);
Ztls_Extern bool ecImportPublic(Key &, ZuBSpan oid, ZuBSpan pubKey);
Ztls_Extern bool ecImportPrivate(Key &, ZuBSpan oid, ZuBSpan key);
Ztls_Extern bool ecExportPrivate(const Key &, ZuSpan<uint8_t> key);
Ztls_Extern bool ecExportPublic(const Key &, ZuSpan<uint8_t> pubKey);
Ztls_Extern bool ecExportOID(const Key &, ZuSpan<uint8_t> oid);

Ztls_Extern bool ed25519Generate(Key &);
Ztls_Extern bool ed25519ImportPublic(Key &, ZuBSpan pubKey);
Ztls_Extern bool ed25519ImportPrivate(Key &, ZuBSpan key);
Ztls_Extern bool ed25519ExportPublic(const Key &, ZuSpan<uint8_t> pubKey);
Ztls_Extern bool ed25519ExportPrivate(const Key &, ZuSpan<uint8_t> key);
Ztls_Extern bool ed25519ToX25519(ZuBSpan edPublic, ZuSpan<uint8_t> xPublic);

Ztls_Extern bool x25519Generate(Key &);
Ztls_Extern bool x25519ImportPublic(Key &, ZuBSpan pubKey);
Ztls_Extern bool x25519ImportPrivate(Key &, ZuBSpan key);
Ztls_Extern bool x25519ExportPublic(const Key &, ZuSpan<uint8_t> pubKey);
Ztls_Extern bool x25519ExportPrivate(const Key &, ZuSpan<uint8_t> key);
Ztls_Extern bool x25519Agree(const Key &, ZuBSpan peerPublic, ZuSpan<uint8_t> secret);

Ztls_Extern bool mlkem768Generate(Key &);
Ztls_Extern bool mlkem768ImportSeed(Key &, ZuBSpan seed);
Ztls_Extern bool mlkem768ImportPrivate(Key &, ZuBSpan prvKey);
Ztls_Extern bool mlkem768ImportPublic(Key &, ZuBSpan pubKey);
Ztls_Extern bool mlkem768ExportPrivate(const Key &, ZuSpan<uint8_t> prvKey, size_t *length);
Ztls_Extern bool mlkem768ExportPublic(const Key &, ZuSpan<uint8_t> pubKey);
Ztls_Extern bool
mlkem768Encapsulate(const Key &, ZuSpan<uint8_t> ciphertext, ZuSpan<uint8_t> secret);
Ztls_Extern bool mlkem768Decapsulate(const Key &, ZuBSpan ciphertext, ZuSpan<uint8_t> secret);

Ztls_Extern bool signData(
  const Key &, MDType md, ZuBSpan data, ZuSpan<uint8_t> sig, size_t *siglen);
Ztls_Extern bool verifyData(const Key &, MDType md, ZuBSpan data, ZuBSpan sig);
using CSRFn = bool (*)(void *, ZuBSpan);
Ztls_Extern bool csr(const Key &, void *ctx, CSRFn);

} // namespace PK

} // namespace Ztls

// ZtlsPKOps_HH
#endif
