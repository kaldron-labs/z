//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// Copyright (c) 2016 DeNA Co., Ltd., Kazuho Oku
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.

#include <assert.h>
#include <stdlib.h>
#include <string.h>

// Low-level key operations remain part of the libcrypto 3.5 contract.
#define OPENSSL_SUPPRESS_DEPRECATED
#include <openssl/bn.h>
#include <openssl/crypto.h>
#include <openssl/ec.h>
#include <openssl/ecdh.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/x509_vfy.h>

#include <zlib/ZuAssert.hh>

#include <zlib/ZmHeap.hh>

#include <zlib/ZiLog.hh>

#include <zlib/ZtlsCxn.hh>
#include <zlib/ZtlsEngineOpenSSL.hh>
#include <zlib/ZtlsRandom.hh>
#include <zlib/ZtlsSec.hh>

namespace Ztls_ {

template <int Bits> struct OpenSSLSignature : OpenSSLSigScheme {
  using OpenSSLSigScheme::OpenSSLSigScheme;
  const EVP_MD *md() const override
  {
    if constexpr (Bits == 256)
      return EVP_sha256();
    else if constexpr (Bits == 384)
      return EVP_sha384();
    else if constexpr (Bits == 512)
      return EVP_sha512();
    else
      return nullptr; // Ed25519 hashes internally.
  }
};

static const OpenSSLSignature<256>   rsa256{Ztls_SigRSAPSSRSAESHA256};
static const OpenSSLSignature<384>   rsa384{Ztls_SigRSAPSSRSAESHA384};
static const OpenSSLSignature<512>   rsa512{Ztls_SigRSAPSSRSAESHA512};
static const OpenSSLSigScheme *const rsa_signature_schemes[] = {&rsa256, &rsa384, &rsa512};
static const OpenSSLSignature<256>   ec256{Ztls_SigECDSAP256SHA256};
static const OpenSSLSigScheme *const secp256r1_signature_schemes[] = {&ec256};
#if ZtlsOpenSSLHaveSecp384r1
static const OpenSSLSignature<384>   ec384{Ztls_SigECDSAP384SHA384};
static const OpenSSLSigScheme *const secp384r1_signature_schemes[] = {&ec384};
#endif
#if ZtlsOpenSSLHaveSecp521r1
static const OpenSSLSignature<512>   ec512{Ztls_SigECDSAP521SHA512};
static const OpenSSLSigScheme *const secp521r1_signature_schemes[] = {&ec512};
#endif
#if ZtlsOpenSSLHaveEd25519
static const OpenSSLSignature<0>     ed25519{Ztls_SigEd25519};
static const OpenSSLSigScheme *const ed25519_signature_schemes[] = {&ed25519};
#endif

// The default list sent in ClientHello.signatureAlgorithms. ECDSA certificates are preferred.
static const uint16_t default_signature_schemes[] = {
#if ZtlsOpenSSLHaveEd25519
  Ztls_SigEd25519,
#endif
  Ztls_SigECDSAP256SHA256,
#if ZtlsOpenSSLHaveSecp384r1
  Ztls_SigECDSAP384SHA384,
#endif
#if ZtlsOpenSSLHaveSecp521r1
  Ztls_SigECDSAP521SHA512,
#endif
  Ztls_SigRSAPSSRSAESHA512, Ztls_SigRSAPSSRSAESHA384, Ztls_SigRSAPSSRSAESHA256};

ZuSpan<const OpenSSLSigScheme *const> openssl_lookup_sig_schemes(EVP_PKEY *key)
{
  ZuSpan<const OpenSSLSigScheme *const> schemes;

  switch (EVP_PKEY_id(key)) {
  case EVP_PKEY_RSA:
    schemes = rsa_signature_schemes;
    break;
  case EVP_PKEY_EC: {
    EC_KEY *eckey = EVP_PKEY_get1_EC_KEY(key);
    switch (EC_GROUP_get_curve_name(EC_KEY_get0_group(eckey))) {
    case NID_X9_62_prime256v1:
      schemes = secp256r1_signature_schemes;
      break;
#if ZtlsOpenSSLHaveSecp384r1
    case NID_secp384r1:
      schemes = secp384r1_signature_schemes;
      break;
#endif
#if ZtlsOpenSSLHaveSecp521r1
    case NID_secp521r1:
      schemes = secp521r1_signature_schemes;
      break;
#endif
    default:
      break;
    }
    EC_KEY_free(eckey);
  } break;
#if ZtlsOpenSSLHaveEd25519
  case EVP_PKEY_ED25519:
    schemes = ed25519_signature_schemes;
    break;
#endif
  default:
    break;
  }

  return schemes;
}

const OpenSSLSigScheme *openssl_select_sig_scheme(
  ZuSpan<const OpenSSLSigScheme *const> available, ZuSpan<const uint16_t> algorithms)
{
  for (auto scheme : available)
    for (auto algorithm : algorithms)
      if (algorithm == scheme->schemeId) return scheme;

  return NULL;
}

struct OpenSSLRandom : RandomSource {
  void fill(void *buf, size_t len) override
  {
    Ztls::Random random;
    if (!random.random({static_cast<uint8_t *>(buf), len})) {
      ZiLOG(Fatal, "Ztls", "random byte generation failed");
      abort();
    }
  }
};
static OpenSSLRandom randomSource;
RandomSource	    &opensslRandom = randomSource;

static EC_KEY *ecdh_generate_key(EC_GROUP *group)
{
  EC_KEY *key;

  if ((key = EC_KEY_new()) == NULL) return NULL;
  if (!EC_KEY_set_group(key, group) || !EC_KEY_generate_key(key)) {
    EC_KEY_free(key);
    return NULL;
  }

  return key;
}

static int ecdh_calc_secret(
  KeyExchangeBytes *out, const EC_GROUP *group, EC_KEY *privkey, EC_POINT *peer_point)
{
  KeyExchangeBytes secret;
  secret.length((EC_GROUP_get_degree(group) + 7)/8);
  if (ECDH_compute_key(secret.data(), secret.length(), peer_point, privkey, NULL) <= 0) {
    out->null();
    return Ztls_AlertHSFailure;
  }
  *out = ZuMv(secret);
  return 0;
}

static EC_POINT *x9_62_decode_point(const EC_GROUP *group, Bytes vec, BN_CTX *bnCtx)
{
  EC_POINT *point = NULL;

  if ((point = EC_POINT_new(group)) == NULL) return NULL;
  if (!EC_POINT_oct2point(group, point, vec.data(), vec.length(), bnCtx)) {
    EC_POINT_free(point);
    return NULL;
  }

  return point;
}

static KeyExchangeBytes
x9_62_encode_point(const EC_GROUP *group, const EC_POINT *point, BN_CTX *bnCtx)
{
  auto length = EC_POINT_point2oct(group, point, POINT_CONVERSION_UNCOMPRESSED, NULL, 0, bnCtx);
  if (!length) return {};
  KeyExchangeBytes point_bytes(length, length);
  if (EC_POINT_point2oct(
	group, point, POINT_CONVERSION_UNCOMPRESSED, point_bytes.data(), length, bnCtx) != length)
    return {};
  return point_bytes;
}

struct ECKeyExchangeContext : ZmNHeap<ECKeyExchangeContext>, KeyExchangeContext {
friend inline ZuStringT<"Ztls.Engine.ECKeyExchange"> ZmNHeapID(ECKeyExchangeContext *);
  BN_CTX		*bnCtx = nullptr;
  EC_KEY		*privkey = nullptr;
  KeyExchangeBytes	pubkeyBytes;

  explicit ECKeyExchangeContext(const KeyExchangeAlgo *algo) : KeyExchangeContext{algo} { }
  ~ECKeyExchangeContext() override
  {
    EC_KEY_free(privkey);
    BN_CTX_free(bnCtx);
  }
  int exchange(KeyExchangeBytes *secret, Bytes peerkey) override;
};

int ECKeyExchangeContext::exchange(KeyExchangeBytes *secret, Bytes peerkey)
{
  auto		  ctx = this;
  const EC_GROUP *group = EC_KEY_get0_group(ctx->privkey);
  EC_POINT	 *peer_point = NULL;
  int		  ret;

  if (secret == NULL) {
    ret = 0;
    goto Exit;
  }

  if ((peer_point = x9_62_decode_point(group, peerkey, ctx->bnCtx)) == NULL) {
    ret = Ztls_AlertDecodeError;
    goto Exit;
  }
  if ((ret = ecdh_calc_secret(secret, group, ctx->privkey, peer_point)) != 0) goto Exit;

Exit:
  if (peer_point != NULL) EC_POINT_free(peer_point);
  return ret;
}

static int x9_62_create_context(const KeyExchangeAlgo *algo, ECKeyExchangeContext **ctx)
{
  int ret;

  *ctx = new ECKeyExchangeContext{algo};

  if (((*ctx)->bnCtx = BN_CTX_new()) == NULL) {
    ret = Ztls_ErrorNoMemory;
    goto Exit;
  }

  ret = 0;
Exit:
  if (ret != 0 && *ctx != NULL) {
    delete *ctx;
    *ctx = NULL;
  }
  return ret;
}

static int x9_62_setup_pubkey(ECKeyExchangeContext *ctx)
{
  const EC_GROUP *group = EC_KEY_get0_group(ctx->privkey);
  const EC_POINT *pubkey = EC_KEY_get0_public_key(ctx->privkey);
  ctx->pubkeyBytes = x9_62_encode_point(group, pubkey, ctx->bnCtx);
  if (!ctx->pubkeyBytes.length()) return Ztls_ErrorNoMemory;
  ctx->pubkey = wire_bytes(ctx->pubkeyBytes.data(), ctx->pubkeyBytes.length());
  return 0;
}

static int x9_62_create_key_exchange(const KeyExchangeAlgo *algo, KeyExchangeContext **_ctx)
{
  EC_GROUP	       *group = NULL;
  ECKeyExchangeContext *ctx = NULL;
  int			ret;

  // FIXME use a global?
  if ((group = EC_GROUP_new_by_curve_name((int)algo->data)) == NULL) {
    ret = Ztls_ErrorLibrary;
    goto Exit;
  }
  if ((ret = x9_62_create_context(algo, &ctx)) != 0) goto Exit;
  if ((ctx->privkey = ecdh_generate_key(group)) == NULL) {
    ret = Ztls_ErrorLibrary;
    goto Exit;
  }
  if ((ret = x9_62_setup_pubkey(ctx)) != 0) goto Exit;
  ret = 0;

Exit:
  if (group != NULL) EC_GROUP_free(group);
  if (ret == 0) {
    *_ctx = ctx;
  } else {
    if (ctx != NULL) delete ctx;
    *_ctx = NULL;
  }

  return ret;
}

static int x9_62_init_key(const KeyExchangeAlgo *algo, KeyExchangeContext **_ctx, EC_KEY *eckey)
{
  ECKeyExchangeContext *ctx = NULL;
  int			ret;

  if ((ret = x9_62_create_context(algo, &ctx)) != 0) goto Exit;
  ctx->privkey = eckey;
  if ((ret = x9_62_setup_pubkey(ctx)) != 0) goto Exit;
  ret = 0;

Exit:
  if (ret == 0) {
    *_ctx = ctx;
  } else {
    if (ctx != NULL) {
      // Ownership transfers only on success; the caller releases its retained reference on error.
      ctx->privkey = nullptr;
      delete ctx;
    }
    *_ctx = NULL;
  }
  return ret;
}

static int x9_62_key_exchange(
  EC_GROUP *group, KeyExchangeBytes *pubkey, KeyExchangeBytes *secret, Bytes peerkey,
  BN_CTX *bnCtx)
{
  EC_POINT *peer_point = NULL;
  EC_KEY   *privkey = NULL;
  int	    ret;

  pubkey->null();
  secret->null();

  // decode peer key
  if ((peer_point = x9_62_decode_point(group, peerkey, bnCtx)) == NULL) {
    ret = Ztls_AlertDecodeError;
    goto Exit;
  }

  // create private key
  if ((privkey = ecdh_generate_key(group)) == NULL) {
    ret = Ztls_ErrorNoMemory;
    goto Exit;
  }

  // encode public key
  *pubkey = x9_62_encode_point(group, EC_KEY_get0_public_key(privkey), bnCtx);
  if (!pubkey->length()) {
    ret = Ztls_ErrorNoMemory;
    goto Exit;
  }

  ret = ecdh_calc_secret(secret, group, privkey, peer_point);

Exit:
  if (peer_point != NULL) EC_POINT_free(peer_point);
  if (privkey != NULL) EC_KEY_free(privkey);
  if (ret != 0) {
    pubkey->null();
    secret->null();
  }
  return ret;
}

static int secp_key_exchange(
  const KeyExchangeAlgo *algo, KeyExchangeBytes *pubkey, KeyExchangeBytes *secret, Bytes peerkey)
{
  EC_GROUP *group = NULL;
  BN_CTX   *bnCtx = NULL;
  int	    ret;

  if ((group = EC_GROUP_new_by_curve_name((int)algo->data)) == NULL) {
    ret = Ztls_ErrorLibrary;
    goto Exit;
  }
  if ((bnCtx = BN_CTX_new()) == NULL) {
    ret = Ztls_ErrorNoMemory;
    goto Exit;
  }

  ret = x9_62_key_exchange(group, pubkey, secret, peerkey, bnCtx);

Exit:
  if (bnCtx != NULL) BN_CTX_free(bnCtx);
  if (group != NULL) EC_GROUP_free(group);
  return ret;
}

struct EVPKeyExchangeContext : ZmNHeap<EVPKeyExchangeContext>, KeyExchangeContext {
friend inline ZuStringT<"Ztls.Engine.EVPKeyExchange"> ZmNHeapID(EVPKeyExchangeContext *);
  EVP_PKEY	*privkey;
  bool		kem;

  EVPKeyExchangeContext(const KeyExchangeAlgo *algo, EVP_PKEY *key, bool kem_)
  :
    KeyExchangeContext{algo}, privkey{key}, kem{kem_}
  { }
  ~EVPKeyExchangeContext() override
  {
    EVP_PKEY_free(privkey);
    // get1_tls_encodedpoint/get1_encoded_public_key transfer OpenSSL-owned backing.
    OPENSSL_free(pubkey.data());
  }
  int exchange(KeyExchangeBytes *secret, Bytes peerkey) override;
};

#if ZtlsOpenSSLHaveX25519

static int evp_keyex_derive(EVPKeyExchangeContext *ctx, KeyExchangeBytes *secret, Bytes peerkey)
{
  EVP_PKEY     *evppeer = NULL;
  EVP_PKEY_CTX *evpctx = NULL;
  size_t	length = 0;
  int		ret;

  if (secret == NULL) {
    ret = 0;
    goto Exit;
  }

  secret->null();

  if (peerkey.length() != ctx->pubkey.length()) {
    ret = Ztls_AlertDecryptError;
    goto Exit;
  }

  if ((evppeer = EVP_PKEY_new()) == NULL) {
    ret = Ztls_ErrorNoMemory;
    goto Exit;
  }
  if (EVP_PKEY_copy_parameters(evppeer, ctx->privkey) <= 0) {
    ret = Ztls_ErrorLibrary;
    goto Exit;
  }
  if (EVP_PKEY_set1_tls_encodedpoint(evppeer, peerkey.data(), peerkey.length()) <= 0) {
    ret = Ztls_ErrorLibrary;
    goto Exit;
  }
  if ((evpctx = EVP_PKEY_CTX_new(ctx->privkey, NULL)) == NULL) {
    ret = Ztls_ErrorLibrary;
    goto Exit;
  }
  if (EVP_PKEY_derive_init(evpctx) <= 0) {
    ret = Ztls_ErrorLibrary;
    goto Exit;
  }
  if (EVP_PKEY_derive_set_peer(evpctx, evppeer) <= 0) {
    ret = Ztls_ErrorLibrary;
    goto Exit;
  }
  if (EVP_PKEY_derive(evpctx, NULL, &length) <= 0) {
    ret = Ztls_ErrorLibrary;
    goto Exit;
  }
  secret->length(length);
  if (EVP_PKEY_derive(evpctx, secret->data(), &length) <= 0) {
    ret = Ztls_ErrorLibrary;
    goto Exit;
  }

  if (secret->length() > length) {
    ZuClear(secret->data() + length, secret->length() - length);
    secret->length(length);
  }
  ret = 0;
Exit:
  if (evpctx != NULL) EVP_PKEY_CTX_free(evpctx);
  if (evppeer != NULL) EVP_PKEY_free(evppeer);
  if (ret != 0 && secret) secret->null();
  return ret;
}

// Upon success, ownership of `pkey` is transferred to the object being created. Otherwise, the
// refcount remains unchanged.
static int evp_keyex_init(const KeyExchangeAlgo *algo, KeyExchangeContext **_ctx, EVP_PKEY *pkey)
{
  EVPKeyExchangeContext *ctx = NULL;
  uint8_t		*key_bytes = nullptr;
  size_t		 length;
  int			 ret;

  ctx = new EVPKeyExchangeContext{algo, pkey, false};

  // set public key
  if (!(length = EVP_PKEY_get1_tls_encodedpoint(ctx->privkey, &key_bytes))) {
    OPENSSL_free(key_bytes);
    ret = Ztls_ErrorNoMemory;
    goto Exit;
  }

  ctx->pubkey = {key_bytes, length};
  *_ctx = ctx;
  ret = 0;
Exit:
  if (ret != 0 && ctx != NULL) {
    ctx->privkey = NULL;
    // do not decrement refcount of pkey in case of error
    delete ctx;
  }
  return ret;
}

static int evp_keyex_create(const KeyExchangeAlgo *algo, KeyExchangeContext **ctx)
{
  EVP_PKEY_CTX *evpctx = NULL;
  EVP_PKEY     *pkey = NULL;
  int		ret;

  // generate private key
  if ((evpctx = EVP_PKEY_CTX_new_id((int)algo->data, NULL)) == NULL) {
    ret = Ztls_ErrorLibrary;
    goto Exit;
  }
  if (EVP_PKEY_keygen_init(evpctx) <= 0) {
    ret = Ztls_ErrorLibrary;
    goto Exit;
  }
  if (EVP_PKEY_keygen(evpctx, &pkey) <= 0) {
    ret = Ztls_ErrorLibrary;
    goto Exit;
  }

  // setup
  if ((ret = evp_keyex_init(algo, ctx, pkey)) != 0) goto Exit;
  pkey = NULL;
  ret = 0;

Exit:
  if (pkey != NULL) EVP_PKEY_free(pkey);
  if (evpctx != NULL) EVP_PKEY_CTX_free(evpctx);
  return ret;
}

static int evp_keyex_exchange(
  const KeyExchangeAlgo *algo, KeyExchangeBytes *outpubkey, KeyExchangeBytes *secret,
  Bytes peerkey)
{
  KeyExchangeContext *ctx = NULL;
  int		      ret;

  outpubkey->null();

  if ((ret = evp_keyex_create(algo, &ctx)) != 0) goto Exit;
  outpubkey->copy(ctx->pubkey.data(), ctx->pubkey.length());
  ret = ctx->exchange(secret, peerkey);

Exit:
  delete ctx;
  if (ret != 0) outpubkey->null();
  return ret;
}

#endif

static int evp_kem_derive(EVPKeyExchangeContext *ctx, KeyExchangeBytes *secret, Bytes ciphertext)
{
  EVP_PKEY_CTX *evpctx = NULL;
  size_t	length = 0;
  int		ret;

  if (secret == NULL) {
    ret = 0;
    goto Exit;
  }

  secret->null();

  if ((evpctx = EVP_PKEY_CTX_new_from_pkey(NULL, ctx->privkey, NULL)) == NULL) {
    ret = Ztls_ErrorLibrary;
    goto Exit;
  }
  if (EVP_PKEY_decapsulate_init(evpctx, NULL) <= 0) {
    ret = Ztls_ErrorLibrary;
    goto Exit;
  }
  if (EVP_PKEY_decapsulate(evpctx, NULL, &length, ciphertext.data(), ciphertext.length()) <= 0) {
    ret = Ztls_ErrorLibrary;
    goto Exit;
  }
  secret->length(length);
  if (EVP_PKEY_decapsulate(
	evpctx, secret->data(), &length, ciphertext.data(), ciphertext.length()) <= 0) {
    ret = Ztls_ErrorLibrary;
    goto Exit;
  }
  if (secret->length() > length) ZuClear(secret->data() + length, secret->length() - length);
  secret->length(length);
  ret = 0;

Exit:
  if (evpctx != NULL) EVP_PKEY_CTX_free(evpctx);
  if (ret != 0 && secret) secret->null();
  return ret;
}

int EVPKeyExchangeContext::exchange(KeyExchangeBytes *secret, Bytes peerkey)
{
#if ZtlsOpenSSLHaveX25519
  if (!kem) return evp_keyex_derive(this, secret, peerkey);
#endif
  return evp_kem_derive(this, secret, peerkey);
}

static int evp_kem_create(const KeyExchangeAlgo *algo, KeyExchangeContext **_ctx)
{
  EVPKeyExchangeContext *ctx = NULL;
  EVP_PKEY_CTX		*evpctx = NULL;
  uint8_t		*key_bytes = nullptr;
  size_t		 length;
  int			 ret;

  ctx = new EVPKeyExchangeContext{algo, nullptr, true};

  // generate private key
  if ((evpctx = EVP_PKEY_CTX_new_from_name(NULL, (const char *)algo->data, NULL)) == NULL) {
    ret = Ztls_ErrorLibrary;
    goto Exit;
  }
  if (EVP_PKEY_keygen_init(evpctx) <= 0) {
    ret = Ztls_ErrorLibrary;
    goto Exit;
  }
  if (EVP_PKEY_generate(evpctx, &ctx->privkey) <= 0) {
    ret = Ztls_ErrorLibrary;
    goto Exit;
  }

  // set public key
  if (!(length = EVP_PKEY_get1_encoded_public_key(ctx->privkey, &key_bytes))) {
    OPENSSL_free(key_bytes);
    ret = Ztls_ErrorNoMemory;
    goto Exit;
  }

  ctx->pubkey = {key_bytes, length};
  *_ctx = ctx;
  ret = 0;

Exit:
  if (ret != 0 && ctx != NULL) delete ctx;
  if (evpctx != NULL) EVP_PKEY_CTX_free(evpctx);
  return ret;
}

static int evp_kem_exchange(
  const KeyExchangeAlgo *algo, KeyExchangeBytes *ciphertext, KeyExchangeBytes *secret,
  Bytes peerkey)
{
  EVP_PKEY_CTX *evpctx = NULL;
  EVP_PKEY     *key = NULL;
  size_t	ciphertext_length = 0, secret_length = 0;
  int		ret;

  ciphertext->null();
  secret->null();

  if ((evpctx = EVP_PKEY_CTX_new_from_name(NULL, (const char *)algo->data, NULL)) == NULL) {
    ret = Ztls_ErrorLibrary;
    goto Exit;
  }
  if (EVP_PKEY_paramgen_init(evpctx) <= 0) {
    ret = Ztls_ErrorLibrary;
    goto Exit;
  }
  if (EVP_PKEY_paramgen(evpctx, &key) <= 0) {
    ret = Ztls_ErrorLibrary;
    goto Exit;
  }
  if (EVP_PKEY_set1_encoded_public_key(key, peerkey.data(), peerkey.length()) <= 0) {
    ret = Ztls_ErrorLibrary;
    goto Exit;
  }

  EVP_PKEY_CTX_free(evpctx);
  evpctx = EVP_PKEY_CTX_new_from_pkey(NULL, key, NULL);
  if (evpctx == NULL) {
    ret = Ztls_ErrorLibrary;
    goto Exit;
  }
  if (EVP_PKEY_encapsulate_init(evpctx, NULL) <= 0) {
    ret = Ztls_ErrorLibrary;
    goto Exit;
  }
  if (EVP_PKEY_encapsulate(evpctx, NULL, &ciphertext_length, NULL, &secret_length) <= 0) {
    ret = Ztls_ErrorLibrary;
    goto Exit;
  }
  ciphertext->length(ciphertext_length);
  secret->length(secret_length);
  if (EVP_PKEY_encapsulate(
	evpctx, ciphertext->data(), &ciphertext_length, secret->data(), &secret_length) <= 0) {
    ret = Ztls_ErrorLibrary;
    goto Exit;
  }
  if (secret->length() > secret_length)
    ZuClear(secret->data() + secret_length, secret->length() - secret_length);
  secret->length(secret_length);
  ciphertext->length(ciphertext_length);
  ret = 0;

Exit:
  if (evpctx != NULL) EVP_PKEY_CTX_free(evpctx);
  if (key != NULL) EVP_PKEY_free(key);
  if (ret != 0) {
    secret->null();
    ciphertext->null();
  }
  return ret;
}

int openssl_create_key_exchange(KeyExchangeContext **ctx, EVP_PKEY *pkey)
{
  int ret, id;

  switch (id = EVP_PKEY_id(pkey)) {
  case EVP_PKEY_EC: {
    // obtain eckey
    EC_KEY *eckey = EVP_PKEY_get1_EC_KEY(pkey);

    // determine algo
    const KeyExchangeAlgo *algo;
    switch (EC_GROUP_get_curve_name(EC_KEY_get0_group(eckey))) {
    case NID_X9_62_prime256v1:
      algo = &openssl_secp256r1;
      break;
#if ZtlsOpenSSLHaveSecp384r1
    case NID_secp384r1:
      algo = &openssl_secp384r1;
      break;
#endif
#if ZtlsOpenSSLHaveSecp521r1
    case NID_secp521r1:
      algo = &openssl_secp521r1;
      break;
#endif
    default:
      EC_KEY_free(eckey);
      return Ztls_ErrorIncompatibleKey;
    }

    // load key
    if ((ret = x9_62_init_key(algo, ctx, eckey)) != 0) {
      EC_KEY_free(eckey);
      return ret;
    }

    return 0;
  } break;

#if ZtlsOpenSSLHaveX25519
  case NID_X25519:
    if ((ret = evp_keyex_init(&openssl_x25519, ctx, pkey)) != 0) return ret;
    EVP_PKEY_up_ref(pkey);
    return 0;
#endif

  default:
    return Ztls_ErrorIncompatibleKey;
  }
}

#if ZtlsOpenSSLHaveAsync

static void releaseSignDigest(Ztls::Cxn *tls)
{
  EVP_MD_CTX_destroy(tls->signatureDigest);
  ASYNC_WAIT_CTX_free(tls->signatureWait);
  tls->signatureDigest = nullptr;
  tls->signatureWait = nullptr;
  tls->signatureJob = nullptr;
  tls->signatureLength = 0;
  tls->signatureAlgorithm = 0;
  tls->signature.null();
}

static int sign_digest_job(void *arg)
{
  auto tls = *static_cast<Ztls::Cxn **>(arg);
  return EVP_DigestSignFinal(tls->signatureDigest, tls->signature.data(), &tls->signatureLength);
}

static void updateSignatureHandle(Ztls::Cxn *tls)
{
#ifndef _WIN32
  size_t	count = 0;
  OSSL_ASYNC_FD fd;
  tls->signatureFd = -1;
  if (ASYNC_WAIT_CTX_get_all_fds(tls->signatureWait, nullptr, &count) && count == 1 &&
    ASYNC_WAIT_CTX_get_all_fds(tls->signatureWait, &fd, &count))
    tls->signatureFd = fd;
#endif
}

static bool retireSignDigest(Ztls::Cxn *tls)
{
  if (tls->signatureJob) {
    int result;
    int status = ASYNC_start_job(
      &tls->signatureJob, tls->signatureWait, &result, sign_digest_job, &tls, sizeof(tls));
    if (status == ASYNC_PAUSE) {
      updateSignatureHandle(tls);
      return false;
    }
    if (status != ASYNC_FINISH && tls->signatureJob) {
      ZiLOG(Fatal, "Ztls", ([status, error = ERR_peek_last_error()](auto &s) {
	s << "async signature retirement failed: " << status << ": " << error;
      }));
      return false;
    }
  }
  releaseSignDigest(tls);
  return true;
}

static int resume_sign_digest(Ztls::Cxn *tls, Buffer *outbuf, uint16_t *selected)
{
  *selected = tls->signatureAlgorithm;
  int ret;
  switch (ASYNC_start_job(
    &tls->signatureJob, tls->signatureWait, &ret, sign_digest_job, &tls, sizeof(tls))) {
  case ASYNC_PAUSE:
    updateSignatureHandle(tls);
    return Ztls_ErrorAsyncOperation;
  case ASYNC_FINISH:
    tls->signatureJob = nullptr;
    if (ret != 1) {
      ret = Ztls_ErrorLibrary;
      break;
    }
    Ztls_BufferPushv(outbuf, tls->signature.data(), tls->signatureLength);
    ret = 0;
    break;
  default:
    ret = Ztls_ErrorLibrary;
    break;
  }

Exit:
  tls->retireSignature();
  return ret;
}

static int start_sign_digest(
  Ztls::Cxn *tls, EVP_MD_CTX *digest, uint16_t algorithm, size_t length, Buffer *outbuf)
{
  tls->signatureDigest = digest;
  tls->signatureWait = ASYNC_WAIT_CTX_new();
  if (!tls->signatureWait) {
    releaseSignDigest(tls);
    return Ztls_ErrorNoMemory;
  }
  tls->signatureAlgorithm = algorithm;
  tls->signatureLength = length;
  tls->signature.length(length);
  memset(tls->signature.data(), 0, length);
  tls->signatureRetire = [tls]() { return retireSignDigest(tls); };
  tls->signatureResume = [tls](Buffer *output, uint16_t *selected)
  { return resume_sign_digest(tls, output, selected); };
  return resume_sign_digest(tls, outbuf, &algorithm);
}

#endif

static int do_sign(
  EVP_PKEY *key, const OpenSSLSigScheme *scheme, Buffer *outbuf, Bytes input, Ztls::Cxn *async)
{
  EVP_MD_CTX   *ctx = NULL;
  const EVP_MD *md = scheme->md();
  EVP_PKEY_CTX *pkey_ctx;
  size_t	siglen;
  int		ret;

  if ((ctx = EVP_MD_CTX_create()) == NULL) {
    ret = Ztls_ErrorNoMemory;
    goto Exit;
  }

  if (EVP_DigestSignInit(ctx, &pkey_ctx, md, NULL, key) != 1) {
    ret = Ztls_ErrorLibrary;
    goto Exit;
  }

#if ZtlsOpenSSLHaveEd25519
  if (EVP_PKEY_id(key) == EVP_PKEY_ED25519) {
    // ED25519 requires the use of the all-at-once function that appeared in OpenSSL 1.1.1, hence
    // different path
    if (EVP_DigestSign(ctx, NULL, &siglen, input.data(), input.length()) != 1) {
      ret = Ztls_ErrorLibrary;
      goto Exit;
    }
    if ((ret = buffer_reserve(outbuf, siglen, outbuf->tx)) != 0) goto Exit;
    if (EVP_DigestSign(ctx, outbuf->base + outbuf->off, &siglen, input.data(), input.length()) !=
      1) {
      ret = Ztls_ErrorLibrary;
      goto Exit;
    }
  } else
#endif
  {
    if (EVP_PKEY_id(key) == EVP_PKEY_RSA) {
      if (EVP_PKEY_CTX_set_rsa_padding(pkey_ctx, RSA_PKCS1_PSS_PADDING) != 1) {
	ret = Ztls_ErrorLibrary;
	goto Exit;
      }
      if (EVP_PKEY_CTX_set_rsa_pss_saltlen(pkey_ctx, -1) != 1) {
	ret = Ztls_ErrorLibrary;
	goto Exit;
      }
      if (EVP_PKEY_CTX_set_rsa_mgf1_md(pkey_ctx, md) != 1) {
	ret = Ztls_ErrorLibrary;
	goto Exit;
      }
    }
    if (EVP_DigestSignUpdate(ctx, input.data(), input.length()) != 1) {
      ret = Ztls_ErrorLibrary;
      goto Exit;
    }
    if (EVP_DigestSignFinal(ctx, NULL, &siglen) != 1) {
      ret = Ztls_ErrorLibrary;
      goto Exit;
    }
    // If permitted by the caller (by providing a non-NULL `async` slot), use the asynchronous
    // signing method and return immediately.
#if ZtlsOpenSSLHaveAsync
    if (async) return start_sign_digest(async, ctx, scheme->schemeId, siglen, outbuf);
#endif
    // Otherwise, generate signature synchronously.
    if ((ret = buffer_reserve(outbuf, siglen, outbuf->tx)) != 0) goto Exit;
    if (EVP_DigestSignFinal(ctx, outbuf->base + outbuf->off, &siglen) != 1) {
      ret = Ztls_ErrorLibrary;
      goto Exit;
    }
  }

  outbuf->off += siglen;

  ret = 0;
Exit:
  if (ctx != NULL) EVP_MD_CTX_destroy(ctx);
  return ret;
}

struct OpenSSLCipherContext : ZmNHeap<OpenSSLCipherContext>, CipherContext {
friend inline ZuStringT<"Ztls.Engine.Cipher.OpenSSL"> ZmNHeapID(OpenSSLCipherContext *);
  EVP_CIPHER_CTX	*evp = EVP_CIPHER_CTX_new();
  bool			encrypt;

  OpenSSLCipherContext(const CipherAlgo *algo, bool encrypt_)
  :
    CipherContext{algo}, encrypt{encrypt_}
  { }
  ~OpenSSLCipherContext() override { EVP_CIPHER_CTX_free(evp); }
  int setup(const void *key, const EVP_CIPHER *cipher)
  {
    if (!evp) return Ztls_ErrorNoMemory;
    if (encrypt) {
      if (!EVP_EncryptInit_ex(
	    evp, cipher, nullptr, static_cast<const unsigned char *>(key), nullptr))
	return Ztls_ErrorLibrary;
    } else {
      if (!EVP_DecryptInit_ex(
	    evp, cipher, nullptr, static_cast<const unsigned char *>(key), nullptr))
	return Ztls_ErrorLibrary;
      // Disable ECB one-block buffering, as required by the cipher interface.
      EVP_CIPHER_CTX_set_padding(evp, 0);
    }
    return 0;
  }
  void init(const void *iv) override
  {
    [[maybe_unused]] int ret =
      EVP_EncryptInit_ex(evp, nullptr, nullptr, nullptr, static_cast<const unsigned char *>(iv));
    assert(ret);
  }
  void transform(void *output, const void *input, size_t size) override
  {
    int			 len = int(size);
    [[maybe_unused]] int ret = encrypt ? EVP_EncryptUpdate(
					   evp, static_cast<unsigned char *>(output), &len,
					   static_cast<const unsigned char *>(input), len) :
					 EVP_DecryptUpdate(
					   evp, static_cast<unsigned char *>(output), &len,
					   static_cast<const unsigned char *>(input), len);
    assert(ret && len == int(size));
  }
};

namespace OpenSSLCipherKind {
  enum { AES128ECB, AES256ECB, AES128CTR, AES256CTR, ChaCha20, BlowfishECB };
}

template <int Kind> struct OpenSSLCipherAlgo : CipherAlgo {
  using CipherAlgo::CipherAlgo;
  CipherContext *create(int isEnc, const void *key) const override
  {
    using namespace OpenSSLCipherKind;
    const EVP_CIPHER *cipher;
    if constexpr (Kind == AES128ECB)
      cipher = EVP_aes_128_ecb();
    else if constexpr (Kind == AES256ECB)
      cipher = EVP_aes_256_ecb();
    else if constexpr (Kind == AES128CTR)
      cipher = EVP_aes_128_ctr();
    else if constexpr (Kind == AES256CTR)
      cipher = EVP_aes_256_ctr();
#if ZtlsOpenSSLHaveChaCha
    else if constexpr (Kind == ChaCha20)
      cipher = EVP_chacha20();
#endif
#if ZtlsOpenSSLHaveBF
    else if constexpr (Kind == BlowfishECB)
      cipher = EVP_bf_ecb();
#endif
    bool encrypt = (Kind == AES128CTR || Kind == AES256CTR || Kind == ChaCha20) || isEnc;
    auto ctx = new OpenSSLCipherContext{this, encrypt};
    if (ctx->setup(key, cipher)) {
      delete ctx;
      return nullptr;
    }
    return ctx;
  }
};

struct OpenSSLAeadContext : ZmNHeap<OpenSSLAeadContext>, AeadContext {
friend inline ZuStringT<"Ztls.Engine.AEAD.OpenSSL"> ZmNHeapID(OpenSSLAeadContext *);
  EVP_CIPHER_CTX	*evpCtx = nullptr;
  uint8_t		staticIv[Ztls_MaxIVSize];
  int			decActive = 0;

  OpenSSLAeadContext(const AeadAlgo *algo, bool isEnc) : AeadContext{algo, isEnc} { }
  ~OpenSSLAeadContext() override;
  bool serialEncrypt() const override { return isEnc; }
  bool serialDecrypt() const override { return !isEnc; }
  void getIV(void *iv) override;
  void setIV(const void *iv) override;
  int encBegin(uint64_t seq, const void *aad, size_t aadlen) override;
  int encChunk(void *output, const void *input, size_t length, size_t *written) override;
  int encEnd(void *output, size_t capacity, size_t *written) override;
  int decBegin(uint64_t seq, const void *aad, size_t aadlen) override;
  int decChunk(
    void *output, size_t capacity, const void *input, size_t length, size_t *written) override;
  int decEnd(const void *tag, size_t taglen) override;
  void decAbort() override;
  void encryptV(
    void *output, const Bytes *input, size_t incnt, uint64_t seq, const void *aad,
    size_t aadlen) override;
  size_t decrypt(
    void *output, const void *input, size_t inlen, uint64_t seq, const void *aad,
    size_t aadlen) override;
};

static void openssl_aead_get_iv(AeadContext *_ctx, void *iv)
{
  struct OpenSSLAeadContext *ctx = static_cast<OpenSSLAeadContext *>(_ctx);

  memcpy(iv, ctx->staticIv, ctx->algo->ivSize);
}

static void openssl_aead_set_iv(AeadContext *_ctx, const void *iv)
{
  struct OpenSSLAeadContext *ctx = static_cast<OpenSSLAeadContext *>(_ctx);

  memcpy(ctx->staticIv, iv, ctx->algo->ivSize);
}

static int aead_enc_begin(AeadContext *_ctx, uint64_t seq, const void *aad, size_t aadlen)
{
  struct OpenSSLAeadContext *ctx = static_cast<OpenSSLAeadContext *>(_ctx);
  uint8_t		     iv[Ztls_MaxIVSize];
  int			     len;
  if (aadlen > INT_MAX) return Ztls_ErrorLibrary;
  aead__build_iv(_ctx->algo, iv, ctx->staticIv, seq);
  if (!EVP_EncryptInit_ex(ctx->evpCtx, NULL, NULL, NULL, iv) ||
    (aadlen != 0 &&
      !EVP_EncryptUpdate(
	ctx->evpCtx, NULL, &len, static_cast<const unsigned char *>(aad), (int)aadlen)))
    return Ztls_ErrorLibrary;
  return 0;
}

static int
aead_enc_chunk(AeadContext *_ctx, void *output, const void *input, size_t length, size_t *written)
{
  struct OpenSSLAeadContext *ctx = static_cast<OpenSSLAeadContext *>(_ctx);
  int			     len;
  *written = 0;
  if (length > INT_MAX ||
    !EVP_EncryptUpdate(
      ctx->evpCtx, static_cast<unsigned char *>(output), &len,
      static_cast<const unsigned char *>(input), (int)length) ||
    len != length)
    return Ztls_ErrorLibrary;
  *written = len;
  return 0;
}

static int aead_enc_end(AeadContext *_ctx, void *output, size_t capacity, size_t *written)
{
  struct OpenSSLAeadContext *ctx = static_cast<OpenSSLAeadContext *>(_ctx);
  uint8_t		     final[EVP_MAX_BLOCK_LENGTH];
  int			     len;
  *written = 0;
  if (capacity < _ctx->algo->tagSize || !EVP_EncryptFinal_ex(ctx->evpCtx, final, &len) ||
    len != 0 ||
    !EVP_CIPHER_CTX_ctrl(ctx->evpCtx, EVP_CTRL_GCM_GET_TAG, (int)_ctx->algo->tagSize, output))
    return Ztls_ErrorLibrary;
  *written = _ctx->algo->tagSize;
  return 0;
}

// Only GCM and ChaCha20-Poly1305 install these callbacks. Their update calls
// emit exactly the input size; final emits no additional plaintext.
static int aead_dec_error(struct OpenSSLAeadContext *ctx, int error)
{
  ctx->decActive = 0;
  EVP_CIPHER_CTX_reset(ctx->evpCtx);
  return error;
}

static void aead_dec_abort(AeadContext *_ctx)
{ aead_dec_error(static_cast<OpenSSLAeadContext *>(_ctx), Ztls_ErrorLibrary); }

static int aead_dec_begin(AeadContext *_ctx, uint64_t seq, const void *aad, size_t aadlen)
{
  struct OpenSSLAeadContext *ctx = static_cast<OpenSSLAeadContext *>(_ctx);
  uint8_t		     iv[Ztls_MaxIVSize];
  int			     len;
  if (ctx->decActive || aadlen > INT_MAX || (aadlen != 0 && aad == NULL))
    return aead_dec_error(ctx, Ztls_ErrorLibrary);
  aead__build_iv(_ctx->algo, iv, ctx->staticIv, seq);
  int ret = EVP_DecryptInit_ex(ctx->evpCtx, NULL, NULL, NULL, iv);
  ZuClear(iv, sizeof(iv));
  if (!ret ||
    (aadlen != 0 &&
      !EVP_DecryptUpdate(
	ctx->evpCtx, NULL, &len, static_cast<const unsigned char *>(aad), (int)aadlen)))
    return aead_dec_error(ctx, Ztls_ErrorLibrary);
  ctx->decActive = 1;
  return 0;
}

static int aead_dec_chunk(
  AeadContext *_ctx, void *output, size_t capacity, const void *input, size_t length,
  size_t *written)
{
  struct OpenSSLAeadContext *ctx = static_cast<OpenSSLAeadContext *>(_ctx);
  int			     len;
  *written = 0;
  if (!ctx->decActive || length > capacity || length > INT_MAX ||
    (length != 0 && (output == NULL || input == NULL)))
    return aead_dec_error(ctx, Ztls_ErrorLibrary);
  if (length != 0 &&
    (!EVP_DecryptUpdate(
       ctx->evpCtx, static_cast<unsigned char *>(output), &len,
       static_cast<const unsigned char *>(input), (int)length) ||
      len != length))
    return aead_dec_error(ctx, Ztls_ErrorLibrary);
  *written = length;
  return 0;
}

static int aead_dec_end(AeadContext *_ctx, const void *tag, size_t taglen)
{
  struct OpenSSLAeadContext *ctx = static_cast<OpenSSLAeadContext *>(_ctx);
  uint8_t		     final[EVP_MAX_BLOCK_LENGTH];
  int			     len;
  if (!ctx->decActive || tag == NULL || taglen != _ctx->algo->tagSize ||
    !EVP_CIPHER_CTX_ctrl(ctx->evpCtx, EVP_CTRL_GCM_SET_TAG, (int)taglen, (void *)tag))
    return aead_dec_error(ctx, Ztls_ErrorLibrary);
  int ret = EVP_DecryptFinal_ex(ctx->evpCtx, final, &len);
  ZuClear(final, sizeof(final));
  if (!ret) {
    // Rejected early data may be skipped before the next protected record.
    // Reset the per-record provider state while retaining the installed key.
    // A fresh nonce on begin replaces this unused clearing nonce.
    uint8_t cleared_iv[Ztls_MaxIVSize] = {0};
    ctx->decActive = 0;
    if (!EVP_DecryptInit_ex(ctx->evpCtx, NULL, NULL, NULL, cleared_iv))
      return aead_dec_error(ctx, Ztls_ErrorLibrary);
    return Ztls_AlertBadRecordMac;
  }
  if (len != 0) return aead_dec_error(ctx, Ztls_ErrorLibrary);
  ctx->decActive = 0;
  return 0;
}

static void aead_do_encrypt_init(AeadContext *_ctx, uint64_t seq, const void *aad, size_t aadlen)
{
  struct OpenSSLAeadContext *ctx = static_cast<OpenSSLAeadContext *>(_ctx);
  uint8_t		     iv[Ztls_MaxIVSize];
  [[maybe_unused]] int	     ret;

  aead__build_iv(ctx->algo, iv, ctx->staticIv, seq);
  ret = EVP_EncryptInit_ex(ctx->evpCtx, NULL, NULL, NULL, iv);
  assert(ret);

  if (aadlen != 0) {
    int blocklen;
    ret = EVP_EncryptUpdate(
      ctx->evpCtx, NULL, &blocklen, static_cast<const unsigned char *>(aad), (int)aadlen);
    assert(ret);
  }
}

static size_t
aead_do_encrypt_update(AeadContext *_ctx, void *output, const void *input, size_t inlen)
{
  struct OpenSSLAeadContext *ctx = static_cast<OpenSSLAeadContext *>(_ctx);
  int			     blocklen;
  [[maybe_unused]] int	     ret;

  ret = EVP_EncryptUpdate(
    ctx->evpCtx, static_cast<unsigned char *>(output), &blocklen,
    static_cast<const unsigned char *>(input), (int)inlen);
  assert(ret);

  return blocklen;
}

static size_t aead_do_encrypt_final(AeadContext *_ctx, void *_output)
{
  struct OpenSSLAeadContext *ctx = static_cast<OpenSSLAeadContext *>(_ctx);
  uint8_t		    *output = static_cast<uint8_t *>(_output);
  size_t		     off = 0, tagSize = ctx->algo->tagSize;
  int			     blocklen;
  [[maybe_unused]] int	     ret;

  ret = EVP_EncryptFinal_ex(ctx->evpCtx, output + off, &blocklen);
  assert(ret);
  off += blocklen;
  ret = EVP_CIPHER_CTX_ctrl(ctx->evpCtx, EVP_CTRL_GCM_GET_TAG, (int)tagSize, output + off);
  assert(ret);
  off += tagSize;

  return off;
}

static size_t aead_do_decrypt(
  AeadContext *_ctx, void *_output, const void *input, size_t inlen, uint64_t seq,
  const void *aad, size_t aadlen)
{
  struct OpenSSLAeadContext *ctx = static_cast<OpenSSLAeadContext *>(_ctx);
  uint8_t		    *output = static_cast<uint8_t *>(_output), iv[Ztls_MaxIVSize];
  size_t		     off = 0, tagSize = ctx->algo->tagSize;
  int			     blocklen;
  [[maybe_unused]] int	     ret;

  if (inlen < tagSize) return SIZE_MAX;

  aead__build_iv(ctx->algo, iv, ctx->staticIv, seq);
  ret = EVP_DecryptInit_ex(ctx->evpCtx, NULL, NULL, NULL, iv);
  assert(ret);
  if (aadlen != 0) {
    ret = EVP_DecryptUpdate(
      ctx->evpCtx, NULL, &blocklen, static_cast<const unsigned char *>(aad), (int)aadlen);
    assert(ret);
  }
  ret = EVP_DecryptUpdate(
    ctx->evpCtx, output + off, &blocklen, static_cast<const unsigned char *>(input),
    (int)(inlen - tagSize));
  assert(ret);
  off += blocklen;
  if (!EVP_CIPHER_CTX_ctrl(
	ctx->evpCtx, EVP_CTRL_GCM_SET_TAG, (int)tagSize,
	(void *)((uint8_t *)input + inlen - tagSize)))
    return SIZE_MAX;
  if (!EVP_DecryptFinal_ex(ctx->evpCtx, output + off, &blocklen)) return SIZE_MAX;
  off += blocklen;

  return off;
}

static int aead_setup_crypto(
  AeadContext *_ctx, int is_enc, const void *key, const void *iv, const EVP_CIPHER *cipher)
{
  struct OpenSSLAeadContext *ctx = static_cast<OpenSSLAeadContext *>(_ctx);
  int			     ret;

  ctx->evpCtx = NULL;

  if ((ctx->evpCtx = EVP_CIPHER_CTX_new()) == NULL) {
    ret = Ztls_ErrorNoMemory;
    goto Error;
  }
  if (is_enc) {
    if (!EVP_EncryptInit_ex(
	  ctx->evpCtx, cipher, NULL, static_cast<const unsigned char *>(key), NULL)) {
      ret = Ztls_ErrorLibrary;
      goto Error;
    }
  } else {
    if (!EVP_DecryptInit_ex(
	  ctx->evpCtx, cipher, NULL, static_cast<const unsigned char *>(key), NULL)) {
      ret = Ztls_ErrorLibrary;
      goto Error;
    }
  }
  if (!EVP_CIPHER_CTX_ctrl(ctx->evpCtx, EVP_CTRL_GCM_SET_IVLEN, (int)ctx->algo->ivSize, NULL)) {
    ret = Ztls_ErrorLibrary;
    goto Error;
  }

  memcpy(ctx->staticIv, iv, ctx->algo->ivSize);

  return 0;

Error:
  return ret;
}

OpenSSLAeadContext::~OpenSSLAeadContext()
{
  EVP_CIPHER_CTX_free(evpCtx);
  ZuClear(staticIv, sizeof(staticIv));
  decActive = 0;
}

void OpenSSLAeadContext::getIV(void *iv) { openssl_aead_get_iv(this, iv); }

void OpenSSLAeadContext::setIV(const void *iv) { openssl_aead_set_iv(this, iv); }

int OpenSSLAeadContext::encBegin(uint64_t seq, const void *aad, size_t aadlen)
{
  if (!isEnc) return Ztls_ErrorNotAvailable;
  return aead_enc_begin(this, seq, aad, aadlen);
}

int OpenSSLAeadContext::encChunk(void *output, const void *input, size_t length, size_t *written)
{
  if (!isEnc) {
    *written = 0;
    return Ztls_ErrorNotAvailable;
  }
  return aead_enc_chunk(this, output, input, length, written);
}

int OpenSSLAeadContext::encEnd(void *output, size_t capacity, size_t *written)
{
  if (!isEnc) {
    *written = 0;
    return Ztls_ErrorNotAvailable;
  }
  return aead_enc_end(this, output, capacity, written);
}

int OpenSSLAeadContext::decBegin(uint64_t seq, const void *aad, size_t aadlen)
{
  if (isEnc) return Ztls_ErrorNotAvailable;
  return aead_dec_begin(this, seq, aad, aadlen);
}

int OpenSSLAeadContext::decChunk(
  void *output, size_t capacity, const void *input, size_t length, size_t *written)
{
  if (isEnc) {
    *written = 0;
    return Ztls_ErrorNotAvailable;
  }
  return aead_dec_chunk(this, output, capacity, input, length, written);
}

int OpenSSLAeadContext::decEnd(const void *tag, size_t taglen)
{
  if (isEnc) return Ztls_ErrorNotAvailable;
  return aead_dec_end(this, tag, taglen);
}

void OpenSSLAeadContext::decAbort() { aead_dec_abort(this); }

void OpenSSLAeadContext::encryptV(
  void *output, const Bytes *input, size_t incnt, uint64_t seq, const void *aad, size_t aadlen)
{
  assert(isEnc);
  aead_do_encrypt_init(this, seq, aad, aadlen);
  auto dst = static_cast<uint8_t *>(output);
  for (size_t i = 0; i != incnt; ++i)
    dst += aead_do_encrypt_update(this, dst, input[i].data(), input[i].length());
  aead_do_encrypt_final(this, dst);
}

size_t OpenSSLAeadContext::decrypt(
  void *output, const void *input, size_t inlen, uint64_t seq, const void *aad, size_t aadlen)
{
  assert(!isEnc);
  return aead_do_decrypt(this, output, input, inlen, seq, aad, aadlen);
}

template <int Kind> struct OpenSSLAeadAlgo : AeadAlgo {
  using AeadAlgo::AeadAlgo;
  AeadContext *create(bool isEnc, const void *key, const void *iv) const override
  {
    const EVP_CIPHER *cipher;
    if constexpr (Kind == 128)
      cipher = EVP_aes_128_gcm();
    else if constexpr (Kind == 256)
      cipher = EVP_aes_256_gcm();
#if ZtlsOpenSSLHaveChaCha
    else
      cipher = EVP_chacha20_poly1305();
#endif
    auto ctx = new OpenSSLAeadContext{this, isEnc};
    if (aead_setup_crypto(ctx, isEnc, key, iv, cipher)) {
      delete ctx;
      return nullptr;
    }
    return ctx;
  }
};

template <int Bits> struct OpenSSLHashContext : ZmNHeap<OpenSSLHashContext<Bits>>, HashContext {
friend inline ZuStringT<"Ztls.Engine.Hash"> ZmNHeapID(OpenSSLHashContext *);
  using Native = ZuIf<Bits == 256, SHA256_CTX, SHA512_CTX>;
  Native	state;

  OpenSSLHashContext() { reset(); }
  explicit OpenSSLHashContext(const Native &native) : state{native} { }
  ~OpenSSLHashContext() override { ZuClear(&state, sizeof(state)); }

  void reset()
  {
    if constexpr (Bits == 256)
      SHA256_Init(&state);
    else if constexpr (Bits == 384)
      SHA384_Init(&state);
    else
      SHA512_Init(&state);
  }
  void update(const void *src, size_t len) override
  {
    if constexpr (Bits == 256)
      SHA256_Update(&state, src, len);
    else
      SHA512_Update(&state, src, len);
  }
  static void digest(Native &native, void *md)
  {
    if constexpr (Bits == 256)
      SHA256_Final(static_cast<uint8_t *>(md), &native);
    else if constexpr (Bits == 384)
      SHA384_Final(static_cast<uint8_t *>(md), &native);
    else
      SHA512_Final(static_cast<uint8_t *>(md), &native);
  }
  int final(void *md, HashFinalMode::T mode) override
  {
    if (mode == HashFinalMode::Snapshot) {
      Native copy = state;
      digest(copy, md);
      ZuClear(&copy, sizeof(copy));
      return 0;
    }
    if (md) digest(state, md);
    if (mode == HashFinalMode::Reset) reset();
    return 0;
  }
  HashContext *clone() const override { return new OpenSSLHashContext{state}; }
};

template <int Bits> struct OpenSSLHashAlgo : HashAlgo {
  using HashAlgo::HashAlgo;
  HashContext *create() const override { return new OpenSSLHashContext<Bits>; }
};

int OpenSSLSignCert::call(
  Ztls::Cxn *tls, bool allow_async, uint16_t *selected_algorithm, Buffer *outbuf, Bytes input,
  ZuSpan<const uint16_t> algorithms)
{
  auto self = this;
  auto scheme = openssl_select_sig_scheme(self->schemes, algorithms);
  if (!scheme) return Ztls_AlertHSFailure;
  *selected_algorithm = scheme->schemeId;

#if ZtlsOpenSSLHaveAsync
  return do_sign(self->key, scheme, outbuf, input, allow_async && self->async ? tls : nullptr);
#else
  return do_sign(self->key, scheme, outbuf, input, nullptr);
#endif
}

static X509 *to_x509(Bytes vec)
{
  const uint8_t *p = vec.data();
  return d2i_X509(NULL, &p, (long)vec.length());
}

struct OpenSSLVerifySignature : ZmNHeap<OpenSSLVerifySignature>, VerifySignature {
friend inline ZuStringT<"Ztls.Engine.VerifySignature"> ZmNHeapID(OpenSSLVerifySignature *);
  EVP_PKEY	*key;

  explicit OpenSSLVerifySignature(EVP_PKEY *key_) : key{key_} { }
  ~OpenSSLVerifySignature() override { EVP_PKEY_free(key); }
  int verify(uint16_t algo, Bytes data, Bytes signature) override;
};

int OpenSSLVerifySignature::verify(uint16_t algo, Bytes data, Bytes signature)
{
  ZuSpan<const OpenSSLSigScheme *const> available;
  const OpenSSLSigScheme	       *scheme;
  EVP_MD_CTX			       *ctx = NULL;
  EVP_PKEY_CTX			       *pkey_ctx = NULL;
  int					ret = 0;

  if (!(available = openssl_lookup_sig_schemes(key)).length()) {
    ret = Ztls_ErrorLibrary;
    goto Exit;
  }
  scheme = openssl_select_sig_scheme(available, {&algo, 1});
  if (scheme) goto SchemeFound;
  ret = Ztls_AlertIllegalParameter;
  goto Exit;

SchemeFound:
  if ((ctx = EVP_MD_CTX_create()) == NULL) {
    ret = Ztls_ErrorNoMemory;
    goto Exit;
  }

#if ZtlsOpenSSLHaveEd25519
  if (EVP_PKEY_id(key) == EVP_PKEY_ED25519) {
    // ED25519 requires the use of the all-at-once function that appeared in OpenSSL 1.1.1, hence
    // different path
    if (EVP_DigestVerifyInit(ctx, &pkey_ctx, NULL, NULL, key) != 1) {
      ret = Ztls_ErrorLibrary;
      goto Exit;
    }
    if (EVP_DigestVerify(ctx, signature.data(), signature.length(), data.data(), data.length()) !=
      1) {
      ret = Ztls_ErrorLibrary;
      goto Exit;
    }
  } else
#endif
  {
    if (EVP_DigestVerifyInit(ctx, &pkey_ctx, scheme->md(), NULL, key) != 1) {
      ret = Ztls_ErrorLibrary;
      goto Exit;
    }

    if (EVP_PKEY_id(key) == EVP_PKEY_RSA) {
      if (EVP_PKEY_CTX_set_rsa_padding(pkey_ctx, RSA_PKCS1_PSS_PADDING) != 1) {
	ret = Ztls_ErrorLibrary;
	goto Exit;
      }
      if (EVP_PKEY_CTX_set_rsa_pss_saltlen(pkey_ctx, -1) != 1) {
	ret = Ztls_ErrorLibrary;
	goto Exit;
      }
      if (EVP_PKEY_CTX_set_rsa_mgf1_md(pkey_ctx, scheme->md()) != 1) {
	ret = Ztls_ErrorLibrary;
	goto Exit;
      }
    }
    if (EVP_DigestVerifyUpdate(ctx, data.data(), data.length()) != 1) {
      ret = Ztls_ErrorLibrary;
      goto Exit;
    }
    if (EVP_DigestVerifyFinal(ctx, signature.data(), signature.length()) != 1) {
      ret = Ztls_AlertDecryptError;
      goto Exit;
    }
  }

  ret = 0;

Exit:
  if (ctx != NULL) EVP_MD_CTX_destroy(ctx);
  return ret;
}

int openssl_init_sign_cert(OpenSSLSignCert *self, EVP_PKEY *key)
{
  self->async = 0;

  if (!(self->schemes = openssl_lookup_sig_schemes(key)).length())
    return Ztls_ErrorIncompatibleKey;
  EVP_PKEY_up_ref(key);
  self->key = key;

  return 0;
}

OpenSSLSignCert::~OpenSSLSignCert() { EVP_PKEY_free(key); }

void openssl_dispose_sign_cert(OpenSSLSignCert *self)
{
  EVP_PKEY_free(self->key);
  self->key = nullptr;
}

static int verify_cert_chain(
  X509_STORE *store, X509 *cert, STACK_OF(X509) * chain, int isServer, const char *serverName,
  int *ossl_x509_err)
{
  X509_STORE_CTX *verify_ctx;
  int		  ret;
  *ossl_x509_err = 0;

  // verify certificate chain
  if ((verify_ctx = X509_STORE_CTX_new()) == NULL) {
    ret = Ztls_ErrorNoMemory;
    goto Exit;
  }
  if (X509_STORE_CTX_init(verify_ctx, store, cert, chain) != 1) {
    ret = Ztls_ErrorLibrary;
    goto Exit;
  }

  {
    // setup verify params
    X509_VERIFY_PARAM *params = X509_STORE_CTX_get0_param(verify_ctx);
    X509_VERIFY_PARAM_set_purpose(
      params, isServer ? X509_PURPOSE_SSL_CLIENT : X509_PURPOSE_SSL_SERVER);
    X509_VERIFY_PARAM_set_depth(params, 98);
    // use the default of OpenSSL 1.0.2 and above; see `man SSL_CTX_set_verify`
    // when _acting_ as client, set the server name if provided
    if (!isServer && serverName != NULL) {
      if (server_name_is_ipaddr(serverName)) {
	X509_VERIFY_PARAM_set1_ip_asc(params, serverName);
      } else {
	X509_VERIFY_PARAM_set1_host(params, serverName, strlen(serverName));
	X509_VERIFY_PARAM_set_hostflags(params, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
      }
    }
  }

  if (X509_verify_cert(verify_ctx) != 1) {
    *ossl_x509_err = X509_STORE_CTX_get_error(verify_ctx);
    switch (*ossl_x509_err) {
    case X509_V_ERR_OUT_OF_MEM:
      ret = Ztls_ErrorNoMemory;
      break;
    case X509_V_ERR_CERT_REVOKED:
      ret = Ztls_AlertCertRevoked;
      break;
    case X509_V_ERR_CERT_NOT_YET_VALID:
    case X509_V_ERR_CERT_HAS_EXPIRED:
      ret = Ztls_AlertCertExpired;
      break;
    case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT:
    case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY:
    case X509_V_ERR_CERT_UNTRUSTED:
    case X509_V_ERR_CERT_REJECTED:
      ret = Ztls_AlertUnknownCa;
      break;
    case X509_V_ERR_HOSTNAME_MISMATCH:
    case X509_V_ERR_INVALID_CA:
#ifdef X509_V_ERR_CA_MD_TOO_WEAK
    case X509_V_ERR_CA_MD_TOO_WEAK:
#endif
#ifdef X509_V_ERR_UNSUPPORTED_SIGNATURE_ALGORITHM
    case X509_V_ERR_UNSUPPORTED_SIGNATURE_ALGORITHM:
#endif
      ret = Ztls_AlertBadCert;
      break;
    default:
      ret = Ztls_AlertCertUnknown;
      break;
    }
    goto Exit;
  }

  ret = 0;

Exit:
  if (verify_ctx != NULL) X509_STORE_CTX_free(verify_ctx);
  return ret;
}

int OpenSSLVerifyCert::call(
  Ztls::Cxn *tls, const char *serverName, VerifySignature **verifier, ZuSpan<const Bytes> certs)
{
  auto	self = this;
  X509 *cert = NULL;
  STACK_OF(X509) *chain = sk_X509_new_null();
  size_t i;
  int	 ossl_x509_err, ret;

  // If any certs are given, convert them to OpenSSL representation, then verify the cert chain.
  // If no certs are given, just give the overrideCallback to see if we want to stay fail open.
  if (certs.length() != 0) {
    if ((cert = to_x509(certs[0])) == NULL) {
      ret = Ztls_AlertBadCert;
      goto Exit;
    }
    for (i = 1; i != certs.length(); ++i) {
      X509 *interm = to_x509(certs[i]);
      if (interm == NULL) {
	ret = Ztls_AlertBadCert;
	goto Exit;
      }
      sk_X509_push(chain, interm);
    }
    ret =
      verify_cert_chain(self->certStore, cert, chain, isServer(tls), serverName, &ossl_x509_err);
  } else {
    ret = Ztls_AlertCertRequired;
    ossl_x509_err = 0;
  }

  // When override callback is available, let it override the error.
  if (self->overrideCallback != NULL)
    ret = self->overrideCallback->call(tls, ret, ossl_x509_err, cert, chain);

  if (ret != 0 || certs.length() == 0) goto Exit;

  // extract public key for verifying the TLS handshake signature
  if (auto key = X509_get_pubkey(cert)) {
    *verifier = new OpenSSLVerifySignature{key};
  } else {
    ret = Ztls_AlertBadCert;
    goto Exit;
  }

Exit:
  if (chain != NULL) sk_X509_pop_free(chain, X509_free);
  if (cert != NULL) X509_free(cert);
  return ret;
}

int openssl_init_verify_cert(OpenSSLVerifyCert *self, X509_STORE *store)
{
  self->algos = default_signature_schemes;

  if (store != NULL) {
    X509_STORE_up_ref(store);
    self->certStore = store;
  } else {
    // use default store
    if ((self->certStore = openssl_default_cert_store()) == NULL) return -1;
  }

  return 0;
}

OpenSSLVerifyCert::~OpenSSLVerifyCert() { X509_STORE_free(certStore); }

void openssl_dispose_verify_cert(OpenSSLVerifyCert *self)
{
  X509_STORE_free(self->certStore);
  self->certStore = nullptr;
}

X509_STORE *openssl_default_cert_store(void)
{
  X509_STORE  *store;
  X509_LOOKUP *lookup;

  if ((store = X509_STORE_new()) == NULL) goto Error;
  if ((lookup = X509_STORE_add_lookup(store, X509_LOOKUP_file())) == NULL) goto Error;
  X509_LOOKUP_load_file(lookup, NULL, X509_FILETYPE_DEFAULT);
  if ((lookup = X509_STORE_add_lookup(store, X509_LOOKUP_hash_dir())) == NULL) goto Error;
  X509_LOOKUP_add_dir(lookup, NULL, X509_FILETYPE_DEFAULT);

  return store;
Error:
  if (store != NULL) X509_STORE_free(store);
  return NULL;
}

// OpenSSL ticket callback uses a 16-byte key name and its maximum IV size.
enum { TICKET_LABEL_SIZE = 16, TICKET_IV_SIZE = EVP_MAX_IV_LENGTH };

int openssl_encrypt_ticket_evp(
  Buffer *buf, Bytes src,
  int (*cb)(
    unsigned char *key_name, unsigned char *iv, EVP_CIPHER_CTX *ctx, EVP_MAC_CTX *hctx, int enc))
{
  EVP_CIPHER_CTX *cctx = NULL;
  EVP_MAC	 *mac = NULL;
  EVP_MAC_CTX	 *hctx = NULL;
  size_t	  hlen;
  uint8_t	 *dst;
  int		  clen, ret;

  if ((cctx = EVP_CIPHER_CTX_new()) == NULL) {
    ret = Ztls_ErrorNoMemory;
    goto Exit;
  }
  if ((mac = EVP_MAC_fetch(NULL, "HMAC", NULL)) == NULL) {
    ret = Ztls_ErrorNoMemory;
    goto Exit;
  }
  if ((hctx = EVP_MAC_CTX_new(mac)) == NULL) {
    ret = Ztls_ErrorNoMemory;
    goto Exit;
  }

  if (
    (ret = buffer_reserve(
       buf,
       TICKET_LABEL_SIZE + TICKET_IV_SIZE + src.length() + EVP_MAX_BLOCK_LENGTH + EVP_MAX_MD_SIZE,
       buf->tx)) != 0)
    goto Exit;
  dst = buf->base + buf->off;

  // fill label and iv, as well as obtaining the keys
  if (!(*cb)(dst, dst + TICKET_LABEL_SIZE, cctx, hctx, 1)) {
    ret = Ztls_ErrorLibrary;
    goto Exit;
  }
  dst += TICKET_LABEL_SIZE + TICKET_IV_SIZE;

  // encrypt
  if (!EVP_EncryptUpdate(cctx, dst, &clen, src.data(), (int)src.length())) {
    ret = Ztls_ErrorLibrary;
    goto Exit;
  }
  dst += clen;
  if (!EVP_EncryptFinal_ex(cctx, dst, &clen)) {
    ret = Ztls_ErrorLibrary;
    goto Exit;
  }
  dst += clen;

  // append hmac
  if (!EVP_MAC_update(hctx, buf->base + buf->off, dst - (buf->base + buf->off)) ||
    !EVP_MAC_final(hctx, dst, &hlen, EVP_MAC_CTX_get_mac_size(hctx))) {
    ret = Ztls_ErrorLibrary;
    goto Exit;
  }
  dst += hlen;

  assert(dst <= buf->base + buf->capacity);
  buf->off += dst - (buf->base + buf->off);
  ret = 0;

Exit:
  if (cctx != NULL) EVP_CIPHER_CTX_free(cctx);
  if (hctx != NULL) EVP_MAC_CTX_free(hctx);
  if (mac != NULL) EVP_MAC_free(mac);
  return ret;
}

int openssl_decrypt_ticket_evp(
  Buffer *buf, Bytes src,
  int (*cb)(
    unsigned char *key_name, unsigned char *iv, EVP_CIPHER_CTX *ctx, EVP_MAC_CTX *hctx, int enc))
{
  size_t	  hmac_size;
  EVP_CIPHER_CTX *cctx = NULL;
  EVP_MAC	 *mac = NULL;
  EVP_MAC_CTX	 *hctx = NULL;
  size_t	  hlen;
  int		  clen, ret;

  if ((cctx = EVP_CIPHER_CTX_new()) == NULL) {
    ret = Ztls_ErrorNoMemory;
    goto Exit;
  }
  if ((mac = EVP_MAC_fetch(NULL, "HMAC", NULL)) == NULL) {
    ret = Ztls_ErrorNoMemory;
    goto Exit;
  }
  if ((hctx = EVP_MAC_CTX_new(mac)) == NULL) {
    ret = Ztls_ErrorNoMemory;
    goto Exit;
  }

  // obtain cipher and hash context. Note: no need to handle renew, since the server always sends
  // a new ticket to minimize the chance of ticket reuse
  if (src.length() < TICKET_LABEL_SIZE + TICKET_IV_SIZE) {
    ret = Ztls_AlertDecodeError;
    goto Exit;
  }
  if (!(*cb)(src.data(), src.data() + TICKET_LABEL_SIZE, cctx, hctx, 0)) {
    ret = Ztls_ErrorLibrary;
    goto Exit;
  }

  // check hmac, and exclude label, iv, hmac
  hmac_size = EVP_MAC_CTX_get_mac_size(hctx);
  if (src.length() < TICKET_LABEL_SIZE + TICKET_IV_SIZE + hmac_size) {
    ret = Ztls_AlertDecodeError;
    goto Exit;
  }
  src = {src.data(), src.length() - hmac_size};
  uint8_t hmac[EVP_MAX_MD_SIZE];
  if (!EVP_MAC_update(hctx, src.data(), src.length()) ||
    !EVP_MAC_final(hctx, hmac, &hlen, sizeof(hmac))) {
    ret = Ztls_ErrorLibrary;
    goto Exit;
  }
  if (!Ztls::ctEqual({src.data() + src.length(), hmac_size}, {hmac, hmac_size})) {
    ret = Ztls_AlertHSFailure;
    goto Exit;
  }
  src = {src.data() + TICKET_LABEL_SIZE + TICKET_IV_SIZE,
    src.length() - TICKET_LABEL_SIZE - TICKET_IV_SIZE};

  // decrypt
  if ((ret = buffer_reserve(buf, src.length(), buf->tx)) != 0) goto Exit;
  if (!EVP_DecryptUpdate(cctx, buf->base + buf->off, &clen, src.data(), (int)src.length())) {
    ret = Ztls_ErrorLibrary;
    goto Exit;
  }
  buf->off += clen;
  if (!EVP_DecryptFinal_ex(cctx, buf->base + buf->off, &clen)) {
    ret = Ztls_ErrorLibrary;
    goto Exit;
  }
  buf->off += clen;

  ret = 0;

Exit:
  if (cctx != NULL) EVP_CIPHER_CTX_free(cctx);
  if (hctx != NULL) EVP_MAC_CTX_free(hctx);
  if (mac != NULL) EVP_MAC_free(mac);
  return ret;
}

template <int Kind> struct OpenSSLKeyExchangeAlgo : KeyExchangeAlgo {
  using KeyExchangeAlgo::KeyExchangeAlgo;
  int create(KeyExchangeContext **ctx) const override
  {
    if constexpr (Kind == 0) return x9_62_create_key_exchange(this, ctx);
#if ZtlsOpenSSLHaveX25519
    else if constexpr (Kind == 1)
      return evp_keyex_create(this, ctx);
#endif
    else
      return evp_kem_create(this, ctx);
  }
  int exchange(KeyExchangeBytes *pubkey, KeyExchangeBytes *secret, Bytes peerkey) const override
  {
    if constexpr (Kind == 0) return secp_key_exchange(this, pubkey, secret, peerkey);
#if ZtlsOpenSSLHaveX25519
    else if constexpr (Kind == 1)
      return evp_keyex_exchange(this, pubkey, secret, peerkey);
#endif
    else
      return evp_kem_exchange(this, pubkey, secret, peerkey);
  }
};

static const OpenSSLKeyExchangeAlgo<0> secp256r1Algo{
  Ztls_GroupSecp256r1, NID_X9_62_prime256v1, Ztls_GroupNameSecp256r1};
const KeyExchangeAlgo &openssl_secp256r1 = secp256r1Algo;
#if ZtlsOpenSSLHaveSecp384r1
static const OpenSSLKeyExchangeAlgo<0> secp384r1Algo{
  Ztls_GroupSecp384r1, NID_secp384r1, Ztls_GroupNameSecp384r1};
const KeyExchangeAlgo &openssl_secp384r1 = secp384r1Algo;
#endif
#if ZtlsOpenSSLHaveSecp521r1
static const OpenSSLKeyExchangeAlgo<0> secp521r1Algo{
  Ztls_GroupSecp521r1, NID_secp521r1, Ztls_GroupNameSecp521r1};
const KeyExchangeAlgo &openssl_secp521r1 = secp521r1Algo;
#endif
#if ZtlsOpenSSLHaveX25519
static const OpenSSLKeyExchangeAlgo<1> x25519Algo{
  Ztls_GroupX25519, NID_X25519, Ztls_GroupNameX25519};
const KeyExchangeAlgo &openssl_x25519 = x25519Algo;
#endif
#define Ztls_DefineMlkemKeyex(lowcase, id_, name_) \
  static const OpenSSLKeyExchangeAlgo<2> lowcase##Algo{id_, intptr_t(name_), name_}; \
  const KeyExchangeAlgo			&openssl_##lowcase = lowcase##Algo
Ztls_DefineMlkemKeyex(x25519mlkem768, Ztls_GroupX25519mlkem768, Ztls_GrpStrX25519mlkem768);
Ztls_DefineMlkemKeyex(secp256r1mlkem768, Ztls_GroupSecp256r1mlkem768, Ztls_GroupStrP256mlkem768);
Ztls_DefineMlkemKeyex(secp384r1mlkem1024, Ztls_GroupP384mlkem1024, Ztls_GroupStrP384mlkem1024);
Ztls_DefineMlkemKeyex(mlkem512, Ztls_GroupMLKEM512, Ztls_GroupNameMLKEM512);
Ztls_DefineMlkemKeyex(mlkem768, Ztls_GroupMLKEM768, Ztls_GroupNameMLKEM768);
Ztls_DefineMlkemKeyex(mlkem1024, Ztls_GroupMLKEM1024, Ztls_GroupNameMLKEM1024);
#undef Ztls_DefineMlkemKeyex

const KeyExchangeAlgo *openssl_key_exchanges[] = {&openssl_secp256r1, NULL};
const KeyExchangeAlgo *openssl_key_exchanges_all[] = {&openssl_x25519mlkem768,
#if ZtlsOpenSSLHaveSecp521r1
  &openssl_secp521r1,
#endif
#if ZtlsOpenSSLHaveSecp384r1
  &openssl_secp384r1,
#endif
#if ZtlsOpenSSLHaveX25519
  &openssl_x25519,
#endif
  &openssl_secp384r1mlkem1024, &openssl_secp256r1mlkem768, &openssl_mlkem1024, &openssl_mlkem768,
  &openssl_mlkem512, &openssl_secp256r1, NULL};
static const OpenSSLCipherAlgo<OpenSSLCipherKind::AES128ECB> aes128ecbAlgo{
  "AES128-ECB", Ztls_AES128KeySize, Ztls_AESBlockSize, 0};
const CipherAlgo					    &openssl_aes128ecb = aes128ecbAlgo;
static const OpenSSLCipherAlgo<OpenSSLCipherKind::AES128CTR> aes128ctrAlgo{
  "AES128-CTR", Ztls_AES128KeySize, 1, Ztls_AESIVSize};
const CipherAlgo		 &openssl_aes128ctr = aes128ctrAlgo;
static const OpenSSLAeadAlgo<128> aes128gcmAlgo{"AES128-GCM", Ztls_AESGCMConfLimit,
  Ztls_AESGCMIntegrityLimit, &openssl_aes128ctr, &openssl_aes128ecb, Ztls_AES128KeySize,
  Ztls_AESGCMIVSize, Ztls_AESGCMTagSize,

  0, 0,

  1, 1};
const AeadAlgo			 &openssl_aes128gcm = aes128gcmAlgo;
static const OpenSSLCipherAlgo<OpenSSLCipherKind::AES256ECB> aes256ecbAlgo{
  "AES256-ECB", Ztls_AES256KeySize, Ztls_AESBlockSize, 0};
const CipherAlgo					    &openssl_aes256ecb = aes256ecbAlgo;
static const OpenSSLCipherAlgo<OpenSSLCipherKind::AES256CTR> aes256ctrAlgo{
  "AES256-CTR", Ztls_AES256KeySize, 1, Ztls_AESIVSize};
const CipherAlgo		 &openssl_aes256ctr = aes256ctrAlgo;
static const OpenSSLAeadAlgo<256> aes256gcmAlgo{"AES256-GCM", Ztls_AESGCMConfLimit,
  Ztls_AESGCMIntegrityLimit, &openssl_aes256ctr, &openssl_aes256ecb, Ztls_AES256KeySize,
  Ztls_AESGCMIVSize, Ztls_AESGCMTagSize,

  0, 0,

  1, 1};
const AeadAlgo			 &openssl_aes256gcm = aes256gcmAlgo;
static const OpenSSLHashAlgo<256> sha256Algo{
  "sha256", Ztls_SHA256BlockSize, Ztls_SHA256DigestSize, Ztls_ZeroDigestSHA256};
const HashAlgo			 &openssl_sha256 = sha256Algo;
static const OpenSSLHashAlgo<384> sha384Algo{
  "sha384", Ztls_SHA384BlockSize, Ztls_SHA384DigestSize, Ztls_ZeroDigestSHA384};
const HashAlgo			 &openssl_sha384 = sha384Algo;
static const OpenSSLHashAlgo<512> sha512Algo{
  "sha512", Ztls_SHA512BlockSize, Ztls_SHA512DigestSize, Ztls_ZeroDigestSHA512};
const HashAlgo	 &openssl_sha512 = sha512Algo;
const CipherSuite openssl_aes128gcmsha256 = {.id = Ztls_SuiteAes128gcmSHA256,
  .aead = &openssl_aes128gcm,
  .hash = &openssl_sha256,
  .name = Ztls_SuiteStrAes128gcm};
const CipherSuite openssl_aes256gcmsha384 = {.id = Ztls_SuiteAes256gcmSHA384,
  .aead = &openssl_aes256gcm,
  .hash = &openssl_sha384,
  .name = Ztls_SuiteStrAes256gcm};
#if ZtlsOpenSSLHaveChaCha
static const OpenSSLCipherAlgo<OpenSSLCipherKind::ChaCha20> chacha20Algo{
  "CHACHA20", Ztls_ChaCha20KeySize, 1, Ztls_ChaCha20IVSize};
const CipherAlgo		&openssl_chacha20 = chacha20Algo;
static const OpenSSLAeadAlgo<20> chachaPolyAlgo{"CHACHA20-POLY1305", Ztls_ChaChaConfLimit,
  Ztls_ChaChaIntLimit, &openssl_chacha20, nullptr, Ztls_ChaCha20KeySize, Ztls_ChaChaIVSize,
  Ztls_ChaChaTagSize, false, 0, true, true};
const AeadAlgo			&openssl_chacha20poly1305 = chachaPolyAlgo;
const CipherSuite		 openssl_chachasha256 = {.id = Ztls_SuiteChaChaSHA256,
  .aead = &openssl_chacha20poly1305,
  .hash = &openssl_sha256,
  .name = Ztls_SuiteStrChaChaSHA256};
#endif

const CipherSuites openssl_cipher_suites = []
{
  CipherSuites suites;
  // ciphers used with sha384 (must be first)
  suites.push(&openssl_aes256gcmsha384);

  // ciphers used with sha256
  suites.push(&openssl_aes128gcmsha256);
#if ZtlsOpenSSLHaveChaCha
  suites.push(&openssl_chachasha256);
#endif
  return suites;
}();

const CipherSuites openssl_cipher_suites_all = []
{
  CipherSuites suites;
  // ciphers used with sha384 (must be first)
  suites.push(&openssl_aes256gcmsha384);

  // ciphers used with sha256
  suites.push(&openssl_aes128gcmsha256);
#if ZtlsOpenSSLHaveChaCha
  suites.push(&openssl_chachasha256);
#endif
  return suites;
}();

#if ZtlsOpenSSLHaveBF
static const OpenSSLCipherAlgo<OpenSSLCipherKind::BlowfishECB> bfecbAlgo{
  "BF-ECB", Ztls_BlowfishKeySize, Ztls_BlowfishBlockSize, 0};
const CipherAlgo &openssl_bfecb = bfecbAlgo;
#endif

const HpkeKem openssl_hpke_kem_p256sha256 = {
  Ztls_HPKEKEMP256SHA256, &openssl_secp256r1, &openssl_sha256};
#if ZtlsOpenSSLHaveSecp384r1
const HpkeKem openssl_hpke_kem_p384sha384 = {
  Ztls_HPKEKEMP384SHA384, &openssl_secp384r1, &openssl_sha384};
#endif
#if ZtlsOpenSSLHaveX25519
const HpkeKem openssl_hpke_x25519sha256 = {
  Ztls_HPKEKEMX25519SHA256, &openssl_x25519, &openssl_sha256};
#endif
const HpkeKem *openssl_hpke_kems[] = {
#if ZtlsOpenSSLHaveSecp384r1
  &openssl_hpke_kem_p384sha384,
#endif
#if ZtlsOpenSSLHaveX25519
  &openssl_hpke_x25519sha256,
#endif
  &openssl_hpke_kem_p256sha256, NULL};

const HpkeSuite openssl_hpke_aes128gcmsha256 = {
  .id = {.kdf = Ztls_HPKEHKDFSHA256, .aead = Ztls_HPKEAEADAES128GCM},
  .name = "HKDF-SHA256/AES-128-GCM",
  .hash = &openssl_sha256,
  .aead = &openssl_aes128gcm
};
const HpkeSuite openssl_hpke_aes128gcmsha512 = {
  .id = {.kdf = Ztls_HPKEHKDFSHA512, .aead = Ztls_HPKEAEADAES128GCM},
  .name = "HKDF-SHA512/AES-128-GCM",
  .hash = &openssl_sha512,
  .aead = &openssl_aes128gcm
};
const HpkeSuite openssl_hpke_aes256gcmsha384 = {
  .id = {.kdf = Ztls_HPKEHKDFSHA384, .aead = Ztls_HPKEAEADAES256GCM},
  .name = "HKDF-SHA384/AES-256-GCM",
  .hash = &openssl_sha384,
  .aead = &openssl_aes256gcm
};
#if ZtlsOpenSSLHaveChaCha
const HpkeSuite openssl_hpke_chachasha256 = {
  .id = {.kdf = Ztls_HPKEHKDFSHA256, .aead = Ztls_HPKEAEADChaCha},
  .name = "HKDF-SHA256/ChaCha20Poly1305",
  .hash = &openssl_sha256,
  .aead = &openssl_chacha20poly1305
};
#endif
const HpkeSuite *openssl_hpke_cipher_suites[] = {&openssl_hpke_aes128gcmsha256,
  &openssl_hpke_aes256gcmsha384,
#if ZtlsOpenSSLHaveChaCha
  &openssl_hpke_chachasha256,
#endif
  &openssl_hpke_aes128gcmsha512,
  // likely only for tests
  NULL};

} // namespace Ztls_
