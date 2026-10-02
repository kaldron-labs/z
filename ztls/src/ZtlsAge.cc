//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdint.h>
#include <string.h>

#include <zlib/ZuBase64.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuBox.hh>

#include <zlib/ZmScratch.hh>
#include <zlib/ZmVHeap.hh>

#include <zlib/ZtScratch.hh>

#include <zlib/ZiIOBuf.hh>

#include <zlib/ZtlsAge.hh>
#include <zlib/ZtlsBackend.hh>
#include <zlib/ZtlsHMAC.hh>
#include <zlib/ZtlsHPKE.hh>
#include <zlib/ZtlsKEM.hh>
#include <zlib/ZtlsMD.hh>
#include <zlib/ZtlsPK.hh>
#include <zlib/ZtlsPico.hh>
#include <zlib/ZtlsSec.hh>

namespace Ztls_ {

ZtEnumImplStruct(AgeKeyType);

enum { X25519KeySize = 32, ChecksumSize = 6 };

static constexpr auto PubX25519 = "age"_z;
static constexpr auto PubHybrid = "age1pq"_z;
static constexpr auto SecX25519 = "age-secret-key-"_z;
static constexpr auto SecHybrid = "age-secret-key-pq-"_z;
static constexpr auto Alphabet = "qpzry9x8gf2tvdw0s3jn54khce6mua7l"_z;

static uint32_t step_(uint32_t sum, unsigned value)
{
  unsigned top = sum >> 25;
  sum = ((sum & 0x1ffffff) << 5) ^ value;
  if (top & 1) sum ^= 0x3b6a57b2;
  if (top & 2) sum ^= 0x26508e6d;
  if (top & 4) sum ^= 0x1ea119fa;
  if (top & 8) sum ^= 0x3d4233dd;
  if (top & 16) sum ^= 0x2a1462b3;
  return sum;
}

static char lower_(char c)
{
  return c >= 'A' && c <= 'Z' ? char(c + ('a' - 'A')) : c;
}

static char upper_(char c)
{
  return c >= 'a' && c <= 'z' ? char(c - ('a' - 'A')) : c;
}

static uint32_t hrpSum_(ZuCSpan hrp)
{
  uint32_t sum = 1;
  unsigned n = hrp.length();
  for (unsigned i = 0; i < n; ++i)
    sum = step_(sum, unsigned(lower_(hrp[i])) >> 5);
  sum = step_(sum, 0);
  for (unsigned i = 0; i < n; ++i)
    sum = step_(sum, unsigned(lower_(hrp[i])) & 31);
  return sum;
}

static int digit_(char c)
{
  c = lower_(c);
  auto p = static_cast<const char *>(memchr(Alphabet.data(), c,
    Alphabet.length()));
  return p ? int(p - Alphabet.data()) : -1;
}

static ZuUnion<size_t, ZeException> encode_(
  ZuCSpan hrp, bool upper, ZuBSpan key, ZuSpan<char> output)
{
  size_t groups = (key.length() * 8U + 4U) / 5U;
  size_t need = hrp.length() + 1U + groups + ChecksumSize;
  if (output.length() < need)
    return ZeEXCEPT(Error, "ZtlsAge", "insufficient key-text capacity");
  char *out = output.data();
  for (unsigned i = 0, n = hrp.length(); i < n; ++i)
    *out++ = upper ? upper_(hrp[i]) : hrp[i];
  *out++ = '1';
  uint32_t sum = hrpSum_(hrp);
  unsigned acc = 0, bits = 0;
  for (unsigned i = 0, n = key.length(); i < n; ++i) {
    acc = (acc << 8) | key[i];
    bits += 8;
    while (bits >= 5) {
      bits -= 5;
      unsigned v = (acc >> bits) & 31;
      sum = step_(sum, v);
      char c = Alphabet[v];
      *out++ = upper ? upper_(c) : c;
    }
  }
  if (bits) {
    unsigned v = (acc << (5 - bits)) & 31;
    sum = step_(sum, v);
    char c = Alphabet[v];
    *out++ = upper ? upper_(c) : c;
  }
  for (unsigned i = 0; i < ChecksumSize; ++i) sum = step_(sum, 0);
  sum ^= 1;
  for (int i = ChecksumSize - 1; i >= 0; --i) {
    char c = Alphabet[(sum >> (i * 5)) & 31];
    *out++ = upper ? upper_(c) : c;
  }
  return need;
}

static ZuUnion<size_t, ZeException> decode_(
  ZuCSpan hrp, ZuCSpan text, ZuSpan<uint8_t> output)
{
  size_t h = hrp.length();
  if (text.length() < h + 1U + ChecksumSize ||
      text[h] != '1')
    return ZeEXCEPT(Error, "ZtlsAge", "invalid Bech32 key text");
  bool upper = false, lower = false;
  for (unsigned i = 0, n = text.length(); i < n; ++i) {
    char c = text[i];
    if (c >= 'A' && c <= 'Z') upper = true;
    if (c >= 'a' && c <= 'z') lower = true;
  }
  if (upper && lower)
    return ZeEXCEPT(Error, "ZtlsAge", "mixed-case Bech32 key text");
  for (unsigned i = 0; i < h; ++i)
    if (lower_(text[i]) != lower_(hrp[i]))
      return ZeEXCEPT(Error, "ZtlsAge", "unexpected Bech32 key type");
  uint32_t sum = hrpSum_(hrp);
  unsigned acc = 0, bits = 0;
  size_t out = 0;
  size_t end = text.length() - ChecksumSize;
  for (size_t i = h + 1; i < text.length(); ++i) {
    int v = digit_(text[i]);
    if (v < 0) {
      ZuClear(output.data(), out);
      return ZeEXCEPT(Error, "ZtlsAge", "invalid Bech32 character");
    }
    sum = step_(sum, unsigned(v));
    if (i >= end) continue;
    acc = (acc << 5) | unsigned(v);
    bits += 5;
    if (bits >= 8) {
      bits -= 8;
      if (out == output.length()) {
        ZuClear(output.data(), out);
        return ZeEXCEPT(Error, "ZtlsAge", "insufficient key capacity");
      }
      output[out++] = uint8_t(acc >> bits);
    }
  }
  if (sum != 1 || bits >= 5 || (acc & ((1U << bits) - 1U))) {
    ZuClear(output.data(), out);
    return ZeEXCEPT(Error, "ZtlsAge", "non-canonical Bech32 key text");
  }
  return out;
}

static ZuCSpan hrp_(Age::KeyType type, bool identity)
{
  switch (type) {
    case AgeKeyType::X25519:
      return identity ? ZuCSpan{SecX25519} : ZuCSpan{PubX25519};
    case AgeKeyType::Hybrid:
      return identity ? ZuCSpan{SecHybrid} : ZuCSpan{PubHybrid};
  }
  return {};
}

static unsigned keySize_(Age::KeyType type, bool identity)
{
  switch (type) {
    case AgeKeyType::X25519: return X25519KeySize;
    case AgeKeyType::Hybrid:
      return identity ? Ztls::PK::HybridSeedSize :
        Ztls::PK::HybridPublicSize;
  }
  return 0;
}

ZuUnion<size_t, ZeException> Age::encodeRecipient(
  KeyType type, ZuBSpan key, ZuSpan<char> output)
{
  if (key.length() != keySize_(type, false))
    return ZeEXCEPT(Error, "ZtlsAge", "invalid recipient key length");
  return encode_(hrp_(type, false), false, key, output);
}

ZuUnion<size_t, ZeException> Age::decodeRecipient(
  KeyType type, ZuCSpan text, ZuSpan<uint8_t> output)
{
  if (output.length() < keySize_(type, false))
    return ZeEXCEPT(Error, "ZtlsAge", "insufficient recipient key capacity");
  auto r = decode_(hrp_(type, false), text, output);
  if (r.template is<ZeException>())
    return ZuMv(r).template p<ZeException>();
  if (r.template p<size_t>() != keySize_(type, false)) {
    ZuClear(output.data(), r.template p<size_t>());
    return ZeEXCEPT(Error, "ZtlsAge", "invalid recipient key length");
  }
  return r;
}

ZuUnion<size_t, ZeException> Age::encodeIdentity(
  KeyType type, ZuBSpan key, ZuSpan<char> output)
{
  if (key.length() != keySize_(type, true))
    return ZeEXCEPT(Error, "ZtlsAge", "invalid identity key length");
  return encode_(hrp_(type, true), true, key, output);
}

ZuUnion<size_t, ZeException> Age::decodeIdentity(
  KeyType type, ZuCSpan text, ZuSpan<uint8_t> output)
{
  if (output.length() < keySize_(type, true))
    return ZeEXCEPT(Error, "ZtlsAge", "insufficient identity key capacity");
  auto r = decode_(hrp_(type, true), text, output);
  if (r.template is<ZeException>())
    return ZuMv(r).template p<ZeException>();
  if (r.template p<size_t>() != keySize_(type, true)) {
    ZuClear(output.data(), r.template p<size_t>());
    return ZeEXCEPT(Error, "ZtlsAge", "invalid identity key length");
  }
  return r;
}

using AgeScratchHeap = ZmVHeap<"Ztls.Age.Scratch">;

enum {
  FileKeySize = 16,
  SaltSize = 16,
  PayloadNonceSize = 16,
  KeySize = 32,
  TagSize = 16,
  ChunkSize = 64 << 10,
  ChunkCipherSize = ChunkSize + TagSize,
  HeaderStackSize = 4096, // common headers stay on the stack
  HeaderMaxSize = 1 << 20, // resource policy for untrusted input
  RecipientMax = 256, // cap work when scanning large mixed headers
  ReadBufferSize = 4096, // amortize file reads while parsing lines
  ScryptLogN = 18,
  ScryptMaxLogN = 18,
  ScryptMaxMemory = 512 << 20
};
static constexpr auto Version = "age-encryption.org/v1\n"_z;
static constexpr auto ScryptLabel = "age-encryption.org/v1/scrypt"_z;
static constexpr auto ScryptArg = "-> scrypt "_z;
static constexpr auto X25519Arg = "-> X25519 "_z;
static constexpr auto HybridArg = "-> mlkem768x25519 "_z;
static constexpr auto SshRSAArg = "-> ssh-rsa "_z;
static constexpr auto SshRSALabel = "age-encryption.org/v1/ssh-rsa"_z;
static constexpr auto SshEDArg = "-> ssh-ed25519 "_z;
static constexpr auto SshEDLabel = "age-encryption.org/v1/ssh-ed25519"_z;
static constexpr auto X25519Info = "age-encryption.org/v1/X25519"_z;
static constexpr auto HybridInfo = "age-encryption.org/mlkem768x25519"_z;
static constexpr auto HeaderInfo = "header"_z;
static constexpr auto PayloadInfo = "payload"_z;
ZuDerive(AgeHeaderScratch, (ZtBArray<ZtArrayHeapID<"Ztls.Age.Header",
  ZtArraySharded<true>>>));

static bool unb64_(ZuCSpan text, ZuSpan<uint8_t> output)
{
  unsigned expected = output.length();
  if (text.length() != ZuBase64::enclen<false>(expected)) return false;
  for (unsigned i = 0, n = text.length(); i < n; ++i)
    if (ZuBase64::lookup(text[i]) >= 64) return false;
  unsigned tail = expected % 3;
  if (tail && (ZuBase64::lookup(text[text.length() - 1]) &
      (tail == 1 ? 15 : 3))) return false;
  return ZuBase64::decode(output, text) == expected;
}

static bool sshString_(ZuBSpan blob, size_t &cursor, ZuBSpan &field)
{
  if (blob.length() - cursor < 4) return false;
  auto p = blob.data() + cursor;
  size_t n = (size_t(p[0]) << 24) | (size_t(p[1]) << 16) |
    (size_t(p[2]) << 8) | p[3];
  cursor += 4;
  if (n > blob.length() - cursor) return false;
  field = {blob.data() + cursor, n};
  cursor += n;
  return true;
}

static bool sshU32_(ZuBSpan blob, size_t &cursor, uint32_t &value)
{
  if (blob.length() - cursor < 4) return false;
  auto p = blob.data() + cursor;
  value = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
    (uint32_t(p[2]) << 8) | p[3];
  cursor += 4;
  return true;
}

static bool sshRSA_(ZuBSpan blob, Ztls::PK::Data::PK_PKCS1 &data)
{
  size_t cursor = 0;
  ZuBSpan type;
  if (!sshString_(blob, cursor, type) || type != ZuBSpan{"ssh-rsa"} ||
      !sshString_(blob, cursor, data.pubExp) ||
      !sshString_(blob, cursor, data.modulus)) return false;
  return cursor == blob.length() && data.pubExp && data.modulus;
}

static bool sshED_(ZuBSpan blob, ZuBSpan &publicKey)
{
  size_t cursor = 0;
  ZuBSpan type;
  return sshString_(blob, cursor, type) &&
    type == ZuBSpan{"ssh-ed25519"} &&
    sshString_(blob, cursor, publicKey) &&
    publicKey.length() == X25519KeySize && cursor == blob.length();
}

static void sshFingerprint_(ZuBSpan blob, ZuSpan<uint8_t> output)
{
  uint8_t digest[Ztls::MD<Ztls::SHA256>::Size];
  Ztls::MD<Ztls::SHA256> md;
  md.update(blob);
  md.finish(digest);
  ZuBase64::encode<false>(output, {digest, 4});
}

ZuUnion<Age::Recipient, ZeException> Age::decodeSSHRecipient(
  ZuCSpan authorizedKey, ZuSpan<uint8_t> output)
{
  size_t cursor = 0, n = authorizedKey.length();
  while (cursor < n) {
    while (cursor < n && authorizedKey[cursor] <= ' ') ++cursor;
    size_t start = cursor;
    while (cursor < n && authorizedKey[cursor] > ' ') ++cursor;
    ZuCSpan type{authorizedKey.data() + start, cursor - start};
    bool rsa = type == "ssh-rsa"_z;
    bool ed = type == "ssh-ed25519"_z;
    if (!rsa && !ed) continue;
    while (cursor < n && authorizedKey[cursor] <= ' ') ++cursor;
    start = cursor;
    while (cursor < n && authorizedKey[cursor] > ' ') ++cursor;
    ZuCSpan encoded{authorizedKey.data() + start, cursor - start};
    if (!encoded) break;
    unsigned padding = encoded[encoded.length() - 1] == '=';
    if (padding && encoded.length() > 1 &&
        encoded[encoded.length() - 2] == '=') ++padding;
    if (padding && encoded.length() % 4) break;
    unsigned textLength = encoded.length() - padding;
    for (unsigned i = 0; i < textLength; ++i)
      if (ZuBase64::lookup(encoded[i]) >= 64) return
        ZeEXCEPT(Error, "ZtlsAge", "invalid SSH public key");
    unsigned expected = ZuBase64::declen(textLength);
    if (expected > output.length()) break;
    if (!unb64_({encoded.data(), textLength},
        {output.data(), expected})) break;
    ZuBSpan blob{output.data(), expected};
    if (rsa) {
      Ztls::PK::Data::PK_PKCS1 data;
      if (!sshRSA_(blob, data)) break;
      return Recipient{SshRSARecipient{blob}};
    }
    ZuBSpan edPublic;
    if (!sshED_(blob, edPublic)) break;
    return Recipient{SshED25519Recipient{blob}};
  }
  return ZeEXCEPT(Error, "ZtlsAge", "invalid SSH public key");
}

static ZeException invalidOpenSSH_()
{
  return ZeEXCEPT(Error, "ZtlsAge", "invalid OpenSSH private key");
}

ZuUnion<Age::SshKey, ZeException> Age::loadOpenSSH(
  ZuSpan<char> privateKey, ZuSpan<uint8_t> publicKey)
{
  ZuGuard clear{[&privateKey]() {
    ZuClear(privateKey);
  }};
  if (privateKey.length() > HeaderMaxSize) return invalidOpenSSH_();
  constexpr auto begin = "-----BEGIN OPENSSH PRIVATE KEY-----"_z;
  constexpr auto end = "-----END OPENSSH PRIVATE KEY-----"_z;
  size_t start = 0, finish = 0, n = privateKey.length();
  while (start + begin.length() <= n &&
      memcmp(privateKey.data() + start, begin.data(), begin.length()))
    ++start;
  if (start + begin.length() > n) return invalidOpenSSH_();
  start += begin.length();
  finish = start;
  while (finish + end.length() <= n &&
      memcmp(privateKey.data() + finish, end.data(), end.length()))
    ++finish;
  if (finish + end.length() > n) return invalidOpenSSH_();
  unsigned encodedLength = 0;
  for (size_t i = start; i < finish; ++i) {
    auto c = privateKey[i];
    if (c == '\n' || c == '\r') continue;
    privateKey[start + encodedLength++] = c;
  }
  if (!encodedLength || encodedLength % 4) return invalidOpenSSH_();
  auto encoded = ZuBSpan{privateKey.data() + start, encodedLength};
  unsigned padding = encoded[encodedLength - 1] == '=';
  if (padding && encoded[encodedLength - 2] == '=') ++padding;
  for (unsigned i = 0, l = encodedLength - padding; i < l; ++i)
    if (ZuBase64::lookup(encoded[i]) >= 64) return invalidOpenSSH_();
  if ((padding == 2 &&
        (ZuBase64::lookup(encoded[encodedLength - 3]) & 15)) ||
      (padding == 1 &&
        (ZuBase64::lookup(encoded[encodedLength - 2]) & 3)))
    return invalidOpenSSH_();
  unsigned decodedLength = ZuBase64::decode(
    {privateKey.data() + start, encodedLength}, encoded);
  if (decodedLength != ZuBase64::declen(encodedLength) - padding)
    return invalidOpenSSH_();
  ZuBSpan data{privateKey.data() + start, decodedLength};
  constexpr char magic[] = "openssh-key-v1";
  if (data.length() < sizeof(magic) ||
      memcmp(data.data(), magic, sizeof(magic))) return invalidOpenSSH_();
  size_t cursor = sizeof(magic);
  ZuBSpan cipher, kdf, options, blob, privateBlock;
  uint32_t count;
  if (!sshString_(data, cursor, cipher) ||
      !sshString_(data, cursor, kdf) ||
      !sshString_(data, cursor, options) ||
      !sshU32_(data, cursor, count) || count != 1 ||
      !sshString_(data, cursor, blob) ||
      !sshString_(data, cursor, privateBlock) ||
      cursor != data.length()) return invalidOpenSSH_();
  if (cipher != ZuBSpan{"none"} || kdf != ZuBSpan{"none"} || options)
    return ZeEXCEPT(Error, "ZtlsAge", "encrypted OpenSSH key unsupported");
  cursor = 0;
  uint32_t check1, check2;
  ZuBSpan type, comment;
  if (!sshU32_(privateBlock, cursor, check1) ||
      !sshU32_(privateBlock, cursor, check2) || check1 != check2 ||
      !sshString_(privateBlock, cursor, type)) return invalidOpenSSH_();
  ZmRef<Ztls::PK::AnyKey> key;
  try {
    if (type == ZuBSpan{"ssh-ed25519"}) {
      ZuBSpan pub, secret, envelopePub;
      if (!sshString_(privateBlock, cursor, pub) || pub.length() != 32 ||
          !sshString_(privateBlock, cursor, secret) ||
          secret.length() != 64 ||
          memcmp(secret.data() + 32, pub.data(), 32) ||
          !sshED_(blob, envelopePub) || envelopePub != pub)
        return invalidOpenSSH_();
      auto *ed = new Ztls::PK::SK_ED25519{{secret.data(), 32}};
      key = ed;
      uint8_t derived[32];
      if (ed->exportPK(derived).template is<ZeException>() ||
          memcmp(derived, pub.data(), 32)) return invalidOpenSSH_();
    } else if (type == ZuBSpan{"ssh-rsa"}) {
      ZuBSpan modulus, pubExp, prvExp, coeff, prime1, prime2;
      Ztls::PK::Data::PK_PKCS1 envelope;
      if (!sshString_(privateBlock, cursor, modulus) ||
          !sshString_(privateBlock, cursor, pubExp) ||
          !sshString_(privateBlock, cursor, prvExp) ||
          !sshString_(privateBlock, cursor, coeff) ||
          !sshString_(privateBlock, cursor, prime1) ||
          !sshString_(privateBlock, cursor, prime2) ||
          !sshRSA_(blob, envelope) ||
          envelope.modulus != modulus || envelope.pubExp != pubExp)
        return invalidOpenSSH_();
      key = new Ztls::PK::SK_RSA{
        modulus, pubExp, prvExp, coeff, prime1, prime2};
    } else return invalidOpenSSH_();
  } catch (const ZeException &e) {
    return e;
  }
  if (!sshString_(privateBlock, cursor, comment)) return invalidOpenSSH_();
  for (unsigned pad = 1; cursor < privateBlock.length(); ++pad, ++cursor)
    if (privateBlock[cursor] != pad) return invalidOpenSSH_();
  if (privateBlock.length() % 8 || blob.length() > publicKey.length())
    return invalidOpenSSH_();
  memcpy(publicKey.data(), blob.data(), blob.length());
  return SshKey{ZuMv(key), {publicKey.data(), blob.length()}};
}

static bool hkdf_(
  ZuBSpan input, ZuBSpan salt, ZuCSpan info, ZuSpan<uint8_t> output)
{
  uint8_t prk[KeySize];
  ZuGuard clear{[&prk]() { ZuClear(prk, sizeof(prk)); }};
  auto hash = Ztls::Backend::hash_algorithm(Ztls::SHA256);
  return !ptls_hkdf_extract(hash, prk,
      ptls_iovec_init(salt.data(), salt.length()),
      ptls_iovec_init(input.data(), input.length())) &&
    !ptls_hkdf_expand(hash, output.data(), output.length(),
      ptls_iovec_init(prk, sizeof(prk)),
      ptls_iovec_init(info.data(), info.length()));
}

static bool write_(ZiFile &file, ZuBSpan data)
{
  return file.write(data.data(), data.length()) == Zi::OK;
}

static bool scryptKey_(
  ZuBSpan passphrase, ZuBSpan salt, unsigned logN,
  ZuSpan<uint8_t> output)
{
  uint8_t fullSalt[ScryptLabel.length() + SaltSize];
  ZuGuard clear{[&fullSalt]() { ZuClear(fullSalt, sizeof(fullSalt)); }};
  memcpy(fullSalt, ScryptLabel.data(), ScryptLabel.length());
  memcpy(fullSalt + ScryptLabel.length(), salt.data(), SaltSize);
  return Ztls::Backend::scrypt(passphrase, fullSalt,
    uint64_t{1} << logN, 8, 1, ScryptMaxMemory, output);
}

static bool sealKey_(
  ZuBSpan wrappingKey, ZuBSpan fileKey, ZuSpan<uint8_t> body)
{
  uint8_t nonce[12]{};
  Ztls::Pico::AeadCtx ctx;
  if (!ctx.init(Ztls::Backend::chacha20poly1305(), true,
      wrappingKey.data(), nonce)) return false;
  return ptls_aead_encrypt(ctx.get(), body.data(), fileKey.data(),
    fileKey.length(), 0, nullptr, 0) == fileKey.length() + TagSize;
}

static bool openKey_(
  ZuBSpan wrappingKey, ZuBSpan body, ZuSpan<uint8_t> fileKey)
{
  uint8_t nonce[12]{};
  Ztls::Pico::AeadCtx ctx;
  if (!ctx.init(Ztls::Backend::chacha20poly1305(), false,
      wrappingKey.data(), nonce)) return false;
  return ptls_aead_decrypt(ctx.get(), fileKey.data(), body.data(),
    body.length(), 0, nullptr, 0) == FileKeySize;
}

static ZuUnion<void, ZeException> writePayload_(
  Ztls::Random &rng, ZiFile &file, ZuBSpan fileKey, ZuBSpan plaintext)
{
  uint8_t nonce[PayloadNonceSize], key[KeySize];
  ZuGuard clear{[&nonce, &key]() {
    ZuClear(nonce, sizeof(nonce));
    ZuClear(key, sizeof(key));
  }};
  if (!rng.random(nonce) || !hkdf_(fileKey, nonce, PayloadInfo, key) ||
      !write_(file, nonce))
    return ZeEXCEPT(Error, "ZtlsAge", "payload setup failed");
  uint8_t baseIV[12]{};
  Ztls::Pico::AeadCtx ctx;
  if (!ctx.init(Ztls::Backend::chacha20poly1305(), true, key, baseIV))
    return ZeEXCEPT(Error, "ZtlsAge", "payload cipher setup failed");
  ZmRef<ZiIOBuf> chunk = new ZiIOBufAlloc<ChunkCipherSize,
    ChunkCipherSize, "Ztls.Age.Chunk">;
  size_t offset = 0;
  uint64_t counter = 0;
  do {
    size_t remaining = plaintext.length() - offset;
    size_t n = remaining < ChunkSize ? remaining : ChunkSize;
    bool final = remaining <= ChunkSize;
    const uint8_t *input = n ? plaintext.data() + offset : chunk->data();
    size_t written = ptls_aead_encrypt(ctx.get(), chunk->data(), input,
      n, (counter << 8) | unsigned(final), nullptr, 0);
    if (written != n + TagSize ||
        !write_(file, ZuBSpan{chunk->data(), written}))
      return ZeEXCEPT(Error, "ZtlsAge", "payload write failed");
    offset += n;
    ++counter;
  } while (offset < plaintext.length());
  return {};
}

class AgeReader {
public:
  AgeReader(ZiFile &file, ZuSpan<uint8_t> buffer) :
    m_file{file}, m_buffer{buffer} { }

  // 1: byte available; 0: EOF; -1: I/O error.
  int peek()
  {
    if (m_pos < m_end) return 1;
    int n = m_file.read(m_buffer.data(), m_buffer.length(), false);
    if (n == Zi::EndOfFile) return 0;
    if (n < 0) return -1;
    m_pos = 0;
    m_end = n;
    return 1;
  }

  int byte(uint8_t &c)
  {
    int r = peek();
    if (r == 1) c = m_buffer[m_pos++];
    return r;
  }

  int read(uint8_t *dst, unsigned length)
  {
    unsigned total = 0;
    while (total < length) {
      if (m_pos == m_end && length - total >= m_buffer.length()) {
        int n = m_file.read(dst + total, length - total);
        if (n == Zi::EndOfFile) return total;
        if (n < 0) return -1;
        total += n;
        continue;
      }
      int r = peek();
      if (r <= 0) return r < 0 ? -1 : int(total);
      unsigned n = m_end - m_pos;
      if (n > length - total) n = length - total;
      memcpy(dst + total, m_buffer.data() + m_pos, n);
      m_pos += n;
      total += n;
    }
    return total;
  }

private:
  ZiFile &m_file;
  ZuSpan<uint8_t> m_buffer;
  unsigned m_pos = 0;
  unsigned m_end = 0;
};

static ZuUnion<size_t, ZeException> readPayload_(
  AgeReader &reader, ZuBSpan fileKey, ZuSpan<uint8_t> plaintext)
{
  uint8_t nonce[PayloadNonceSize], key[KeySize];
  ZuGuard clearSecrets{[&nonce, &key]() {
    ZuClear(nonce, sizeof(nonce));
    ZuClear(key, sizeof(key));
  }};
  size_t produced = 0;
  ZuGuard clearOutput{[&plaintext, &produced]() {
    ZuClear(plaintext.data(), produced);
  }};
  if (reader.read(nonce, sizeof(nonce)) != sizeof(nonce) ||
      !hkdf_(fileKey, nonce, PayloadInfo, key))
    return ZeEXCEPT(Error, "ZtlsAge", "invalid payload nonce");
  uint8_t baseIV[12]{};
  Ztls::Pico::AeadCtx ctx;
  if (!ctx.init(Ztls::Backend::chacha20poly1305(), false, key, baseIV))
    return ZeEXCEPT(Error, "ZtlsAge", "payload cipher setup failed");
  ZmRef<ZiIOBuf> chunk = new ZiIOBufAlloc<ChunkCipherSize,
    ChunkCipherSize, "Ztls.Age.Chunk">;
  uint64_t counter = 0;
  for (;;) {
    int n = reader.read(chunk->data(), ChunkCipherSize);
    if (n < TagSize)
      return ZeEXCEPT(Error, "ZtlsAge", "truncated payload chunk");
    int more = n == ChunkCipherSize ? reader.peek() : 0;
    if (more < 0)
      return ZeEXCEPT(Error, "ZtlsAge", "payload read failed");
    bool final = !more;
    size_t plainLength = n - TagSize;
    if ((!plainLength && produced) ||
        plainLength > plaintext.length() - produced)
      return ZeEXCEPT(Error, "ZtlsAge", "invalid payload length");
    uint8_t *dst = plainLength ? plaintext.data() + produced : chunk->data();
    produced += plainLength;
    if (ptls_aead_decrypt(ctx.get(), dst,
        chunk->data(), n, (counter << 8) | unsigned(final),
        nullptr, 0) != plainLength)
      return ZeEXCEPT(Error, "ZtlsAge", "payload authentication failed");
    if (final) {
      size_t result = produced;
      produced = 0;
      return result;
    }
    if (++counter >= (uint64_t{1} << 56))
      return ZeEXCEPT(Error, "ZtlsAge", "payload counter exhausted");
  }
}

static ZuUnion<void, ZeException> appendScrypt_(
  AgeHeaderScratch &header, Ztls::Random &rng,
  ZuBSpan passphrase, ZuBSpan fileKey)
{
  struct {
    uint8_t salt[SaltSize];
    uint8_t wrapKey[KeySize];
    uint8_t body[FileKeySize + TagSize];
  } secrets;
  ZuGuard clear{[&secrets]() { ZuClear(&secrets, sizeof(secrets)); }};
  if (!rng.random(secrets.salt) ||
      !scryptKey_(passphrase, secrets.salt, ScryptLogN,
        secrets.wrapKey) ||
      !sealKey_(secrets.wrapKey, fileKey, secrets.body))
    return ZeEXCEPT(Error, "ZtlsAge", "scrypt stanza failed");
  ZuBArray<ZuBase64::enclen<false>(SaltSize)> salt64;
  ZuBArray<ZuBase64::enclen<false>(FileKeySize + TagSize)> body64;
  salt64.length(ZuBase64::encode<false>(
    {salt64.data(), salt64.size()}, secrets.salt));
  body64.length(ZuBase64::encode<false>(
    {body64.data(), body64.size()}, secrets.body));
  header << ScryptArg << salt64 << ' ' <<
    ZuBox<unsigned>{ScryptLogN} << '\n' << body64 << '\n';
  return {};
}

static ZuUnion<void, ZeException> appendX25519_(
  AgeHeaderScratch &header, Ztls::Random &rng,
  ZuBSpan recipient, ZuBSpan fileKey)
{
  if (recipient.length() != X25519KeySize)
    return ZeEXCEPT(Error, "ZtlsAge", "invalid X25519 recipient length");
  struct {
    uint8_t shared[KeySize];
    uint8_t salt[2 * X25519KeySize];
    uint8_t wrapKey[KeySize];
    uint8_t body[FileKeySize + TagSize];
  } data;
  ZuGuard clear{[&data]() { ZuClear(&data, sizeof(data)); }};
  Ztls::PK::SK_X25519 key{rng};
  auto r = key.exportPK({data.salt, X25519KeySize});
  if (r.template is<ZeException>())
    return ZuMv(r).template p<ZeException>();
  r = key.agree(recipient, data.shared);
  if (r.template is<ZeException>())
    return ZuMv(r).template p<ZeException>();
  memcpy(data.salt + X25519KeySize, recipient.data(), X25519KeySize);
  if (!hkdf_(data.shared, data.salt, X25519Info, data.wrapKey) ||
      !sealKey_(data.wrapKey, fileKey, data.body))
    return ZeEXCEPT(Error, "ZtlsAge", "X25519 stanza failed");
  ZuBArray<ZuBase64::enclen<false>(X25519KeySize)> ephemeral64;
  ZuBArray<ZuBase64::enclen<false>(FileKeySize + TagSize)> body64;
  ephemeral64.length(ZuBase64::encode<false>(
    {ephemeral64.data(), ephemeral64.size()},
    {data.salt, X25519KeySize}));
  body64.length(ZuBase64::encode<false>(
    {body64.data(), body64.size()}, data.body));
  header << X25519Arg << ephemeral64 << '\n' << body64 << '\n';
  return {};
}

static ZuUnion<void, ZeException> appendHybrid_(
  AgeHeaderScratch &header, Ztls::Random &rng,
  ZuBSpan recipient, ZuBSpan fileKey)
{
  if (recipient.length() != Ztls::PK::HybridPublicSize)
    return ZeEXCEPT(Error, "ZtlsAge", "invalid hybrid recipient length");
  Ztls::PK::PK_MLKEM768_X25519 key{recipient};
  uint8_t enc[Ztls::PK::HybridCiphertextSize];
  uint8_t body[FileKeySize + Ztls::HPKE::TagSize];
  auto r = Ztls::HPKE::sealBase(rng, key, HybridInfo, {},
    fileKey, enc, body);
  if (r.template is<ZeException>())
    return ZuMv(r).template p<ZeException>();
  ZuBArray<ZuBase64::enclen<false>(Ztls::PK::HybridCiphertextSize)> enc64;
  ZuBArray<ZuBase64::enclen<false>(sizeof(body))> body64;
  enc64.length(ZuBase64::encode<false>(
    {enc64.data(), enc64.size()}, enc));
  body64.length(ZuBase64::encode<false>(
    {body64.data(), body64.size()}, body));
  header << HybridArg << enc64 << '\n' << body64 << '\n';
  return {};
}

static ZuUnion<void, ZeException> appendSshRSA_(
  AgeHeaderScratch &header, ZuBSpan publicKey, ZuBSpan fileKey)
{
  Ztls::PK::Data::PK_PKCS1 data;
  if (!sshRSA_(publicKey, data))
    return ZeEXCEPT(Error, "ZtlsAge", "invalid SSH RSA public key");
  Ztls::PK::PK_RSA key{data};
  size_t keySize = Ztls::Backend::pkey_rsa_size(key.key);
  if (keySize < 256 || keySize > HeaderMaxSize / 2)
    return ZeEXCEPT(Error, "ZtlsAge", "unsupported SSH RSA key size");
  unsigned n = keySize;
  unsigned b64len = ZuBase64::enclen<false>(n);
  auto scratch = ZmScratch(uint8_t, n + b64len, AgeScratchHeap);
  if (!scratch.data())
    return ZeEXCEPT(Error, "ZtlsAge", "SSH RSA allocation failed");
  auto wrapped = scratch.data();
  auto encoded = wrapped + n;
  auto r = key.oaepEncrypt(fileKey, SshRSALabel,
    {wrapped, n});
  if (r.template is<ZeException>())
    return ZuMv(r).template p<ZeException>();
  if (r.template p<size_t>() != n)
    return ZeEXCEPT(Error, "ZtlsAge", "SSH RSA wrap length failed");
  size_t length = ZuBase64::encode<false>(
    {encoded, b64len}, {wrapped, n});
  uint8_t fingerprint[ZuBase64::enclen<false>(4)];
  sshFingerprint_(publicKey, fingerprint);
  header << SshRSAArg << ZuBSpan{fingerprint, sizeof(fingerprint)} << '\n';
  for (size_t offset = 0; offset < length; offset += 64) {
    size_t left = length - offset;
    header << ZuBSpan{encoded + offset,
      left < 64 ? left : 64} << '\n';
  }
  if (!(length % 64)) header << '\n';
  return {};
}

static ZuUnion<void, ZeException> appendSshED_(
  AgeHeaderScratch &header, Ztls::Random &rng,
  ZuBSpan publicKey, ZuBSpan fileKey)
{
  ZuBSpan edPublic;
  if (!sshED_(publicKey, edPublic))
    return ZeEXCEPT(Error, "ZtlsAge", "invalid SSH ED25519 public key");
  struct {
    uint8_t shared[32], tweak[32];
    uint8_t tweaked[32], salt[64], wrapKey[32];
    uint8_t body[FileKeySize + TagSize];
  } data;
  ZuGuard clear{[&data]() { ZuClear(&data, sizeof(data)); }};
  Ztls::PK::PK_ED25519 ed{edPublic};
  auto r = ed.exportX25519({data.salt + 32, 32});
  if (r.template is<ZeException>())
    return ZuMv(r).template p<ZeException>();
  Ztls::PK::SK_X25519 ephemeral{rng};
  r = ephemeral.exportPK({data.salt, 32});
  if (r.template is<ZeException>())
    return ZuMv(r).template p<ZeException>();
  r = ephemeral.agree({data.salt + 32, 32}, data.shared);
  if (r.template is<ZeException>())
    return ZuMv(r).template p<ZeException>();
  if (!hkdf_({}, publicKey, SshEDLabel, data.tweak))
    return ZeEXCEPT(Error, "ZtlsAge", "SSH ED25519 tweak failed");
  Ztls::PK::SK_X25519 tweak{data.tweak};
  r = tweak.agree(data.shared, data.tweaked);
  if (r.template is<ZeException>())
    return ZuMv(r).template p<ZeException>();
  if (!hkdf_(data.tweaked, data.salt, SshEDLabel, data.wrapKey) ||
      !sealKey_(data.wrapKey, fileKey, data.body))
    return ZeEXCEPT(Error, "ZtlsAge", "SSH ED25519 stanza failed");
  uint8_t fingerprint[ZuBase64::enclen<false>(4)];
  sshFingerprint_(publicKey, fingerprint);
  ZuBArray<ZuBase64::enclen<false>(32)> ephemeral64;
  ZuBArray<ZuBase64::enclen<false>(sizeof(data.body))> body64;
  ephemeral64.length(ZuBase64::encode<false>(
    {ephemeral64.data(), ephemeral64.size()}, {data.salt, 32}));
  body64.length(ZuBase64::encode<false>(
    {body64.data(), body64.size()}, data.body));
  header << SshEDArg << ZuBSpan{fingerprint, sizeof(fingerprint)} <<
    ' ' << ephemeral64 << '\n' << body64 << '\n';
  return {};
}

ZuUnion<void, ZeException> Age::encrypt(
  Ztls::Random &rng, ZuSpan<const Recipient> recipients,
  ZuBSpan plaintext, ZiFile &file)
{
  unsigned n = recipients.length();
  if (!n || n > RecipientMax)
    return ZeEXCEPT(Error, "ZtlsAge", "invalid recipient count");
  bool pq = false, classical = false, scrypt = false;
  for (unsigned i = 0; i < n; ++i) {
    auto &recipient = recipients[i];
    if (recipient.template is<HybridRecipient>()) pq = true;
    else if (recipient.template is<ScryptRecipient>()) scrypt = true;
    else classical = true;
  }
  if ((pq && (classical || scrypt)) || (scrypt && n != 1))
    return ZeEXCEPT(Error, "ZtlsAge", "incompatible recipients");
  uint8_t fileKey[FileKeySize], headerKey[KeySize], macValue[KeySize];
  ZuGuard clear{[&fileKey, &headerKey, &macValue]() {
    ZuClear(fileKey, sizeof(fileKey));
    ZuClear(headerKey, sizeof(headerKey));
    ZuClear(macValue, sizeof(macValue));
  }};
  if (!rng.random(fileKey))
    return ZeEXCEPT(Error, "ZtlsAge", "file key generation failed");
  auto header = ZtScratch(AgeHeaderScratch, HeaderStackSize);
  if (!header.data())
    return ZeEXCEPT(Error, "ZtlsAge", "header allocation failed");
  header << Version;
  try {
    for (unsigned i = 0; i < n; ++i) {
      auto &recipient = recipients[i];
      ZuUnion<void, ZeException> r;
      if (recipient.template is<X25519Recipient>())
        r = appendX25519_(header, rng,
          recipient.template p<X25519Recipient>().key, fileKey);
      else if (recipient.template is<HybridRecipient>())
        r = appendHybrid_(header, rng,
          recipient.template p<HybridRecipient>().key, fileKey);
      else if (recipient.template is<SshRSARecipient>())
        r = appendSshRSA_(header,
          recipient.template p<SshRSARecipient>().publicKey, fileKey);
      else if (recipient.template is<SshED25519Recipient>())
        r = appendSshED_(header, rng,
          recipient.template p<SshED25519Recipient>().publicKey, fileKey);
      else
        r = appendScrypt_(header, rng,
          recipient.template p<ScryptRecipient>().passphrase, fileKey);
      if (r.template is<ZeException>())
        return ZuMv(r).template p<ZeException>();
      if (header.length() > HeaderMaxSize)
        return ZeEXCEPT(Error, "ZtlsAge", "header too large");
    }
  } catch (const ZeException &e) {
    return e;
  }
  header << "---";
  if (!hkdf_(fileKey, {}, HeaderInfo, headerKey))
    return ZeEXCEPT(Error, "ZtlsAge", "header key derivation failed");
  Ztls::HMAC<Ztls::SHA256> mac;
  mac.start(headerKey);
  mac.update(header);
  mac.finish(macValue);
  ZuBArray<ZuBase64::enclen<false>(KeySize)> mac64;
  mac64.length(ZuBase64::encode<false>(
    {mac64.data(), mac64.size()}, macValue));
  header << ' ' << mac64 << '\n';
  if (file.write(header.data(), header.length()) != Zi::OK)
    return ZeEXCEPT(Error, "ZtlsAge", "header write failed");
  return writePayload_(rng, file, fileKey, plaintext);
}

static bool nextLine_(ZuBSpan header, size_t &cursor, ZuCSpan &line)
{
  size_t start = cursor;
  while (cursor < header.length() && header[cursor] != '\n') ++cursor;
  if (cursor == header.length()) return false;
  line = {header.data() + start, cursor - start};
  ++cursor;
  return true;
}

static bool parseScrypt_(
  ZuCSpan args, ZuCSpan body, ZuBSpan passphrase,
  ZuSpan<uint8_t> fileKey)
{
  if (args.length() <= ScryptArg.length() ||
      memcmp(args.data(), ScryptArg.data(), ScryptArg.length()))
    return false;
  size_t saltStart = ScryptArg.length();
  size_t split = saltStart;
  while (split < args.length() && args[split] != ' ') ++split;
  if (split == args.length()) return false;
  ZuCSpan saltText{args.data() + saltStart, split - saltStart};
  ZuCSpan logText{args.data() + split + 1,
    args.length() - split - 1};
  if (!logText || logText[0] < '1' || logText[0] > '9') return false;
  for (unsigned i = 1, n = logText.length(); i < n; ++i)
    if (logText[i] < '0' || logText[i] > '9') return false;
  ZuBox<unsigned> parsed;
  if (parsed.scan(logText.data(), logText.length()) != logText.length() ||
      parsed > ScryptMaxLogN)
    return false;
  uint8_t salt[SaltSize], wrapKey[KeySize], wrapped[FileKeySize + TagSize];
  ZuGuard clear{[&salt, &wrapKey, &wrapped]() {
    ZuClear(salt, sizeof(salt));
    ZuClear(wrapKey, sizeof(wrapKey));
    ZuClear(wrapped, sizeof(wrapped));
  }};
  if (!unb64_(saltText, salt) ||
      !unb64_(body, wrapped) ||
      !scryptKey_(passphrase, salt, parsed, wrapKey))
    return false;
  return openKey_(wrapKey, wrapped, fileKey);
}

static bool parseX25519_(
  ZuCSpan args, ZuCSpan body, ZuBSpan seed,
  ZuSpan<uint8_t> fileKey)
{
  if (args.length() != X25519Arg.length() +
        ZuBase64::enclen<false>(X25519KeySize) ||
      memcmp(args.data(), X25519Arg.data(), X25519Arg.length()))
    return false;
  struct {
    uint8_t shared[KeySize];
    uint8_t salt[2 * X25519KeySize];
    uint8_t wrapKey[KeySize];
    uint8_t wrapped[FileKeySize + TagSize];
  } data;
  ZuGuard clear{[&data]() { ZuClear(&data, sizeof(data)); }};
  ZuCSpan ephemeralText{
    args.data() + X25519Arg.length(),
    args.length() - X25519Arg.length()};
  if (!unb64_(ephemeralText, {data.salt, X25519KeySize}) ||
      !unb64_(body, data.wrapped)) return false;
  Ztls::PK::SK_X25519 key{seed};
  auto r = key.exportPK({data.salt + X25519KeySize, X25519KeySize});
  if (r.template is<ZeException>()) return false;
  r = key.agree({data.salt, X25519KeySize}, data.shared);
  if (r.template is<ZeException>()) return false;
  return hkdf_(data.shared, data.salt, X25519Info, data.wrapKey) &&
    openKey_(data.wrapKey, data.wrapped, fileKey);
}

static bool parseHybrid_(
  ZuCSpan args, ZuCSpan body, ZuBSpan seed,
  ZuSpan<uint8_t> fileKey)
{
  if (args.length() != HybridArg.length() +
        ZuBase64::enclen<false>(Ztls::PK::HybridCiphertextSize) ||
      memcmp(args.data(), HybridArg.data(), HybridArg.length()))
    return false;
  uint8_t enc[Ztls::PK::HybridCiphertextSize];
  uint8_t wrapped[FileKeySize + Ztls::HPKE::TagSize];
  ZuCSpan encText{args.data() + HybridArg.length(),
    args.length() - HybridArg.length()};
  if (!unb64_(encText, enc) || !unb64_(body, wrapped)) return false;
  Ztls::PK::SK_MLKEM768_X25519 key{seed};
  auto r = Ztls::HPKE::openBase(key, HybridInfo, {},
    enc, wrapped, fileKey);
  return !r.template is<ZeException>() &&
    r.template p<size_t>() == FileKeySize;
}

static bool parseSshRSA_(
  ZuCSpan args, ZuBSpan body, unsigned bodyChars,
  const Age::SshRSAIdentity &identity, ZuSpan<uint8_t> fileKey)
{
  if (!identity.key ||
      args.length() != SshRSAArg.length() +
        ZuBase64::enclen<false>(4) ||
      memcmp(args.data(), SshRSAArg.data(), SshRSAArg.length()))
    return false;
  uint8_t fingerprint[ZuBase64::enclen<false>(4)];
  sshFingerprint_(identity.publicKey, fingerprint);
  if (memcmp(args.data() + SshRSAArg.length(),
      fingerprint, sizeof(fingerprint))) return false;
  size_t keySize = Ztls::Backend::pkey_rsa_size(identity.key->key);
  if (keySize < 256 || keySize > HeaderMaxSize / 2 ||
      bodyChars != ZuBase64::enclen<false>(keySize)) return false;
  unsigned n = keySize;
  auto scratch = ZmScratch(uint8_t, bodyChars + n, AgeScratchHeap);
  if (!scratch.data()) return false;
  auto encoded = scratch.data();
  auto recovered = encoded + bodyChars;
  ZuGuard clear{[recovered, n]() { ZuClear(recovered, n); }};
  unsigned i = 0;
  for (auto c : body)
    if (c != '\n') encoded[i++] = c;
  if (i != bodyChars ||
      !unb64_({encoded, bodyChars}, {encoded, n}))
    return false;
  auto r = identity.key->oaepDecrypt(
    {encoded, n}, SshRSALabel, {recovered, n});
  if (r.template is<ZeException>() ||
      r.template p<size_t>() != FileKeySize) return false;
  memcpy(fileKey.data(), recovered, FileKeySize);
  return true;
}

static bool parseSshED_(
  ZuCSpan args, ZuCSpan body,
  const Age::SshED25519Identity &identity, ZuSpan<uint8_t> fileKey)
{
  if (!identity.key ||
      args.length() != SshEDArg.length() +
        ZuBase64::enclen<false>(4) + 1 +
        ZuBase64::enclen<false>(X25519KeySize) ||
      memcmp(args.data(), SshEDArg.data(), SshEDArg.length()))
    return false;
  uint8_t fingerprint[ZuBase64::enclen<false>(4)];
  sshFingerprint_(identity.publicKey, fingerprint);
  auto arg = args.data() + SshEDArg.length();
  if (memcmp(arg, fingerprint, sizeof(fingerprint)) ||
      arg[sizeof(fingerprint)] != ' ') return false;
  ZuCSpan ephemeralText{
    arg + sizeof(fingerprint) + 1,
    ZuBase64::enclen<false>(X25519KeySize)};
  struct {
    uint8_t seed[32], digest[Ztls::MD<Ztls::SHA512>::Size];
    uint8_t shared[32], tweak[32];
    uint8_t tweaked[32], salt[64], wrapKey[32];
    uint8_t wrapped[FileKeySize + TagSize];
  } data;
  ZuGuard clear{[&data]() { ZuClear(&data, sizeof(data)); }};
  if (!unb64_(ephemeralText, {data.salt, 32}) ||
      !unb64_(body, data.wrapped)) return false;
  auto r = identity.key->exportSK(data.seed);
  if (r.template is<ZeException>()) return false;
  Ztls::MD<Ztls::SHA512> sha512;
  sha512.update(data.seed);
  sha512.finish(data.digest);
  Ztls::PK::SK_X25519 x{ZuBSpan{data.digest, 32}};
  r = x.exportPK({data.salt + 32, 32});
  if (r.template is<ZeException>()) return false;
  r = x.agree({data.salt, 32}, data.shared);
  if (r.template is<ZeException>()) return false;
  if (!hkdf_({}, identity.publicKey, SshEDLabel, data.tweak)) return false;
  Ztls::PK::SK_X25519 tweak{data.tweak};
  r = tweak.agree(data.shared, data.tweaked);
  if (r.template is<ZeException>()) return false;
  return hkdf_(data.tweaked, data.salt, SshEDLabel, data.wrapKey) &&
    openKey_(data.wrapKey, data.wrapped, fileKey);
}

static bool verifyHeader_(
  ZuBSpan header, size_t macStart,
  ZuBSpan expected, ZuBSpan fileKey)
{
  uint8_t key[KeySize], actual[KeySize];
  ZuGuard clear{[&key, &actual]() {
    ZuClear(key, sizeof(key));
    ZuClear(actual, sizeof(actual));
  }};
  if (!hkdf_(fileKey, {}, HeaderInfo, key)) return false;
  Ztls::HMAC<Ztls::SHA256> mac;
  mac.start(key);
  mac.update({header.data(), macStart + 3});
  mac.finish(actual);
  return Ztls::ctEqual(actual, expected);
}

ZuUnion<size_t, ZeException> Age::decrypt(
  ZiFile &file, ZuSpan<const Identity> identities,
  ZuSpan<uint8_t> plaintext)
{
  if (!identities)
    return ZeEXCEPT(Error, "ZtlsAge", "no identities");
  auto readBuffer = ZmScratch(uint8_t, ReadBufferSize, AgeScratchHeap);
  if (!readBuffer.data())
    return ZeEXCEPT(Error, "ZtlsAge", "reader allocation failed");
  AgeReader reader{file, {readBuffer.data(), ReadBufferSize}};
  auto header = ZtScratch(AgeHeaderScratch, HeaderStackSize);
  if (!header.data())
    return ZeEXCEPT(Error, "ZtlsAge", "header allocation failed");
  size_t macStart = 0;
  for (;;) {
    size_t start = header.length();
    uint8_t c;
    int r;
    do {
      if (header.length() == HeaderMaxSize)
        return ZeEXCEPT(Error, "ZtlsAge", "header too large");
      r = reader.byte(c);
      if (r <= 0)
        return ZeEXCEPT(Error, "ZtlsAge", "truncated header");
      header.append(&c, 1);
    } while (c != '\n');
    size_t length = header.length() - start - 1;
    if (length >= 4 &&
        !memcmp(header.data() + start, "--- ", 4)) {
      macStart = start;
      break;
    }
  }
  if (header.length() < Version.length() ||
      memcmp(header.data(), Version.data(), Version.length()))
    return ZeEXCEPT(Error, "ZtlsAge", "unsupported age version");
  uint8_t candidate[FileKeySize];
  ZuGuard clear{[&candidate]() {
    ZuClear(candidate, sizeof(candidate));
  }};
  size_t end = macStart + 4;
  ZuCSpan macText{header.data() + end, header.length() - end - 1};
  uint8_t expected[KeySize];
  ZuGuard clearMac{[&expected]() { ZuClear(expected, sizeof(expected)); }};
  if (!unb64_(macText, expected))
    return ZeEXCEPT(Error, "ZtlsAge", "invalid header MAC");
  size_t cursor = Version.length();
  unsigned stanzas = 0;
  bool scrypt = false, found = false;
  while (cursor < macStart) {
    ZuCSpan args, body;
    if (!nextLine_({header.data(), macStart}, cursor, args) ||
        args.length() < 4 || memcmp(args.data(), "-> ", 3))
      return ZeEXCEPT(Error, "ZtlsAge", "invalid recipient stanza");
    if (++stanzas > RecipientMax)
      return ZeEXCEPT(Error, "ZtlsAge", "too many recipients");
    bool isScrypt = args.length() >= ScryptArg.length() &&
      !memcmp(args.data(), ScryptArg.data(), ScryptArg.length());
    bool isX25519 = args.length() >= X25519Arg.length() &&
      !memcmp(args.data(), X25519Arg.data(), X25519Arg.length());
    bool isHybrid = args.length() >= HybridArg.length() &&
      !memcmp(args.data(), HybridArg.data(), HybridArg.length());
    bool isSshRSA = args.length() >= SshRSAArg.length() &&
      !memcmp(args.data(), SshRSAArg.data(), SshRSAArg.length());
    bool isSshED = args.length() >= SshEDArg.length() &&
      !memcmp(args.data(), SshEDArg.data(), SshEDArg.length());
    scrypt |= isScrypt;
    unsigned bodyLines = 0;
    unsigned bodyChars = 0;
    size_t bodyStart = cursor;
    do {
      if (!nextLine_({header.data(), macStart}, cursor, body) ||
          body.length() > 64)
        return ZeEXCEPT(Error, "ZtlsAge", "invalid stanza body");
      ++bodyLines;
      bodyChars += body.length();
    } while (body.length() == 64);
    if (bodyLines != 1 && (isScrypt || isX25519 || isHybrid || isSshED))
      return ZeEXCEPT(Error, "ZtlsAge", "invalid native stanza body");
    if (found) continue;
    try {
      for (unsigned i = 0, n = identities.length(); i < n; ++i) {
        auto &identity = identities[i];
        bool unwrapped = false;
        if (isScrypt && identity.template is<ScryptIdentity>())
          unwrapped = parseScrypt_(args, body,
            identity.template p<ScryptIdentity>().passphrase, candidate);
        else if (isX25519 && identity.template is<X25519Identity>())
          unwrapped = parseX25519_(args, body,
            identity.template p<X25519Identity>().seed, candidate);
        else if (isHybrid && identity.template is<HybridIdentity>())
          unwrapped = parseHybrid_(args, body,
            identity.template p<HybridIdentity>().seed, candidate);
        else if (isSshRSA && identity.template is<SshRSAIdentity>())
          unwrapped = parseSshRSA_(args,
            {header.data() + bodyStart, cursor - bodyStart}, bodyChars,
            identity.template p<SshRSAIdentity>(), candidate);
        else if (isSshED && identity.template is<SshED25519Identity>())
          unwrapped = parseSshED_(args, body,
            identity.template p<SshED25519Identity>(), candidate);
        if (unwrapped &&
            verifyHeader_(header, macStart, expected, candidate)) {
          found = true;
          break;
        }
        ZuClear(candidate, sizeof(candidate));
      }
    } catch (const ZeException &e) {
      return e;
    }
  }
  if (cursor != macStart || !stanzas || (scrypt && stanzas != 1) || !found)
    return ZeEXCEPT(Error, "ZtlsAge", "no usable recipient");
  return readPayload_(reader, candidate, plaintext);
}

} // Ztls_
