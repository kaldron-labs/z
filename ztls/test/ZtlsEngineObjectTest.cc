//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#define OPENSSL_SUPPRESS_DEPRECATED
#include <openssl/ec.h>

#ifndef _WIN32
#include <dlfcn.h>

#endif

#include <openssl/evp.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmHeap.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZtlsEngineOpenSSL.hh>

#if ZtlsFusion
#include <zlib/ZtlsFusion.hh>

#endif

using namespace ZuTestUtil;
using namespace Ztls_;

#ifndef _WIN32
// Native ABI interposition injects one point-encoding failure after EC ownership
// has been staged. It never changes the frozen libcrypto implementation.
static bool	failECEncoding = false;
static unsigned failedECEncodings = 0;
extern "C" __attribute__((visibility("default"))) size_t EC_POINT_point2oct(
  const EC_GROUP *group, const EC_POINT *point, point_conversion_form_t form,
  unsigned char *output, size_t capacity, BN_CTX *ctx)
{
  if (failECEncoding) {
    ++failedECEncodings;
    return 0;
  }
  using EncodePoint = decltype(&EC_POINT_point2oct);
  auto encode = reinterpret_cast<EncodePoint>(dlsym(RTLD_NEXT, "EC_POINT_point2oct"));
  return encode(group, point, form, output, capacity, ctx);
}

static void countECRelease(void *, void *ptr, CRYPTO_EX_DATA *, int, long, void *)
{
  if (ptr) ++*static_cast<unsigned *>(ptr);
}

static void ecInitFailure()
{
  ZuTestScope(ecInitFailure);
  auto key = EVP_EC_gen("prime256v1");
  ZuCheck(key);
  if (!key) return;
  unsigned releases = 0;
  auto	   legacy = EVP_PKEY_get1_EC_KEY(key);
  int	   index = EC_KEY_get_ex_new_index(0, nullptr, nullptr, nullptr, countECRelease);
  ZuCheck(legacy && index >= 0 && EC_KEY_set_ex_data(legacy, index, &releases));
  EC_KEY_free(legacy);
  KeyExchangeContext *context = nullptr;
  failedECEncodings = 0;
  failECEncoding = true;
  int result = opensslCreateKeyExchange(&context, key);
  failECEncoding = false;
  ZuCheck(result == Ztls_ErrorNoMemory && !context && failedECEncodings == 1);
  ZuCheck(!releases);
  EVP_PKEY_free(key);
  ZuCheck(releases == 1);
}
#endif

static void digest(const HashAlgo &algo, const EVP_MD *native)
{
  ZuTestScope(digest);
  uint8_t  expected[Ztls_MaxDigestSize], result[Ztls_MaxDigestSize];
  unsigned size;
  ZuCheck(EVP_Digest("abc", 3, expected, &size, native, nullptr) == 1);
  auto hash = algo.create();
  hash->update("a", 1);
  auto copy = hash->clone();
  ZuCheck(copy);
  hash->update("bc", 2);
  hash->final(result, HashFinalMode::Snapshot);
  ZuCheck(!memcmp(result, expected, size));
  hash->final(result, HashFinalMode::Reset);
  ZuCheck(!memcmp(result, expected, size));
  hash->final(result, HashFinalMode::Snapshot);
  ZuCheck(!memcmp(result, algo.emptyDigest, size));
  copy->update("bc", 2);
  copy->final(result);
  ZuCheck(!memcmp(result, expected, size));
  delete copy;
  delete hash;
}

static void hashes()
{
  ZuTestScope(hashes);
  ZuTestCall(digest, opensslSha256, EVP_sha256());
  ZuTestCall(digest, opensslSha384, EVP_sha384());
  ZuTestCall(digest, opensslSha512, EVP_sha512());
  // RFC 4231 test case 1, including a reset before repeating the message.
  const uint8_t expected[] = {0xb0, 0x34, 0x4c, 0x61, 0xd8, 0xdb, 0x38, 0x53, 0x5c, 0xa8, 0xaf,
    0xce, 0xaf, 0x0b, 0xf1, 0x2b, 0x88, 0x1d, 0xc2, 0x00, 0xc9, 0x83, 0x3d, 0xa7, 0x26, 0xe9,
    0x37, 0x6c, 0x2e, 0x32, 0xcf, 0xf7};
  uint8_t	key[20], result[sizeof(expected)];
  memset(key, 0x0b, sizeof(key));
  auto hmac = hmacCreate(&opensslSha256, key, sizeof(key));
  ZuCheck(hmac && !hmac->clone());
  memset(result, 0x5a, sizeof(result));
  ZuCheck(hmac->final(result, HashFinalMode::Snapshot) == Ztls_ErrorNotAvailable);
  ZuCheck(result[0] == 0x5a && result[sizeof(result) - 1] == 0x5a);
  hmac->update("Hi There", 8);
  hmac->final(result, HashFinalMode::Reset);
  ZuCheck(!memcmp(result, expected, sizeof(expected)));
  hmac->update("Hi There", 8);
  hmac->final(result);
  ZuCheck(!memcmp(result, expected, sizeof(expected)));
  delete hmac;
}

struct OtherBase {
  uint64_t	identity = 0x1234;
  virtual ~OtherBase() = default;
  virtual uint64_t id() const { return identity; }
};

struct ReleaseObservation {
  unsigned	releases = 0;
  bool		cleared = false;
};

struct ObservedCipher : ZmNHeap<ObservedCipher>, OtherBase, CipherContext {
friend inline ZuStringT<"Ztls.Test.Object"> ZmNHeapID(ObservedCipher *);
  CipherContext		*native;
  ReleaseObservation	*observer;
  uint8_t		secret[Ztls_AES128KeySize];

  ObservedCipher(
    ReleaseObservation *observer_, const void *key,
    const CipherAlgo *algorithm = &opensslAes128ecb)
  :
    CipherContext{algorithm}, native{algo->create(1, key)}, observer{observer_}
  { memcpy(secret, key, sizeof(secret)); }
  ~ObservedCipher() override
  {
    delete native;
    ZuClear(secret, sizeof(secret));
    observer->cleared = true;
    for (auto byte : secret) observer->cleared &= !byte;
    ++observer->releases;
  }
  void init(const void *iv) override { native->init(iv); }
  void transform(void *out, const void *in, size_t size) override
  { native->transform(out, in, size); }
};

static void ciphers()
{
  ZuTestScope(ciphers);
  // AES-128 FIPS 197, Appendix C.1.
  const uint8_t key[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
    0x0c, 0x0d, 0x0e, 0x0f};
  const uint8_t plaintext[] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa,
    0xbb, 0xcc, 0xdd, 0xee, 0xff};
  const uint8_t ciphertext[] = {0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30, 0xd8, 0xcd, 0xb7,
    0x80, 0x70, 0xb4, 0xc5, 0x5a};
  using Cache = ZmHeapCacheT<
    ZmNHeapID_<ObservedCipher>, ZmHeapAllocSize<sizeof(ObservedCipher)>{},
    alignof(ObservedCipher), false>;
  auto		     cache = Cache::instance()->cache();
  auto		     frees = uint64_t(cache->stats().frees) + uint64_t(cache->stats().heapFrees);
  ReleaseObservation observation;
  auto		     concrete = new ObservedCipher{&observation, key};
  CipherContext	    *cipher = concrete;
  ZuCheck(static_cast<void *>(cipher) != static_cast<void *>(concrete));
  uint8_t result[sizeof(plaintext)];
  cipher->transform(result, plaintext, sizeof(plaintext));
  ZuCheck(!memcmp(result, ciphertext, sizeof(ciphertext)));
  delete cipher;
  ZuCheck(observation.releases == 1 && observation.cleared);
  ZuCheck(uint64_t(cache->stats().frees) + uint64_t(cache->stats().heapFrees) == frees + 1);
  cipher = opensslAes128ecb.create(0, key);
  cipher->transform(result, ciphertext, sizeof(ciphertext));
  ZuCheck(!memcmp(result, plaintext, sizeof(plaintext)));
  delete cipher;
#if ZtlsFusion
  if (fusionSupported()) {
    cipher = fusionAes128ctr.create(1, key);
    cipher->init(plaintext);
    memset(result, 0, sizeof(result));
    cipher->transform(result, result, sizeof(result));
    ZuCheck(!memcmp(result, ciphertext, sizeof(ciphertext)));
    delete cipher;
  }
#endif
}

struct NoSerialAead : AeadContext {
  AeadContext	&native;
  explicit NoSerialAead(AeadContext &ctx) : AeadContext{ctx.algo, ctx.isEnc}, native{ctx} { }
  void getIV(void *iv) override { native.getIV(iv); }
  void setIV(const void *iv) override { native.setIV(iv); }
  void encryptV(
    void *out, const Bytes *in, size_t count, uint64_t seq, const void *aad,
    size_t aadlen) override
  { native.encryptV(out, in, count, seq, aad, aadlen); }
  size_t decrypt(
    void *out, const void *in, size_t len, uint64_t seq, const void *aad, size_t aadlen) override
  { return native.decrypt(out, in, len, seq, aad, aadlen); }
};

static void aead(const AeadAlgo &algo)
{
  ZuTestScope(aead);
  // NIST GCM, all-zero AES-128 key, IV and one-block plaintext.
  const uint8_t expected[] = {0x03, 0x88, 0xda, 0xce, 0x60, 0xb6, 0xa3, 0x92, 0xf3, 0x28, 0xc2,
    0xb9, 0x71, 0xb2, 0xfe, 0x78, 0xab, 0x6e, 0x47, 0xd4, 0x2c, 0xec, 0x13, 0xbd, 0xf5, 0x3a,
    0x67, 0xb2, 0x12, 0x57, 0xbd, 0xdf};
  uint8_t	key[16]{}, iv[12]{}, input[16]{}, output[32], plaintext[16];
  auto		enc = algo.create(true, key, iv);
  auto		dec = algo.create(false, key, iv);
  ZuCheck(enc && dec);
  enc->encrypt(output, input, sizeof(input), 0, nullptr, 0, nullptr);
  ZuCheck(!memcmp(output, expected, sizeof(expected)));
  ZuCheck(dec->decrypt(plaintext, output, sizeof(output), 0, nullptr, 0) == sizeof(plaintext));
  ZuCheck(!memcmp(input, plaintext, sizeof(input)));
  output[sizeof(output) - 1] ^= 1;
  ZuCheck(dec->decrypt(plaintext, output, sizeof(output), 0, nullptr, 0) == SIZE_MAX);
  size_t       written = 99;
  NoSerialAead optional{*enc};
  memset(output, 0x5a, sizeof(output));
  ZuCheck(!optional.serialEncrypt() && !optional.serialDecrypt());
  ZuCheck(optional.encChunk(output, input, sizeof(input), &written) == Ztls_ErrorNotAvailable);
  ZuCheck(!written && output[0] == 0x5a && output[sizeof(output) - 1] == 0x5a);
  ZuCheck(enc->decBegin(0, nullptr, 0) == Ztls_ErrorNotAvailable);
  ZuCheck(dec->encBegin(0, nullptr, 0) == Ztls_ErrorNotAvailable);
  delete enc;
  delete dec;
}

#if ZtlsFusion
static void supplement(const AeadAlgo &aeadAlgo, const CipherAlgo &hpAlgo, bool wrapped)
{
  ZuTestScope(supplement);
  uint8_t key[Ztls_AES256KeySize]{}, iv[Ztls_AESGCMIVSize]{};
  uint8_t input[32]{}, output[48], expected[48], mask[16]{}, zero[16]{};
  enum { SampleOffset = 24 }; // Eight ciphertext bytes followed by eight tag bytes.
  auto reference = (aeadAlgo.keySize == Ztls_AES128KeySize ? opensslAes128gcm : opensslAes256gcm)
		     .create(true, key, iv);
  reference->encrypt(expected, input, sizeof(input), 7, nullptr, 0, nullptr);
  delete reference;
  auto referenceHP = hpAlgo.create(true, key);
  referenceHP->init(expected + SampleOffset);
  referenceHP->transform(mask, zero, sizeof(zero));
  delete referenceHP;
  ReleaseObservation observation;
  CipherContext	    *hp = wrapped ? new ObservedCipher{&observation, key, &hpAlgo} :
				    hpAlgo.create(true, key);
  AeadSupplement     extra{hp, output + SampleOffset, {}};
  auto		     aead = aeadAlgo.create(true, key, iv);
  aead->encrypt(output, input, sizeof(input), 7, nullptr, 0, &extra);
  ZuCheck(!memcmp(output, expected, sizeof(output)));
  ZuCheck(!memcmp(extra.output, mask, sizeof(mask)));
  delete aead;
  delete hp;
  ZuCheck(!wrapped || (observation.releases == 1 && observation.cleared));
}
#endif

static void aeads()
{
  ZuTestScope(aeads);
  ZuTestCall(aead, opensslAes128gcm);
#if ZtlsFusion
  if (fusionSupported()) {
    ZuTestCall(aead, fusionAes128gcm);
    ZuTestCall(supplement, fusionAes128gcm, *opensslAes128gcm.ctrCipher, false);
    ZuTestCall(supplement, fusionAes128gcm, *opensslAes128gcm.ctrCipher, true);
    ZuTestCall(supplement, fusionAes128gcm, *fusionAes128gcm.ctrCipher, false);
    ZuTestCall(supplement, fusionAes256gcm, *fusionAes128gcm.ctrCipher, false);
    int wide = fusionCanAesni256;
    fusionCanAesni256 = 0;
    ZuTestCall(aead, nonTemporalAes128gcm);
    if (wide) {
      fusionCanAesni256 = 1;
      ZuTestCall(aead, nonTemporalAes128gcm);
    }
    fusionCanAesni256 = wide;
  }
#endif
}

static void keyExchange(const KeyExchangeAlgo &algo)
{
  ZuTestScope(keyExchange);
  KeyExchangeContext *ctx = nullptr;
  KeyExchangeBytes    publicKey, first, second;
  ZuCheck(!algo.create(&ctx) && ctx);
  ZuCheck(!algo.exchange(&publicKey, &first, ctx->pubkey));
  ZuCheck(!ctx->exchange(&second, {publicKey.data(), publicKey.length()}));
  ZuCheck(first.length() && first.length() == second.length());
  ZuCheck(!memcmp(first.data(), second.data(), first.length()));
  ZuCheck(ctx->exchange(&second, {}) != 0);
  delete ctx;
}

static void keyExchanges()
{
  ZuTestScope(keyExchanges);
  for (auto algo = opensslKeyExchangesAll; *algo; ++algo) ZuTestCallRT(keyExchange, **algo);
}

static void creationFailure()
{
  ZuTestScope(creationFailure);
  uint8_t key[Ztls_AES128KeySize]{};
  // Native provider failure must release the already constructed context.
  ZuCheck(EVP_set_default_properties(nullptr, "provider=ztls-unavailable") == 1);
  ZuCheck(!opensslAes128ecb.create(1, key));
  uint8_t iv[12]{};
  ZuCheck(!opensslAes128gcm.create(true, key, iv));
  ZuCheck(EVP_set_default_properties(nullptr, nullptr) == 1);
}

static void suiteIndexes()
{
  ZuTestScope(suiteIndexes);
  CipherSuites suites;
  suites.push(&opensslAes128gcmsha256);
  suites.push(&opensslAes256gcmsha384);
  suites.push(&opensslAes128gcmsha256);
  ZuCheck(suites.index(Ztls_SuiteAes128gcmSHA256) == 0);
  ZuCheck(suites.find(Ztls_SuiteAes256gcmSHA384) == &opensslAes256gcmsha384);
  ZuCheck(!suites.find(0xffff));
  ZuCheck(suites.first(&opensslSha256) == &opensslAes128gcmsha256);
  ZuCheck(!suites.first(&opensslSha512));
  ZuCheck(suites.hashes().length() == 2 && suites.serialRx() && suites.serialTx());
  CipherSuites copy{suites};
  suites.clear();
  ZuCheck(!suites.length() && !suites.find(Ztls_SuiteAes128gcmSHA256));
  const CipherSuite *const order[] = {&opensslAes256gcmsha384, &opensslAes128gcmsha256};
  copy = SuiteSpan{order};
  CipherSuites moved{ZuMv(copy)};
  ZuCheck(!copy.length() && moved.index(Ztls_SuiteAes128gcmSHA256) == 1);
  ZuCheck(moved.first(&opensslSha384) == &opensslAes256gcmsha384);
  ZuCheck(opensslHPKECipherSuites.find(
    {Ztls_HPKEHKDFSHA256, Ztls_HPKEAEADAES128GCM}) == &opensslHPKEAes128gcmsha256);
  ZuCheck(!opensslHPKECipherSuites.find({0xffff, Ztls_HPKEAEADAES128GCM}));
  enum { Capacity = 1U << ZtlsSuiteBits };
  ZtArray<CipherSuite, ZtArrayHeapID<"Ztls.Test.Suites">> extra(Capacity + 1, Capacity + 1);
  CipherSuites full;
  for (unsigned i = 0; i <= Capacity; ++i) {
    extra[i] = opensslAes128gcmsha256;
    extra[i].id = uint16_t(0x2000 + i);
    if (i < Capacity) full.push(&extra[i]);
  }
  bool rejected = false;
  try { full.push(&extra[Capacity]); }
  catch (const ZeException &) { rejected = true; }
  ZuCheck(rejected && full.length() == Capacity && !full.find(extra[Capacity].id));
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(hashes);
  ZuTestCall(ciphers);
  ZuTestCall(aeads);
  ZuTestCall(keyExchanges);
#ifndef _WIN32
  ZuTestCall(ecInitFailure);
#endif
  ZuTestCall(creationFailure);
  ZuTestCall(suiteIndexes);
  return 0;
}
