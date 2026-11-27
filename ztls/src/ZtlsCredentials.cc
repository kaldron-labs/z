//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// TLS credentials and session tickets

#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <string.h>
#include <openssl/core_names.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>

#include <zlib/ZuBase64.hh>
#include <zlib/ZuLib.hh>

#include <zlib/ZmHeap.hh>
#include <zlib/ZmSpecific.hh>

#include <zlib/ZfASN1.hh>

#include <zlib/ZiFile.hh>

#include <zlib/ZtlsCredentials.hh>
#include <zlib/ZtlsEngineOpenSSL.hh>
#include <zlib/ZtlsRandom.hh>

namespace Ztls {

struct CertStore : public ZmNHeap<CertStore> {
friend inline ZuStringT<"Ztls.CertStore"> ZmNHeapID(CertStore *);
  X509_STORE	*store = nullptr;
};

struct SignCert : public ZmNHeap<SignCert> {
friend inline ZuStringT<"Ztls.SignCert"> ZmNHeapID(SignCert *);
  Ztls_::OpenSSLSignCert	impl{};
};

struct VerifyCert : public ZmNHeap<VerifyCert> {
friend inline ZuStringT<"Ztls.VerifyCert"> ZmNHeapID(VerifyCert *);
  Ztls_::OpenSSLVerifyCert	impl{};
};

struct TicketKey : public ZmNHeap<TicketKey>, Ztls_::EncryptTicket {
friend inline ZuStringT<"Ztls.TicketKey"> ZmNHeapID(TicketKey *);
  uint8_t	name[16]{};
  uint8_t	aesKey[32]{};
  uint8_t	hmacKey[32]{};

  ~TicketKey() override
  {
    ZuClear(name, sizeof(name));
    ZuClear(aesKey, sizeof(aesKey));
    ZuClear(hmacKey, sizeof(hmacKey));
  }
  int call(Ztls::Cxn *, int isEncrypt, Ztls_::Buffer *dst, Ztls_::Bytes src) override;
};

static int ticketKeyHook(
  unsigned char *keyName, unsigned char *iv, EVP_CIPHER_CTX *ctx, EVP_MAC_CTX *hctx, int enc)
{
  auto key = ZmTLS<TicketKey *, (int TicketKey::*){}>();
  if (!key) return -1;
  OSSL_PARAM params[] = {
    OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST, const_cast<char *>("SHA256"), 0),
    OSSL_PARAM_construct_end()};
  if (enc) {
    memcpy(keyName, key->name, sizeof(key->name));
    Ztls::Random random;
    if (!random.random({iv, unsigned(EVP_CIPHER_iv_length(EVP_aes_256_cbc()))}) ||
      EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key->aesKey, iv) != 1)
      return -1;
    if (EVP_MAC_init(hctx, key->hmacKey, sizeof(key->hmacKey), params) != 1) return -1;
    return 1;
  }
  if (memcmp(keyName, key->name, sizeof(key->name))) return 0;
  EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key->aesKey, iv);
  if (EVP_MAC_init(hctx, key->hmacKey, sizeof(key->hmacKey), params) != 1) return -1;
  return 1;
}

int TicketKey::call(Ztls::Cxn *, int isEncrypt, Ztls_::Buffer *dst, Ztls_::Bytes src)
{
  auto &key = ZmTLS<TicketKey *, (int TicketKey::*){}>();
  key = this;
  int ret = isEncrypt ? Ztls_::opensslEncryptTicketEvp(dst, src, ticketKeyHook) :
			Ztls_::opensslDecryptTicketEvp(dst, src, ticketKeyHook);
  key = nullptr;
  return ret;
}

CertStore *certStoreNew()
{
  auto store = new CertStore{};
  store->store = X509_STORE_new();
  if (!store->store) {
    delete store;
    return nullptr;
  }
  return store;
}

void certStoreFree(CertStore *store)
{
  if (!store) return;
  if (store->store) X509_STORE_free(store->store);
  delete store;
}

bool certStoreLoadPath(CertStore *store, const char *path)
{
  if (!store || !store->store || !path) return false;
  return X509_STORE_load_locations(store->store, nullptr, path) == 1;
}

bool certStoreLoadFile(CertStore *store, const char *path)
{
  if (!store || !store->store || !path) return false;
  return X509_STORE_load_locations(store->store, path, nullptr) == 1;
}

static X509 *x509DER(ZuBSpan data)
{
  if (!data) return nullptr;
  const unsigned char *ptr = data.data();
  const unsigned char *end = ptr + data.length();
  auto [prefix, content] = ZfASN1::loadTL<ZfASN1::tagU(), ZfASN1::Sequence>(ZuCSpan{data});
  auto length = data.length();
  if (prefix > 0 && unsigned(prefix) <= length && content <= length - unsigned(prefix))
    length = unsigned(prefix) + unsigned(content);
  // The native decoder still determines acceptance, including exact consumption.
  X509 *cert = d2i_X509(nullptr, &ptr, length);
  if (cert && ptr != end) {
    X509_free(cert);
    cert = nullptr;
  }
  return cert;
}

static bool certStoreAdd(CertStore *store, X509 *cert)
{
  int ok = X509_STORE_add_cert(store->store, cert);
  if (ok != 1) {
    unsigned long err = ERR_peek_last_error();
    if (ERR_GET_LIB(err) == ERR_LIB_X509 &&
      ERR_GET_REASON(err) == X509_R_CERT_ALREADY_IN_HASH_TABLE) {
      ERR_clear_error();
      ok = 1;
    }
  }
  return ok == 1;
}

bool certStoreAddDER(CertStore *store, ZuBSpan data)
{
  if (!store || !store->store) return false;
  X509 *cert = x509DER(data);
  if (!cert) return false;
  bool ok = certStoreAdd(store, cert);
  X509_free(cert);
  return ok;
}

bool certStoreAddCA(CertStore *store, ZuBSpan data)
{
  if (!store || !store->store) return false;
  X509 *cert = x509DER(data);
  if (!cert) return false;
  bool ok = X509_cmp_current_time(X509_get0_notBefore(cert)) < 0 &&
    X509_cmp_current_time(X509_get0_notAfter(cert)) > 0 && X509_check_ca(cert) > 0 &&
    certStoreAdd(store, cert);
  X509_free(cert);
  return ok;
}

VerifyCert *verifyCertNew(CertStore *store)
{
  if (!store || !store->store) return nullptr;
  auto verify = new VerifyCert{};
  if (Ztls_::opensslInitVerifyCert(&verify->impl, store->store) != 0) {
    delete verify;
    return nullptr;
  }
  return verify;
}

void verifyCertFree(VerifyCert *verify)
{
  if (!verify) return;
  Ztls_::opensslDisposeVerifyCert(&verify->impl);
  delete verify;
}

Ztls_::VerifyCertificate *verifyCertHook(VerifyCert *verify)
{ return verify ? &verify->impl : nullptr; }

SignCert *signCertNew(const PK::Key &key)
{
  if (!key.get()) return nullptr;
  auto sign = new SignCert{};
  if (Ztls_::opensslInitSignCert(&sign->impl, key.get()) != 0) {
    delete sign;
    return nullptr;
  }
  return sign;
}

void signCertFree(SignCert *sign)
{
  if (!sign) return;
  Ztls_::opensslDisposeSignCert(&sign->impl);
  delete sign;
}

Ztls_::SignCertificate *signCertHook(SignCert *sign) { return sign ? &sign->impl : nullptr; }

bool signCertAsync(SignCert *sign, bool async)
{
  if (!sign) return false;
#ifdef _WIN32
  // OpenSSL asynchronous signing exposes an int fd for async jobs while Windows HANDLE is
  // pointer-sized.
  if (async) return false;
  sign->impl.async = 0;
  return true;
#else
#if defined(ZtlsOpenSSLHaveAsync) && ZtlsOpenSSLHaveAsync
  sign->impl.async = async ? 1 : 0;
  return true;
#else
  sign->impl.async = 0;
  return !async;
#endif
#endif
}

ZuDerive(CertificatePEM, (ZtBArray<ZtArrayHeapID<"Ztls.Engine.CertificatePEM">>));

bool loadCerts(Ztls_::Context *ctx, const char *path)
{
  if (!ctx || !path) return false;
  ZiFile file;
  if (file.open(path, ZiFile::ReadOnly) != Zi::OK) return false;
  auto size = file.size();
  if (size <= 0 || size > INT_MAX) return false;
  CertificatePEM pem;
  pem.length(unsigned(size));
  if (file.read(pem.data(), pem.length()) != size) return false;

  Ztls_::CertificateChain chain;

  constexpr char begin[] = "-----BEGIN CERTIFICATE-----";
  constexpr char end[] = "-----END CERTIFICATE-----";
  auto		 cursor = pem.data(), limit = cursor + pem.length(), body = cursor;
  unsigned	 encoded = 0, padding = 0;
  bool		 inCert = false, malformed = false;
  while (cursor != limit) {
    auto line = cursor;
    auto nl = static_cast<uint8_t *>(memchr(cursor, '\n', limit - cursor));
    cursor = nl ? nl + 1 : limit;
    if (!inCert) {
      if (cursor - line >= (sizeof(begin) - 1) && !memcmp(line, begin, (sizeof(begin) - 1))) {
	inCert = true;
	malformed = false;
	body = cursor;
	encoded = padding = 0;
      }
      continue;
    }
    if (cursor - line >= (sizeof(end) - 1) && !memcmp(line, end, (sizeof(end) - 1))) {
      inCert = false;
      if (malformed) return false;
      if (!encoded) continue;
      if ((encoded + padding) % 4) return false;
      unsigned		    capacity = ZuBase64::declen(encoded);
      Ztls_::CertificateDER der(capacity, capacity);
      auto		    length = ZuBase64::decode({der.data(), capacity}, {body, encoded});
      der.length(length);
      chain.push(ZuMv(der));
      continue;
    }
    if (padding || malformed) continue;
    auto stop = cursor;
    if (auto nul = static_cast<uint8_t *>(memchr(line, 0, stop - line))) stop = nul;
    while (line != stop && (*line == ' ' || *line == '\t' || *line == '\r' || *line == '\n'))
      ++line;
    while (line != stop && (stop[-1] == ' ' || (stop[-1] >= '\t' && stop[-1] <= '\r'))) --stop;
    while (line != stop && ZuBase64::lookup(*line) < 64) body[encoded++] = *line++;
    if (line != stop) {
      padding = encoded % 4 == 2 ? 2 : encoded % 4 == 3 ? 1 : 0;
      malformed =
	*line != '=' || !padding || stop - line < padding || (padding == 2 && line[1] != '=');
      // The existing loader accepts text after the padding on the final line.
    }
  }
  if (!chain.length()) return false;
  ctx->certificates = ZuMv(chain);
  return true;
}

bool loadCertsDER(Ztls_::Context *ctx, ZuSpan<const ZuBSpan> certs, const PK::Key &key)
{
  if (!ctx || !certs.length() || !key.get() || !PK::check(key, true)) return false;

  STACK_OF(X509) *parsed = sk_X509_new_null();
  STACK_OF(X509) *untrusted = sk_X509_new_null();
  X509_STORE	 *store = X509_STORE_new();
  X509_STORE_CTX *verify = X509_STORE_CTX_new();
  bool		  ok = parsed && untrusted && store && verify;

  for (unsigned i = 0; ok && i < certs.length(); ++i) {
    X509 *cert = x509DER(certs[i]);
    if (!cert || !sk_X509_push(parsed, cert)) {
      X509_free(cert);
      ok = false;
    }
  }

  X509	   *leaf = ok ? sk_X509_value(parsed, 0) : nullptr;
  EVP_PKEY *pub = leaf ? X509_get_pubkey(leaf) : nullptr;
  ok = ok && pub && EVP_PKEY_eq(pub, key.get()) == 1;
  EVP_PKEY_free(pub);

  X509 *anchor = ok ? sk_X509_value(parsed, certs.length() - 1) : nullptr;
  if (ok) ok = X509_STORE_add_cert(store, anchor) == 1;
  for (unsigned i = 1; ok && i + 1 < certs.length(); ++i)
    if (!sk_X509_push(untrusted, sk_X509_value(parsed, i))) ok = false;
  if (ok) ok = X509_STORE_CTX_init(verify, store, leaf, untrusted) == 1;
  if (ok) X509_STORE_CTX_set_flags(verify, X509_V_FLAG_PARTIAL_CHAIN | X509_V_FLAG_X509_STRICT);
  if (ok) ok = X509_STORE_CTX_set_purpose(verify, X509_PURPOSE_SSL_CLIENT) == 1;
  if (ok) ok = X509_verify_cert(verify) == 1;

  if (ok) {
    Ztls_::CertificateChain chain;
    chain.ensure(certs.length());
    for (auto cert : certs) chain.push(Ztls_::CertificateDER(cert.data(), cert.length()));
    ctx->certificates = ZuMv(chain);
  }
  X509_STORE_CTX_free(verify);
  X509_STORE_free(store);
  sk_X509_free(untrusted);
  sk_X509_pop_free(parsed, X509_free);
  return ok;
}

TicketKey *ticketKeyNew()
{
  if (!cryptoInit()) return nullptr;
  auto	       key = new TicketKey{};
  Ztls::Random random;
  if (!random.random(key->name) || !random.random(key->aesKey) || !random.random(key->hmacKey)) {
    delete key;
    return nullptr;
  }
  return key;
}

void ticketKeyFree(TicketKey *key)
{
  if (!key) return;
  delete key;
}

Ztls_::EncryptTicket *ticketEncryptHook(TicketKey *key) { return key; }

} // namespace Ztls
