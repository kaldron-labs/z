//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// zpicotls buffer hook integration

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include <zlib/ZuLib.hh>
#include <zlib/ZtlsPico.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmVHeap.hh>
#include <zlib/ZiIOBuf.hh>

#include <openssl/core_names.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/x509v3.h>

#include <zlib/ZmSpecific.hh>

namespace Ztls::Pico {

struct Counters {
  ZmAtomic<uint64_t> origin_alloc{0};
  ZmAtomic<uint64_t> internal_alloc{0};
  ZmAtomic<uint64_t> origin_free{0};
  ZmAtomic<uint64_t> internal_free{0};
  ZmAtomic<uint64_t> origin_align_fail{0};
  ZmAtomic<uint64_t> origin_ensure_fail{0};
};

static Counters counters;

using CtxHeap = ZmVHeap<
  "Ztls.Pico.Ctx", 128, (1U<<17), ZiIOBuf_Align>;
using BufHeap = ZmVHeap<
  "Ztls.Pico.Buf", ZmVHeap_DefltMin, ZmVHeap_DefltMax, ZiIOBuf_Align>;

constexpr unsigned alignBits(unsigned value) {
  unsigned bits = 0;
  while ((1u << bits) < value) ++bits;
  return bits;
}

ZuAssert((ZiIOBuf_Align & (ZiIOBuf_Align - 1)) == 0,
  "ZiIOBuf_Align must be a power of two");

constexpr unsigned IOBufAlignBits = alignBits(ZiIOBuf_Align);

#if defined(__has_feature)
# if __has_feature(address_sanitizer)
#  define ZtlsPico_ASAN 1
# endif
#endif
#if defined(__SANITIZE_ADDRESS__)
# define ZtlsPico_ASAN 1
#endif
#if !defined(ZtlsPico_ASAN)
# define ZtlsPico_ASAN 0
#endif

#if Ztls_Fusion
ZuAssert(IOBufAlignBits >= PTLS_X86_CACHE_LINE_ALIGN_BITS,
  "ZiIOBuf_Align must satisfy zpicotls fusion alignment");
#endif

static uint8_t *internal_alloc_(unsigned capacity)
{
#if ZtlsPico_ASAN
  return static_cast<uint8_t *>(
    ZmHeapAlloc<ZiIOBuf_Align>(capacity));
#else
  return static_cast<uint8_t *>(BufHeap::valloc(capacity));
#endif
}

static void internal_free_(void *ptr)
{
#if ZtlsPico_ASAN
  Zm::alignedFree(ptr);
#else
  BufHeap::vfree(ptr);
#endif
}

static void *buffer_alloc(ptls_buffer_t *pbuf, size_t capacity,
    uint8_t align_bits, int tx)
{
  pbuf->tx = tx ? 1 : 0;
  if (pbuf->origin) {
    if (ZuUnlikely(align_bits > IOBufAlignBits)) {
      counters.origin_align_fail++;
      return nullptr;
    }
    auto buf = static_cast<ZiIOBuf *>(pbuf->origin);
    auto raw = buf->data_();
    // Preserve zpicotls' base offset across ZiIOBuf relocation.
    ptrdiff_t offset = pbuf->base ? pbuf->base - raw : 0;
    if (ZuUnlikely(offset < 0)) {
      counters.origin_ensure_fail++;
      return nullptr;
    }
    auto offset_ = size_t(offset);
    if (ZuUnlikely(capacity > UINT32_MAX ||
	  offset_ > UINT32_MAX - capacity)) {
      counters.origin_ensure_fail++;
      return nullptr;
    }
    auto skip = buf->skip;
    auto length = buf->length;
    auto live = size_t(skip) + length;
    if (live < offset_ + pbuf->off) live = offset_ + pbuf->off;
    if (ZuUnlikely(live > buf->size)) {
      counters.origin_ensure_fail++;
      return nullptr;
    }
    buf->skip = 0;
    buf->length = uint32_t(live);
    auto grown = buf->ensure(uint32_t(offset_ + capacity));
    buf->skip = skip;
    buf->length = length;
    if (ZuUnlikely(!grown)) {
      counters.origin_ensure_fail++;
      return nullptr;
    }
    raw = buf->data_();
    // Capacity remains relative to the preserved base, not data().
    pbuf->base = raw + offset_;
    pbuf->capacity = buf->size - offset_;
    pbuf->is_allocated = 1;
    pbuf->align_bits = align_bits;
    counters.origin_alloc++;
    return pbuf->base;
  }

  // Empty/oversized requests are rejected before allocation in every build.
  if (ZuUnlikely(!capacity || capacity > UINT32_MAX)) return nullptr;
  auto newp = internal_alloc_(unsigned(capacity));
  if (pbuf->off) memcpy(newp, pbuf->base, pbuf->off);
  OPENSSL_cleanse(pbuf->base, pbuf->off);
  if (pbuf->is_allocated) internal_free_(pbuf->base);
  pbuf->base = newp;
  pbuf->capacity = capacity;
  pbuf->is_allocated = 1;
  pbuf->align_bits = align_bits;
  counters.internal_alloc++;
  return newp;
}

static void buffer_free(ptls_buffer_t *pbuf, int tx)
{
  (void)tx;
  if (ZuUnlikely(!pbuf || !pbuf->base)) return;
  if (pbuf->origin) {
    counters.origin_free++;
    return;
  }
  internal_free_(pbuf->base);
  counters.internal_free++;
}

void install()
{
  static const bool done = []() {
    ptls_buffer_alloc = buffer_alloc;
    ptls_buffer_free = buffer_free;
    return true;
  }();
  (void)done;
}

Stats stats()
{
  return Stats{
    counters.origin_alloc.load_(),
    counters.internal_alloc.load_(),
    counters.origin_free.load_(),
    counters.internal_free.load_(),
    counters.origin_align_fail.load_(),
    counters.origin_ensure_fail.load_()
  };
}

void reset_stats()
{
  counters.origin_alloc = 0;
  counters.internal_alloc = 0;
  counters.origin_free = 0;
  counters.internal_free = 0;
  counters.origin_align_fail = 0;
  counters.origin_ensure_fail = 0;
}

bool AeadCtx::init(
  ptls_aead_algorithm_t *algo, bool enc, const void *key, const void *iv)
{
  clear();
  if (!algo || !key || !iv ||
      algo->context_size < sizeof(ptls_aead_context_t) ||
      algo->context_size > UINT16_MAX ||
      algo->align_bits > IOBufAlignBits)
    return false;

  auto storage = CtxHeap::valloc(algo->context_size);
  auto ctx = reinterpret_cast<ptls_aead_context_t *>(
    storage);
  *ctx = ptls_aead_context_t{algo};
  if (algo->setup_crypto(ctx, enc ? 1 : 0, key, iv)) {
    OPENSSL_cleanse(ctx, algo->context_size);
    CtxHeap::vfree(storage);
    return false;
  }
  if (enc) {
    if (!ctx->dispose_crypto || !ctx->do_encrypt || !ctx->do_encrypt_v ||
	!ctx->do_encrypt_v_s) {
      if (ctx->dispose_crypto) ctx->dispose_crypto(ctx);
      OPENSSL_cleanse(ctx, algo->context_size);
      CtxHeap::vfree(storage);
      return false;
    }
  } else if (!ctx->dispose_crypto || !ctx->do_decrypt) {
    if (ctx->dispose_crypto) ctx->dispose_crypto(ctx);
    OPENSSL_cleanse(ctx, algo->context_size);
    CtxHeap::vfree(storage);
    return false;
  }

  m_storage = storage;
  m_ctx = ctx;
  m_size = uint16_t(algo->context_size);
  return true;
}

void AeadCtx::clear()
{
  if (!m_storage) return;
  if (m_ctx) {
    m_ctx->dispose_crypto(m_ctx);
    if (m_size) OPENSSL_cleanse(m_ctx, m_size);
  }
  CtxHeap::vfree(m_storage);
  m_storage = nullptr;
  m_ctx = nullptr;
  m_size = 0;
}

bool CipherCtx::init(ptls_cipher_algorithm_t *algo, bool enc, const void *key)
{
  clear();
  if (!algo || !key ||
      algo->context_size < sizeof(ptls_cipher_context_t) ||
      algo->context_size > UINT16_MAX)
    return false;

  auto storage = CtxHeap::valloc(algo->context_size);
  auto ctx = reinterpret_cast<ptls_cipher_context_t *>(
    storage);
  *ctx = ptls_cipher_context_t{algo};
  if (algo->setup_crypto(ctx, enc ? 1 : 0, key)) {
    OPENSSL_cleanse(ctx, algo->context_size);
    CtxHeap::vfree(storage);
    return false;
  }
  if (!ctx->do_init || !ctx->do_transform || !ctx->do_dispose) {
    if (ctx->do_dispose) ctx->do_dispose(ctx);
    OPENSSL_cleanse(ctx, algo->context_size);
    CtxHeap::vfree(storage);
    return false;
  }

  m_storage = storage;
  m_ctx = ctx;
  m_size = uint16_t(algo->context_size);
  return true;
}

void CipherCtx::clear()
{
  if (!m_storage) return;
  if (m_ctx) {
    m_ctx->do_dispose(m_ctx);
    if (m_size) OPENSSL_cleanse(m_ctx, m_size);
  }
  CtxHeap::vfree(m_storage);
  m_storage = nullptr;
  m_ctx = nullptr;
  m_size = 0;
}

struct CertStore : public ZmNHeap<CertStore> {
friend inline ZuStringT<"Ztls.Pico.CertStore"> ZmNHeapID(CertStore *);
  X509_STORE	*store = nullptr;
};

struct SignCert : public ZmNHeap<SignCert> {
friend inline ZuStringT<"Ztls.Pico.SignCert"> ZmNHeapID(SignCert *);
  ptls_openssl_sign_certificate_t impl{};
};

struct VerifyCert : public ZmNHeap<VerifyCert> {
friend inline ZuStringT<"Ztls.Pico.VerifyCert"> ZmNHeapID(VerifyCert *);
  ptls_openssl_verify_certificate_t impl{};
};

struct TicketKey : public ZmNHeap<TicketKey> {
friend inline ZuStringT<"Ztls.Pico.TicketKey"> ZmNHeapID(TicketKey *);
  ptls_encrypt_ticket_t super{};
  uint8_t name[16]{};
  uint8_t aes_key[32]{};
  uint8_t hmac_key[32]{};
};

static int ticket_key_cb(unsigned char *key_name, unsigned char *iv,
  EVP_CIPHER_CTX *ctx, EVP_MAC_CTX *hctx, int enc)
{
  auto key = ZmTLS<TicketKey *, (int TicketKey::*){}>();
  if (!key) return -1;
  OSSL_PARAM params[] = {
    OSSL_PARAM_construct_utf8_string(
      OSSL_MAC_PARAM_DIGEST, const_cast<char *>("SHA256"), 0),
    OSSL_PARAM_construct_end()
  };
  if (enc) {
    memcpy(key_name, key->name, sizeof(key->name));
    if (RAND_bytes(
	  iv, EVP_CIPHER_iv_length(EVP_aes_256_cbc())) != 1 ||
	EVP_EncryptInit_ex(
	  ctx, EVP_aes_256_cbc(), nullptr, key->aes_key, iv) != 1)
      return -1;
    if (EVP_MAC_init(hctx, key->hmac_key, sizeof(key->hmac_key), params) != 1)
      return -1;
    return 1;
  }
  if (memcmp(key_name, key->name, sizeof(key->name))) return 0;
  EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key->aes_key, iv);
  if (EVP_MAC_init(hctx, key->hmac_key, sizeof(key->hmac_key), params) != 1)
    return -1;
  return 1;
}

static int ticket_encrypt_cb_(ptls_encrypt_ticket_t *self, ptls_t *,
  int is_encrypt, ptls_buffer_t *dst, ptls_iovec_t src)
{
  auto &key = ZmTLS<TicketKey *, (int TicketKey::*){}>();
  key = reinterpret_cast<TicketKey *>(self);
  int ret = is_encrypt ?
    ptls_openssl_encrypt_ticket_evp(dst, src, ticket_key_cb) :
    ptls_openssl_decrypt_ticket_evp(dst, src, ticket_key_cb);
  key = nullptr;
  return ret;
}

bool init()
{
  if (!cryptoInit()) return false;
  install();
  return true;
}

ptls_cipher_suite_t *fusion_cipher_suite(uint16_t id)
{
#if Ztls_Fusion
  // Normal Fusion preserves exact in-place decrypt; non-temporal variants do not.
  if (!ptls_fusion_is_supported_by_cpu()) return nullptr;
  switch (id) {
    case PTLS_CIPHER_SUITE_AES_256_GCM_SHA384: {
      static ptls_cipher_suite_t suite{
	.id = PTLS_CIPHER_SUITE_AES_256_GCM_SHA384,
	.aead = &ptls_fusion_aes256gcm,
	.hash = &ptls_openssl_sha384,
	.name = PTLS_CIPHER_SUITE_NAME_AES_256_GCM_SHA384
      };
      return &suite;
    }
    case PTLS_CIPHER_SUITE_AES_128_GCM_SHA256: {
      static ptls_cipher_suite_t suite{
	.id = PTLS_CIPHER_SUITE_AES_128_GCM_SHA256,
	.aead = &ptls_fusion_aes128gcm,
	.hash = &ptls_openssl_sha256,
	.name = PTLS_CIPHER_SUITE_NAME_AES_128_GCM_SHA256
      };
      return &suite;
    }
  }
#endif
  return nullptr;
}

CertStore *cert_store_new()
{
  auto store = new CertStore{};
  store->store = X509_STORE_new();
  if (!store->store) { delete store; return nullptr; }
  return store;
}

void cert_store_free(CertStore *store)
{
  if (!store) return;
  if (store->store) X509_STORE_free(store->store);
  delete store;
}

bool cert_store_load_path(CertStore *store, const char *path)
{
  if (!store || !store->store || !path) return false;
  return X509_STORE_load_locations(store->store, nullptr, path) == 1;
}

bool cert_store_load_file(CertStore *store, const char *path)
{
  if (!store || !store->store || !path) return false;
  return X509_STORE_load_locations(store->store, path, nullptr) == 1;
}

static X509 *x509_der(ZuBSpan data)
{
  if (!data) return nullptr;
  const unsigned char *ptr = data.data();
  const unsigned char *end = ptr + data.length();
  X509 *cert = d2i_X509(nullptr, &ptr, data.length());
  if (cert && ptr != end) {
    X509_free(cert);
    cert = nullptr;
  }
  return cert;
}

static bool cert_store_add_(CertStore *store, X509 *cert)
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

bool cert_store_add_der(CertStore *store, ZuBSpan data)
{
  if (!store || !store->store) return false;
  X509 *cert = x509_der(data);
  if (!cert) return false;
  bool ok = cert_store_add_(store, cert);
  X509_free(cert);
  return ok;
}

bool cert_store_add_ca(CertStore *store, ZuBSpan data)
{
  if (!store || !store->store) return false;
  X509 *cert = x509_der(data);
  if (!cert) return false;
  bool ok = X509_cmp_current_time(X509_get0_notBefore(cert)) < 0 &&
    X509_cmp_current_time(X509_get0_notAfter(cert)) > 0 &&
    X509_check_ca(cert) > 0 && cert_store_add_(store, cert);
  X509_free(cert);
  return ok;
}

VerifyCert *verify_cert_new(CertStore *store)
{
  if (!store || !store->store) return nullptr;
  auto verify = new VerifyCert{};
  if (ptls_openssl_init_verify_certificate(&verify->impl, store->store) != 0) {
    delete verify;
    return nullptr;
  }
  return verify;
}

void verify_cert_free(VerifyCert *verify)
{
  if (!verify) return;
  ptls_openssl_dispose_verify_certificate(&verify->impl);
  delete verify;
}

ptls_verify_certificate_t *verify_cert_cb(VerifyCert *verify)
{
  return verify ? &verify->impl.super : nullptr;
}

SignCert *sign_cert_new(const PK::Key &key)
{
  if (!key.get()) return nullptr;
  auto sign = new SignCert{};
  if (ptls_openssl_init_sign_certificate(&sign->impl, key.get()) != 0) {
    delete sign;
    return nullptr;
  }
  return sign;
}

void sign_cert_free(SignCert *sign)
{
  if (!sign) return;
  ptls_openssl_dispose_sign_certificate(&sign->impl);
  delete sign;
}

ptls_sign_certificate_t *sign_cert_cb(SignCert *sign)
{
  return sign ? &sign->impl.super : nullptr;
}

bool sign_cert_async(SignCert *sign, bool async)
{
  if (!sign) return false;
#ifdef _WIN32
  // zpicotls exposes an int fd for async jobs while Windows HANDLE is pointer-sized.
  if (async) return false;
  sign->impl.async = 0;
  return true;
#else
#if defined(PTLS_OPENSSL_HAVE_ASYNC) && PTLS_OPENSSL_HAVE_ASYNC
  sign->impl.async = async ? 1 : 0;
  return true;
#else
  sign->impl.async = 0;
  return !async;
#endif
#endif
}

bool load_certificates(ptls_context_t *ctx, const char *path)
{
  if (!ctx || !path) return false;
  return ptls_load_certificates(ctx, path) == 0;
}

bool load_certificates_der(
  ptls_context_t *ctx, ZuSpan<const ZuBSpan> certs, const PK::Key &key)
{
  if (!ctx || !certs.length() || !key.get() ||
      !PK::check(key, true)) return false;

  STACK_OF(X509) *parsed = sk_X509_new_null();
  STACK_OF(X509) *untrusted = sk_X509_new_null();
  X509_STORE *store = X509_STORE_new();
  X509_STORE_CTX *verify = X509_STORE_CTX_new();
  ptls_iovec_t *list = nullptr;
  bool ok = parsed && untrusted && store && verify;
  unsigned count = certs.length();

  for (unsigned i = 0; ok && i < count; ++i) {
    X509 *cert = x509_der(certs[i]);
    if (!cert || !sk_X509_push(parsed, cert)) {
      X509_free(cert);
      ok = false;
    }
  }

  X509 *leaf = ok ? sk_X509_value(parsed, 0) : nullptr;
  EVP_PKEY *pub = leaf ? X509_get_pubkey(leaf) : nullptr;
  ok = ok && pub && EVP_PKEY_eq(pub, key.get()) == 1;
  EVP_PKEY_free(pub);

  X509 *anchor = ok ? sk_X509_value(parsed, count - 1) : nullptr;
  if (ok) ok = X509_STORE_add_cert(store, anchor) == 1;
  for (unsigned i = 1; ok && i + 1 < count; ++i)
    if (!sk_X509_push(untrusted, sk_X509_value(parsed, i))) ok = false;
  if (ok) ok = X509_STORE_CTX_init(verify, store, leaf, untrusted) == 1;
  if (ok) X509_STORE_CTX_set_flags(
    verify, X509_V_FLAG_PARTIAL_CHAIN | X509_V_FLAG_X509_STRICT);
  if (ok) ok = X509_STORE_CTX_set_purpose(
    verify, X509_PURPOSE_SSL_CLIENT) == 1;
  if (ok) ok = X509_verify_cert(verify) == 1;

  if (ok) {
    list = static_cast<ptls_iovec_t *>(
      calloc(count, sizeof(ptls_iovec_t)));
    ok = !!list;
  }
  for (unsigned i = 0; ok && i < count; ++i) {
    auto cert = certs[i];
    list[i].base = static_cast<uint8_t *>(malloc(cert.length()));
    if (!list[i].base) {
      ok = false;
      break;
    }
    memcpy(list[i].base, cert.data(), cert.length());
    list[i].len = cert.length();
  }
  if (ok) {
    if (ctx->certificates.list) {
      for (size_t i = 0; i < ctx->certificates.count; ++i)
	free(ctx->certificates.list[i].base);
      free(ctx->certificates.list);
    }
    ctx->certificates.list = list;
    ctx->certificates.count = count;
    list = nullptr;
  }
  if (list) {
    for (unsigned i = 0; i < count; ++i) free(list[i].base);
    free(list);
  }
  X509_STORE_CTX_free(verify);
  X509_STORE_free(store);
  sk_X509_free(untrusted);
  sk_X509_pop_free(parsed, X509_free);
  return ok;
}

TicketKey *ticket_key_new()
{
  if (!init()) return nullptr;
  auto key = new TicketKey{};
  key->super.cb = ticket_encrypt_cb_;
  if (RAND_bytes(key->name, sizeof(key->name)) != 1 ||
      RAND_bytes(key->aes_key, sizeof(key->aes_key)) != 1 ||
      RAND_bytes(key->hmac_key, sizeof(key->hmac_key)) != 1) {
    delete key;
    return nullptr;
  }
  return key;
}

void ticket_key_free(TicketKey *key)
{
  if (!key) return;
  delete key;
}

ptls_encrypt_ticket_t *ticket_encrypt_cb(TicketKey *key)
{
  return key ? &key->super : nullptr;
}

} // namespace Ztls::Pico
