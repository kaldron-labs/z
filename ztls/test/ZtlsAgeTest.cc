//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuBase64.hh>
#include <zlib/ZuHex.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZtlsAge.hh>
#include <zlib/ZtlsKEM.hh>
#include <zlib/ZtlsMD.hh>
#include <zlib/ZtlsPK.hh>
#include "ZiTestResidue.hh"

#include "ZtlsAgeVectors.hh"
#include "ZtlsAgeSSHVectors.hh"

using namespace ZuTestUtil;
using Bytes = ZtBArray<ZtArrayHeapID<"Ztls.Age.Test">>;
using Chars = ZtCArray<ZtArrayHeapID<"Ztls.Age.Test">>;

static void nativeKeys()
{
  ZuTestScope(nativeKeys);
  constexpr auto identity =
    "AGE-SECRET-KEY-1GFPYYSJZGFPYYSJZGFPYYSJZGFPYYSJZGFPYYSJZGFPYYSJZGFPQ4EGAEX"_z;
  constexpr auto recipient =
    "age1zvkyg2lqzraa2lnjvqej32nkuu0ues2s82hzrye869xeexvn73equnujwj"_z;
  uint8_t secret[32], pub[32];
  char text[sizeof("AGE-SECRET-KEY-1GFPYYSJZGFPYYSJZGFPYYSJZGFPYYSJZGFPYYSJZGFPYYSJZGFPQ4EGAEX") - 1];
  auto sk = ZtlsAge::decodeIdentity(ZtlsAgeKeyType::X25519, identity, secret);
  ZuCheck(!sk.template is<ZeException>());
  if (sk.template is<ZeException>()) return;
  ZuCheck(sk.template p<size_t>() == sizeof(secret));
  auto encoded = ZtlsAge::encodeIdentity(
    ZtlsAgeKeyType::X25519, secret, {text, sizeof(text)});
  ZuCheck(!encoded.template is<ZeException>());
  if (!encoded.template is<ZeException>()) {
    ZuCSpan actual{text, encoded.template p<size_t>()};
    ZuCheck(actual == identity);
  }
  auto pk = ZtlsAge::decodeRecipient(ZtlsAgeKeyType::X25519, recipient, pub);
  ZuCheck(!pk.template is<ZeException>());
  if (pk.template is<ZeException>()) return;
  ZuCheck(pk.template p<size_t>() == sizeof(pub));
  encoded = ZtlsAge::encodeRecipient(
    ZtlsAgeKeyType::X25519, pub, {text, sizeof(text)});
  ZuCheck(!encoded.template is<ZeException>());
  if (!encoded.template is<ZeException>()) {
    ZuCSpan actual{text, encoded.template p<size_t>()};
    ZuCheck(actual == recipient);
  }
  ZuCSpan bad{"age1zvkyg2lqzraa2lnjvqej32nkuu0ues2s82hzrye869xeexvn73equnujwx"};
  ZuCheck(ZtlsAge::decodeRecipient(ZtlsAgeKeyType::X25519, bad, pub)
    .template is<ZeException>());
  ZuCheck(ZtlsAge::decodeIdentity(ZtlsAgeKeyType::X25519,
    "AGE-SECRET-KEY-1GFPYYSJZGFPYYSJZGFPYYSJZGFPYYSJZGFPYYSJZGFPYYSJZGFPQ4EGAEx"_z,
    secret).template is<ZeException>());
  // The checksum is valid, but the last data symbol has nonzero pad bits.
  ZuCheck(ZtlsAge::decodeIdentity(ZtlsAgeKeyType::X25519,
    "AGE-SECRET-KEY-1GFPYYSJZGFPYYSJZGFPYYSJZGFPYYSJZGFPYYSJZGFPPG0UGY5"_z,
    secret).template is<ZeException>());
  ZuClear(secret, sizeof(secret));
}

static void hybridKey()
{
  ZuTestScope(hybridKey);
  constexpr auto identity =
    "AGE-SECRET-KEY-PQ-1XX76JRALNLXDMEW0CRK45QMCCH4X06SE84UN3VPM33W6HWDX0H3SK3ZQFR"_z;
  uint8_t seed[Ztls::PK::HybridSeedSize];
  auto decoded = ZtlsAge::decodeIdentity(
    ZtlsAgeKeyType::Hybrid, identity, seed);
  ZuCheck(!decoded.template is<ZeException>());
  if (decoded.template is<ZeException>()) return;
  ZuCheck(decoded.template p<size_t>() == sizeof(seed));
  char text[sizeof("AGE-SECRET-KEY-PQ-1XX76JRALNLXDMEW0CRK45QMCCH4X06SE84UN3VPM33W6HWDX0H3SK3ZQFR") - 1];
  auto encoded = ZtlsAge::encodeIdentity(
    ZtlsAgeKeyType::Hybrid, seed, {text, sizeof(text)});
  ZuCheck(!encoded.template is<ZeException>());
  if (!encoded.template is<ZeException>()) {
    ZuCSpan actual{text, encoded.template p<size_t>()};
    ZuCheck(actual == identity);
  }
  ZuClear(seed, sizeof(seed));
}

static void scryptFile()
{
  ZuTestScopeRT(scryptFile);
  auto path = ZiTestResidue::file("scrypt.age");
  Ztls::Random rng;
  ZuCheckRT(rng.init());
  ZtlsAge age;
  ZtlsAge::Recipient recipient =
    ZtlsAge::ScryptRecipient{ZuBSpan{"passphrase"}};
  ZtlsAge::Identity identity =
    ZtlsAge::ScryptIdentity{ZuBSpan{"passphrase"}};
  enum { PlainSize = (64 << 10) + 17 };
  Bytes plain;
  plain.length(PlainSize);
  for (unsigned i = 0; i < PlainSize; ++i) plain[i] = uint8_t(i * 13);
  Bytes recovered;
  recovered.length(PlainSize);
  constexpr unsigned sizes[] = {0, 64 << 10, PlainSize};
  for (auto size : sizes) {
    ZiFile output{path, ZiFile::Write | ZiFile::GC, 0600};
    ZuCheckRT(output);
    if (!output) return;
    auto encrypted = age.encrypt(rng, {&recipient, 1},
      {plain.data(), size}, output);
    ZuCheckRT(!encrypted.template is<ZeException>());
    output.close();
    if (encrypted.template is<ZeException>()) return;
    ZiFile input{path, ZiFile::ReadOnly | ZiFile::GC};
    ZuCheckRT(input);
    if (!input) return;
    auto decrypted = age.decrypt(input, {&identity, 1},
      {recovered.data(), size});
    ZuCheckRT(!decrypted.template is<ZeException>());
    if (!decrypted.template is<ZeException>()) {
      ZuCheckRT(decrypted.template p<size_t>() == size);
      ZuCheckRT(!memcmp(recovered.data(), plain.data(), size));
    }
    ZuClear(recovered.data(), size);
    input.close();
  }
  {
    ZtlsAge::Identity wrong =
      ZtlsAge::ScryptIdentity{ZuBSpan{"wrong passphrase"}};
    ZiFile input{path, ZiFile::ReadOnly | ZiFile::GC};
    ZuCheckRT(input);
    ZuCheckRT(age.decrypt(input, {&wrong, 1}, recovered)
      .template is<ZeException>());
  }
  {
    ZiFile input{path, ZiFile::ReadOnly | ZiFile::GC};
    ZuCheckRT(input);
    auto shortResult = age.decrypt(input, {&identity, 1},
      {recovered.data(), PlainSize - 1});
    ZuCheckRT(shortResult.template is<ZeException>());
    bool cleared = true;
    for (unsigned i = 0; i < PlainSize - 1; ++i)
      cleared &= !recovered[i];
    ZuCheckRT(cleared);
  }
  {
    ZiFile tamper{path, ZiFile::GC};
    ZuCheckRT(tamper);
    if (!tamper) return;
    auto last = tamper.size() - 1;
    uint8_t c;
    ZuCheckRT(tamper.pread(last, &c, 1) == 1);
    c ^= 1;
    ZuCheckRT(tamper.pwrite(last, &c, 1) == Zi::OK);
  }
  {
    ZiFile input{path, ZiFile::ReadOnly | ZiFile::GC};
    ZuCheckRT(input);
    auto badTag = age.decrypt(input, {&identity, 1},
      {recovered.data(), recovered.length()});
    ZuCheckRT(badTag.template is<ZeException>());
    bool cleared = true;
    for (unsigned i = 0; i < PlainSize; ++i)
      cleared &= !recovered[i];
    ZuCheckRT(cleared);
  }
}

static void recipientFiles()
{
  ZuTestScopeRT(recipientFiles);
  auto path = ZiTestResidue::file("recipient.age");
  Ztls::Random rng;
  ZuCheckRT(rng.init());
  ZtlsAge age;
  uint8_t xSeed[2][Ztls::X25519KeySize];
  uint8_t xPublic[2][Ztls::X25519KeySize];
  uint8_t pqSeed[2][Ztls::PK::HybridSeedSize];
  uint8_t pqPublic[2][Ztls::PK::HybridPublicSize];
  ZuGuard clear{[&xSeed, &pqSeed]() {
    ZuClear(xSeed, sizeof(xSeed));
    ZuClear(pqSeed, sizeof(pqSeed));
  }};
  for (unsigned i = 0; i < 2; ++i) {
    ZuCheckRT(rng.random(xSeed[i]));
    ZuCheckRT(rng.random(pqSeed[i]));
    Ztls::PK::SK_X25519 x{xSeed[i]};
    Ztls::PK::SK_MLKEM768_X25519 pq{pqSeed[i]};
    ZuCheckRT(!x.exportPK(xPublic[i]).template is<ZeException>());
    ZuCheckRT(!pq.exportPK(pqPublic[i]).template is<ZeException>());
  }
  uint8_t plain[257], recovered[sizeof(plain)];
  for (unsigned i = 0; i < sizeof(plain); ++i) plain[i] = uint8_t(i * 37);
  ZtlsAge::Recipient recipients[2];
  ZtlsAge::Identity identities[2];
  for (unsigned profile = 0; profile < 2; ++profile) {
    for (unsigned i = 0; i < 2; ++i) {
      if (profile) {
        recipients[i] = ZtlsAge::HybridRecipient{pqPublic[i]};
        identities[i] = ZtlsAge::HybridIdentity{pqSeed[i]};
      } else {
        recipients[i] = ZtlsAge::X25519Recipient{xPublic[i]};
        identities[i] = ZtlsAge::X25519Identity{xSeed[i]};
      }
    }
    ZiFile output{path, ZiFile::Write | ZiFile::GC, 0600};
    ZuCheckRT(output);
    if (!output) return;
    auto encrypted = age.encrypt(rng, recipients, plain, output);
    ZuCheckRT(!encrypted.template is<ZeException>());
    output.close();
    if (encrypted.template is<ZeException>()) return;
    // The second recipient must be usable even when the first identity is
    // absent; a wrong identity preceding it must not terminate the scan.
    ZtlsAge::Identity reversed[2] = {identities[1], identities[0]};
    ZiFile input{path, ZiFile::ReadOnly | ZiFile::GC};
    ZuCheckRT(input);
    if (!input) return;
    auto decrypted = age.decrypt(input, reversed, recovered);
    ZuCheckRT(!decrypted.template is<ZeException>());
    if (!decrypted.template is<ZeException>()) {
      ZuCheckRT(decrypted.template p<size_t>() == sizeof(plain));
      ZuCheckRT(!memcmp(recovered, plain, sizeof(plain)));
    }
    input.close();
    ZuClear(recovered, sizeof(recovered));
    ZtlsAge::Identity wrong = profile ?
      ZtlsAge::Identity{ZtlsAge::HybridIdentity{xSeed[0]}} :
      ZtlsAge::Identity{ZtlsAge::X25519Identity{pqSeed[0]}};
    ZiFile wrongInput{path, ZiFile::ReadOnly | ZiFile::GC};
    ZuCheckRT(wrongInput);
    if (!wrongInput) return;
    auto rejected = age.decrypt(wrongInput, {&wrong, 1}, recovered);
    ZuCheckRT(rejected.template is<ZeException>());
    wrongInput.close();
  }
  ZtlsAge::Recipient mixed[2] = {
    ZtlsAge::X25519Recipient{xPublic[0]},
    ZtlsAge::HybridRecipient{pqPublic[0]}
  };
  ZiFile output{path, ZiFile::Write | ZiFile::GC, 0600};
  ZuCheckRT(output);
  if (!output) return;
  ZuCheckRT(age.encrypt(rng, mixed, plain, output)
    .template is<ZeException>());
  ZuCheckRT(output.size() == 0);
}

static void malformedFile()
{
  ZuTestScopeRT(malformedFile);
  auto path = ZiTestResidue::file("malformed.age");
  Ztls::Random rng;
  ZuCheckRT(rng.init());
  uint8_t seed[Ztls::X25519KeySize], publicKey[sizeof(seed)];
  ZuGuard clear{[&seed]() { ZuClear(seed, sizeof(seed)); }};
  ZuCheckRT(rng.random(seed));
  Ztls::PK::SK_X25519 key{seed};
  ZuCheckRT(!key.exportPK(publicKey).template is<ZeException>());
  ZtlsAge::Recipient recipient = ZtlsAge::X25519Recipient{publicKey};
  ZtlsAge::Identity identity = ZtlsAge::X25519Identity{seed};
  Bytes plain, cipher, recovered;
  plain.length((64 << 10) + 17);
  recovered.length(plain.length());
  for (unsigned i = 0, n = plain.length(); i < n; ++i)
    plain[i] = uint8_t(i * 29);
  ZtlsAge age;
  {
    ZiFile output{path, ZiFile::Write | ZiFile::GC, 0600};
    ZuCheckRT(output);
    ZuCheckRT(!age.encrypt(rng, {&recipient, 1}, plain, output)
      .template is<ZeException>());
  }
  {
    ZiFile input{path, ZiFile::ReadOnly | ZiFile::GC};
    ZuCheckRT(input);
    cipher.length(input.size());
    ZuCheckRT(input.read(cipher.data(), cipher.length()) == cipher.length());
  }
  size_t payload = 0;
  for (size_t i = 0; i + 5 < cipher.length(); ++i) {
    if (!memcmp(cipher.data() + i, "\n--- ", 5)) {
      payload = i + 5;
      while (payload < cipher.length() && cipher[payload] != '\n')
        ++payload;
      ++payload;
      break;
    }
  }
  ZuCheckRT(payload && payload + 16 + (64 << 10) + 16 < cipher.length());
  struct Case {
    size_t length;
    size_t change;
    uint8_t value;
    bool clearsChunk;
  };
  const Case cases[] = {
    {1, 0, 0, false},
    {cipher.length(), 0, 'X', false},
    {payload, payload - 2, '!', false},
    {payload + 15, 0, 0, false},
    {payload + 16 + (64 << 10) + 16, 0, 0, true},
    {cipher.length() - 1, 0, 0, true},
    {cipher.length(), cipher.length() - 1,
      uint8_t(cipher[cipher.length() - 1] ^ 1), true}
  };
  for (auto &test : cases) {
    ZiFile output{path, ZiFile::Write | ZiFile::GC, 0600};
    ZuCheckRT(output);
    ZuCheckRT(output.write(cipher.data(), test.length) == Zi::OK);
    if (test.value)
      ZuCheckRT(output.pwrite(test.change, &test.value, 1) == Zi::OK);
    output.close();
    memset(recovered.data(), 0xa5, recovered.length());
    ZiFile input{path, ZiFile::ReadOnly | ZiFile::GC};
    ZuCheckRT(input);
    ZuCheckRT(age.decrypt(input, {&identity, 1}, recovered)
      .template is<ZeException>());
    if (test.clearsChunk) {
      bool cleared = true;
      for (unsigned i = 0; i < (64 << 10); ++i)
        cleared &= !recovered[i];
      ZuCheckRT(cleared);
    }
  }
}

static void testkit()
{
  ZuTestScopeRT(testkit);
  auto path = ZiTestResidue::file("testkit.age");
  ZtlsAge age;
  for (auto &vector : ZtlsAgeVectors) {
    ZuBSpan encoded{vector.encoded};
    Bytes data;
    data.length(ZuBase64::declen(encoded.length()));
    data.length(ZuBase64::decode(data, encoded));
    size_t start = 0;
    while (start + 1 < data.length() &&
        (data[start] != '\n' || data[start + 1] != '\n')) ++start;
    ZuCheckRT(start + 1 < data.length());
    if (start + 1 == data.length()) return;
    start += 2;
    {
      ZiFile output{path, ZiFile::Write | ZiFile::GC, 0600};
      ZuCheckRT(output);
      ZuCheckRT(output.write(data.data() + start,
        data.length() - start) == Zi::OK);
    }
    uint8_t seed[Ztls::PK::HybridSeedSize];
    ZuGuard clear{[&seed]() { ZuClear(seed, sizeof(seed)); }};
    ZtlsAge::Identity identity;
    if (*vector.passphrase)
      identity = ZtlsAge::ScryptIdentity{ZuBSpan{vector.passphrase}};
    else {
      bool hybrid = !memcmp(vector.identity, "AGE-SECRET-KEY-PQ-",
        sizeof("AGE-SECRET-KEY-PQ-") - 1);
      auto decoded = ZtlsAge::decodeIdentity(
        hybrid ? ZtlsAgeKeyType::Hybrid : ZtlsAgeKeyType::X25519,
        ZuCSpan{vector.identity}, seed);
      ZuCheckRT(!decoded.template is<ZeException>());
      if (decoded.template is<ZeException>()) return;
      identity = hybrid ?
        ZtlsAge::Identity{ZtlsAge::HybridIdentity{seed}} :
        ZtlsAge::Identity{ZtlsAge::X25519Identity{seed}};
    }
    Bytes recovered;
    recovered.length(data.length() - start);
    ZiFile input{path, ZiFile::ReadOnly | ZiFile::GC};
    ZuCheckRT(input);
    auto result = age.decrypt(input, {&identity, 1}, recovered);
    ZuCheckRT(result.template is<ZeException>() != vector.success);
    if (!vector.success || result.template is<ZeException>()) continue;
    uint8_t actual[Ztls::MD<Ztls::SHA256>::Size];
    uint8_t expected[sizeof(actual)];
    ZuCheckRT(ZuHex::decode(expected, ZuCSpan{vector.hash}) ==
      sizeof(expected));
    Ztls::MD<Ztls::SHA256> hash;
    hash.update({recovered.data(), result.template p<size_t>()});
    hash.finish(actual);
    ZuCheckRT(!memcmp(actual, expected, sizeof(actual)));
  }
}

static void sshString(Bytes &blob, ZuBSpan bytes, bool mpint = false)
{
  if (mpint)
    while (bytes.length() > 1 && !bytes[0]) bytes.offset(1);
  bool sign = mpint && bytes && (bytes[0] & 0x80);
  unsigned n = bytes.length() + unsigned(sign);
  uint8_t length[4] = {
    uint8_t(n >> 24), uint8_t(n >> 16), uint8_t(n >> 8), uint8_t(n)
  };
  blob.append(length, sizeof(length));
  if (sign) {
    uint8_t zero = 0;
    blob.append(&zero, 1);
  }
  blob.append(bytes.data(), bytes.length());
}

static bool sshLine(ZuCSpan type, ZuBSpan blob)
{
  Bytes encoded;
  encoded.length(ZuBase64::enclen(blob.length()));
  encoded.length(ZuBase64::encode(encoded, blob));
  Bytes line;
  line << type << ' ' << encoded << " comment";
  Bytes decoded;
  decoded.length(blob.length());
  auto result = ZtlsAge::decodeSSHRecipient(line, decoded);
  if (result.template is<ZeException>()) return false;
  auto recipient = ZuMv(result).template p<ZtlsAge::Recipient>();
  ZuBSpan parsed = type == "ssh-rsa"_z ?
    recipient.template p<ZtlsAge::SshRSARecipient>().publicKey :
    recipient.template p<ZtlsAge::SshED25519Recipient>().publicKey;
  if (parsed != blob) return false;
  if (unsigned tail = blob.length() % 3) {
    constexpr auto alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"_z;
    unsigned pos = type.length() + 1 + encoded.length() - (4 - tail);
    char c = line[pos];
    line[pos] = alphabet[ZuBase64::lookup(c) + 1];
    bool rejected = ZtlsAge::decodeSSHRecipient(line, decoded)
      .template is<ZeException>();
    line[pos] = c;
    if (!rejected) return false;
  }
  line[type.length() + 1] = '!';
  return ZtlsAge::decodeSSHRecipient(line, decoded)
    .template is<ZeException>();
}

static void sshRSAFile()
{
  ZuTestScopeRT(sshRSAFile);
  auto path = ZiTestResidue::file("ssh-rsa.age");
  Ztls::Random rng;
  ZuCheckRT(rng.init());
  Ztls::PK::SK_RSA key{rng, 2048};
  uint8_t modulus[256], exponent[8];
  ZuCheckRT(Ztls::Backend::pkey_rsa_export_public(
    key.key, modulus, exponent));
  Bytes blob;
  sshString(blob, ZuBSpan{"ssh-rsa"});
  sshString(blob, exponent, true);
  sshString(blob, modulus, true);
  ZuCheckRT(sshLine("ssh-rsa"_z, blob));
  Bytes shortBlob;
  uint8_t smallExponent = 3, smallModulus[2] = {1, 1};
  sshString(shortBlob, ZuBSpan{"ssh-rsa"});
  sshString(shortBlob, {&smallExponent, 1});
  sshString(shortBlob, smallModulus);
  ZuCheckRT(sshLine("ssh-rsa"_z, shortBlob));
  ZtlsAge::Recipient recipient = ZtlsAge::SshRSARecipient{blob};
  ZtlsAge::Identity identity = ZtlsAge::SshRSAIdentity{blob, &key};
  uint8_t plain[37], recovered[sizeof(plain)];
  for (unsigned i = 0; i < sizeof(plain); ++i) plain[i] = uint8_t(i * 19);
  ZtlsAge age;
  ZiFile output{path, ZiFile::Write | ZiFile::GC, 0600};
  ZuCheckRT(output);
  if (!output) return;
  auto encrypted = age.encrypt(rng, {&recipient, 1}, plain, output);
  ZuCheckRT(!encrypted.template is<ZeException>());
  output.close();
  if (encrypted.template is<ZeException>()) return;
  ZiFile input{path, ZiFile::ReadOnly | ZiFile::GC};
  ZuCheckRT(input);
  if (!input) return;
  auto decrypted = age.decrypt(input, {&identity, 1}, recovered);
  ZuCheckRT(!decrypted.template is<ZeException>());
  if (!decrypted.template is<ZeException>()) {
    ZuCheckRT(decrypted.template p<size_t>() == sizeof(plain));
    ZuCheckRT(!memcmp(recovered, plain, sizeof(plain)));
  }
  ZuClear(recovered, sizeof(recovered));
}

static void sshEDFile()
{
  ZuTestScopeRT(sshEDFile);
  auto path = ZiTestResidue::file("ssh-ed.age");
  Ztls::Random rng;
  ZuCheckRT(rng.init());
  Ztls::PK::SK_ED25519 key{rng};
  uint8_t publicKey[32];
  ZuCheckRT(!key.exportPK(publicKey).template is<ZeException>());
  Bytes blob;
  sshString(blob, ZuBSpan{"ssh-ed25519"});
  sshString(blob, publicKey);
  ZuCheckRT(sshLine("ssh-ed25519"_z, blob));
  ZtlsAge::Recipient recipient = ZtlsAge::SshED25519Recipient{blob};
  ZtlsAge::Identity identity = ZtlsAge::SshED25519Identity{blob, &key};
  uint8_t plain[37], recovered[sizeof(plain)];
  for (unsigned i = 0; i < sizeof(plain); ++i) plain[i] = uint8_t(i * 23);
  ZtlsAge age;
  ZiFile output{path, ZiFile::Write | ZiFile::GC, 0600};
  ZuCheckRT(output);
  if (!output) return;
  auto encrypted = age.encrypt(rng, {&recipient, 1}, plain, output);
  ZuCheckRT(!encrypted.template is<ZeException>());
  output.close();
  if (encrypted.template is<ZeException>()) return;
  ZiFile input{path, ZiFile::ReadOnly | ZiFile::GC};
  ZuCheckRT(input);
  if (!input) return;
  auto decrypted = age.decrypt(input, {&identity, 1}, recovered);
  ZuCheckRT(!decrypted.template is<ZeException>());
  if (!decrypted.template is<ZeException>()) {
    ZuCheckRT(decrypted.template p<size_t>() == sizeof(plain));
    ZuCheckRT(!memcmp(recovered, plain, sizeof(plain)));
  }
  ZuClear(recovered, sizeof(recovered));
}

static void openSSHKey()
{
  ZuTestScopeRT(openSSHKey);
  char pem[] =
    "-----BEGIN OPENSSH PRIVATE KEY-----\n"
    "b3BlbnNzaC1rZXktdjEAAAAABG5vbmUAAAAEbm9uZQAAAAAAAAABAAAAMwAAAAtzc2gtZW\n"
    "QyNTUxOQAAACCUnC67IDwnZNFWZIjRzGbGFPfs1KbFB8/KdZeVJ9ghRgAAAJjRLja70S42\n"
    "uwAAAAtzc2gtZWQyNTUxOQAAACCUnC67IDwnZNFWZIjRzGbGFPfs1KbFB8/KdZeVJ9ghRg\n"
    "AAAECGvsfwfKj6rcbG98C5s/iK1KMx0lj5fX2hMVaJyfNz1pScLrsgPCdk0VZkiNHMZsYU\n"
    "9+zUpsUHz8p1l5Un2CFGAAAADmNvdW50MEBiYXJvcXVlAQIDBAUGBw==\n"
    "-----END OPENSSH PRIVATE KEY-----\n";
  constexpr auto pub =
    "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIJScLrsgPCdk0VZkiNHMZsYU9+zUpsUHz8p1l5Un2CFG"_z;
  uint8_t publicKey[128], expected[128];
  auto loaded = ZtlsAge::loadOpenSSH(
    {pem, sizeof(pem) - 1}, publicKey);
  ZuCheckRT(!loaded.template is<ZeException>());
  if (loaded.template is<ZeException>()) return;
  auto key = ZuMv(loaded).template p<ZtlsAge::SshKey>();
  ZuCheckRT(dynamic_cast<Ztls::PK::SK_ED25519 *>(key.key.ptr()));
  bool cleared = true;
  for (auto c : pem) cleared &= !c;
  ZuCheckRT(cleared);
  auto parsed = ZtlsAge::decodeSSHRecipient(pub, expected);
  ZuCheckRT(!parsed.template is<ZeException>());
  if (!parsed.template is<ZeException>())
    ZuCheckRT(key.publicKey == parsed.template p<ZtlsAge::Recipient>()
      .template p<ZtlsAge::SshED25519Recipient>().publicKey);
}

static void sshReference()
{
  ZuTestScopeRT(sshReference);
  auto path = ZiTestResidue::file("ssh-reference.age");
  constexpr auto plain = "Ztls SSH reference fixture\n"_z;
  ZtlsAge age;
  for (auto &vector : ZtlsAgeSSHVectors) {
    Chars privateKey;
    privateKey.length(strlen(vector.privateKey));
    memcpy(privateKey.data(), vector.privateKey, privateKey.length());
    Bytes publicKey;
    publicKey.length(strlen(vector.publicKey));
    auto loaded = ZtlsAge::loadOpenSSH(privateKey, publicKey);
    ZuCheckRT(!loaded.template is<ZeException>());
    if (loaded.template is<ZeException>()) return;
    auto key = ZuMv(loaded).template p<ZtlsAge::SshKey>();
    bool cleared = true;
    for (auto c : privateKey) cleared &= !c;
    ZuCheckRT(cleared);
    Bytes blob;
    blob.length(publicKey.length());
    auto decoded = ZtlsAge::decodeSSHRecipient(
      ZuCSpan{vector.publicKey}, blob);
    ZuCheckRT(!decoded.template is<ZeException>());
    if (decoded.template is<ZeException>()) return;
    bool rsa = vector.name[0] == 'r';
    auto recipient = ZuMv(decoded).template p<ZtlsAge::Recipient>();
    ZuBSpan expected = rsa ?
      recipient.template p<ZtlsAge::SshRSARecipient>().publicKey :
      recipient.template p<ZtlsAge::SshED25519Recipient>().publicKey;
    ZuCheckRT(key.publicKey == expected);
    ZtlsAge::Identity identity;
    if (rsa) {
      auto sk = dynamic_cast<Ztls::PK::SK_RSA *>(key.key.ptr());
      ZuCheckRT(sk);
      identity = ZtlsAge::SshRSAIdentity{key.publicKey, sk};
    } else {
      auto sk = dynamic_cast<Ztls::PK::SK_ED25519 *>(key.key.ptr());
      ZuCheckRT(sk);
      identity = ZtlsAge::SshED25519Identity{key.publicKey, sk};
    }
    ZuBSpan encoded{vector.cipher};
    Bytes cipher;
    cipher.length(ZuBase64::declen(encoded.length()));
    cipher.length(ZuBase64::decode(cipher, encoded));
    {
      ZiFile output{path, ZiFile::Write | ZiFile::GC, 0600};
      ZuCheckRT(output);
      ZuCheckRT(output.write(cipher.data(), cipher.length()) == Zi::OK);
    }
    Bytes recovered;
    recovered.length(cipher.length());
    ZiFile input{path, ZiFile::ReadOnly | ZiFile::GC};
    ZuCheckRT(input);
    auto result = age.decrypt(input, {&identity, 1}, recovered);
    ZuCheckRT(!result.template is<ZeException>());
    if (result.template is<ZeException>()) return;
    ZuCheckRT(result.template p<size_t>() == plain.length());
    ZuCheckRT(!memcmp(recovered.data(), plain.data(), plain.length()));
  }
}

int main()
{
  ZiTestResidue::init("ZtlsAgeTest");
  ZuTestMain();
  ZuTestCall_("nativeKeys", nativeKeys);
  ZuTestCall_("hybridKey", hybridKey);
  ZuTestCall_("scryptFile", scryptFile);
  ZuTestCall_("recipientFiles", recipientFiles);
  ZuTestCall_("malformedFile", malformedFile);
  ZuTestCall_("testkit", testkit);
  ZuTestCall_("sshRSAFile", sshRSAFile);
  ZuTestCall_("sshEDFile", sshEDFile);
  ZuTestCall_("openSSHKey", openSSHKey);
  ZuTestCall_("sshReference", sshReference);
}
