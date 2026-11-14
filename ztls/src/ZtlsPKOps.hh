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

class ZtlsAPI Key {
public:
  Key() = default;
  ~Key();
  Key(const Key &) = delete;
  Key &operator =(const Key &) = delete;
  Key(Key &&) = delete;
  Key &operator =(Key &&) = delete;

  EVP_PKEY *get() const { return m_key; }
  void reset(EVP_PKEY *);

private:
  EVP_PKEY	*m_key = nullptr;
};

ZtlsAPI bool check(const Key &, bool privateKey);
ZtlsAPI bool loadPEM(Key &, const char *path);

ZtlsAPI size_t pkey_rsa_size(const Key &);
ZtlsAPI bool pkey_rsa_generate(Key &, unsigned bits);
ZtlsAPI bool pkey_rsa_import_public(Key &, ZuBSpan modulus, ZuBSpan pubExp);
ZtlsAPI bool pkey_rsa_import_private(
  Key &, ZuBSpan modulus, ZuBSpan pubExp, ZuBSpan prvExp,
  ZuBSpan prime1, ZuBSpan prime2, ZuBSpan exp1, ZuBSpan exp2, ZuBSpan coeff);
ZtlsAPI bool pkey_rsa_import_ssh_private(
  Key &, ZuBSpan modulus, ZuBSpan pubExp, ZuBSpan prvExp,
  ZuBSpan coeff, ZuBSpan prime1, ZuBSpan prime2);
ZtlsAPI bool pkey_rsa_export_public(
  const Key &, ZuSpan<uint8_t> modulus, ZuSpan<uint8_t> pubExp);
ZtlsAPI bool pkey_rsa_export_private(
  const Key &, ZuSpan<uint8_t> modulus, ZuSpan<uint8_t> pubExp,
  ZuSpan<uint8_t> prvExp, ZuSpan<uint8_t> prime1, ZuSpan<uint8_t> prime2,
  ZuSpan<uint8_t> exp1, ZuSpan<uint8_t> exp2, ZuSpan<uint8_t> coeff);
ZtlsAPI bool pkey_rsa_oaep_encrypt(
  const Key &, ZuBSpan plaintext, ZuBSpan label,
  ZuSpan<uint8_t> ciphertext, size_t *length);
ZtlsAPI bool pkey_rsa_oaep_decrypt(
  const Key &, ZuBSpan ciphertext, ZuBSpan label,
  ZuSpan<uint8_t> plaintext, size_t *length);

ZtlsAPI size_t pkey_ec_key_size(const Key &);
ZtlsAPI size_t pkey_ec_public_size(const Key &);
ZtlsAPI size_t pkey_ec_oid_size(const Key &);
ZtlsAPI bool pkey_ec_generate(Key &, ZuBSpan oid);
ZtlsAPI bool pkey_ec_import_public(Key &, ZuBSpan oid, ZuBSpan pubKey);
ZtlsAPI bool pkey_ec_import_private(Key &, ZuBSpan oid, ZuBSpan key);
ZtlsAPI bool pkey_ec_export_private(const Key &, ZuSpan<uint8_t> key);
ZtlsAPI bool pkey_ec_export_public(const Key &, ZuSpan<uint8_t> pubKey);
ZtlsAPI bool pkey_ec_export_oid(const Key &, ZuSpan<uint8_t> oid);

ZtlsAPI bool pkey_ed25519_generate(Key &);
ZtlsAPI bool pkey_ed25519_import_public(Key &, ZuBSpan pubKey);
ZtlsAPI bool pkey_ed25519_import_private(Key &, ZuBSpan key);
ZtlsAPI bool pkey_ed25519_export_public(const Key &, ZuSpan<uint8_t> pubKey);
ZtlsAPI bool pkey_ed25519_export_private(const Key &, ZuSpan<uint8_t> key);
ZtlsAPI bool ed25519_to_x25519(
  ZuBSpan edPublic, ZuSpan<uint8_t> xPublic);

ZtlsAPI bool pkey_x25519_generate(Key &);
ZtlsAPI bool pkey_x25519_import_public(Key &, ZuBSpan pubKey);
ZtlsAPI bool pkey_x25519_import_private(Key &, ZuBSpan key);
ZtlsAPI bool pkey_x25519_export_public(const Key &, ZuSpan<uint8_t> pubKey);
ZtlsAPI bool pkey_x25519_export_private(const Key &, ZuSpan<uint8_t> key);
ZtlsAPI bool pkey_x25519_agree(
  const Key &, ZuBSpan peerPublic, ZuSpan<uint8_t> secret);

ZtlsAPI bool pkey_mlkem768_generate(Key &);
ZtlsAPI bool pkey_mlkem768_import_seed(Key &, ZuBSpan seed);
ZtlsAPI bool pkey_mlkem768_import_private(Key &, ZuBSpan prvKey);
ZtlsAPI bool pkey_mlkem768_import_public(Key &, ZuBSpan pubKey);
ZtlsAPI bool pkey_mlkem768_export_private(
  const Key &, ZuSpan<uint8_t> prvKey, size_t *length);
ZtlsAPI bool pkey_mlkem768_export_public(
  const Key &, ZuSpan<uint8_t> pubKey);
ZtlsAPI bool pkey_mlkem768_encapsulate(
  const Key &, ZuSpan<uint8_t> ciphertext, ZuSpan<uint8_t> secret);
ZtlsAPI bool pkey_mlkem768_decapsulate(
  const Key &, ZuBSpan ciphertext, ZuSpan<uint8_t> secret);

ZtlsAPI bool pkey_sign(const Key &, MDType md, ZuBSpan data,
  ZuSpan<uint8_t> sig, size_t *siglen);
ZtlsAPI bool pkey_verify(const Key &, MDType md, ZuBSpan data, ZuBSpan sig);
using CSRFn = bool (*)(void *, ZuBSpan);
ZtlsAPI bool pkey_csr(const Key &, void *ctx, CSRFn);

} // PK

} // Ztls

#endif /* ZtlsPKOps_HH */
