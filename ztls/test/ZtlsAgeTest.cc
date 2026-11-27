//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuBase64.hh>
#include <zlib/ZuTestUtil.hh>

#include <zlib/ZtArray.hh>

#include <zlib/ZtlsAge.hh>
#include <zlib/ZtlsKEM.hh>
#include <zlib/ZtlsPK.hh>

#include "ZiTestResidue.hh"

#include "ZtlsTestKeys.hh"

using namespace ZuTestUtil;
using Bytes = ZtBArray<ZtArrayHeapID<"Ztls.Age.Test">>;
using Chars = ZtCArray<ZtArrayHeapID<"Ztls.Age.Test">>;

static void nativeKeys()
{
  ZuTestScopeRT(nativeKeys);
  for (bool hybrid : {false, true}) {
    auto type = hybrid ? ZtlsAgeKeyType::Hybrid : ZtlsAgeKeyType::X25519;
    Chars identity;
    uint8_t seed[32], decoded[32];
    char text[128];
    ZuGuard clear{[&]() {
      ZuClear(identity); ZuClear(seed, sizeof(seed)); ZuClear(decoded, sizeof(decoded));
      ZuClear(text, sizeof(text));
    }};
    ZuCheckRT(ZtlsTestKeys::load(hybrid ? "hybrid.identity" : "alice.identity", identity), return);
    ZuCheckRT(ZtlsTestKeys::read(hybrid ? "hybrid" : "alice", seed), return);
    auto sk = ZtlsAge::decodeIdentity(type, identity, decoded);
    ZuCheckRT(!sk.is<ZeException>() && sk.p<size_t>() == sizeof(seed), return);
    ZuCheckRT(!memcmp(seed, decoded, sizeof(seed)));
    auto encoded = ZtlsAge::encodeIdentity(type, seed, {text, sizeof(text)});
    ZuCheckRT(!encoded.is<ZeException>(), return);
    ZuCheckRT((ZuCSpan{text, encoded.p<size_t>()} == ZuCSpan{identity}));
    identity[identity.length() - 1] ^= 1;
    ZuCheckRT(ZtlsAge::decodeIdentity(type, identity, decoded).is<ZeException>());
  }
  Chars recipient, badPadding;
  uint8_t pub[32], expected[32];
  ZuCheckRT(ZtlsTestKeys::load("alice.recipient", recipient), return);
  ZuCheckRT(ZtlsTestKeys::read("alice.pub", expected), return);
  auto pk = ZtlsAge::decodeRecipient(ZtlsAgeKeyType::X25519, recipient, pub);
  ZuCheckRT(!pk.is<ZeException>() && pk.p<size_t>() == sizeof(pub), return);
  ZuCheckRT(!memcmp(pub, expected, sizeof(pub)));
  char text[128];
  auto encoded = ZtlsAge::encodeRecipient(ZtlsAgeKeyType::X25519, pub, {text, sizeof(text)});
  ZuCheckRT(!encoded.is<ZeException>(), return);
  ZuCheckRT((ZuCSpan{text, encoded.p<size_t>()} == ZuCSpan{recipient}));
  recipient[0] = 'A';
  ZuCheckRT(ZtlsAge::decodeRecipient(ZtlsAgeKeyType::X25519, recipient, pub).is<ZeException>());
  // Valid checksum, nonzero padding bits.
  ZuCheckRT(ZtlsTestKeys::load("bad-padding.identity", badPadding), return);
  ZuCheckRT(ZtlsAge::decodeIdentity(ZtlsAgeKeyType::X25519, badPadding, pub).is<ZeException>());
  ZuClear(badPadding);
}

static void scryptFile()
{
  ZuTestScopeRT(scryptFile);
  auto	       path = ZiTestResidue::file("scrypt.age");
  Ztls::Random rng;
  ZuCheckRT(rng.init());
  ZtlsAge	     age;
  ZtlsAge::Recipient recipient = ZtlsAge::ScryptRecipient{ZuBSpan{"passphrase"}};
  ZtlsAge::Identity  identity = ZtlsAge::ScryptIdentity{ZuBSpan{"passphrase"}};
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
    auto encrypted = age.encrypt(rng, {&recipient, 1}, {plain.data(), size}, output);
    ZuCheckRT(!encrypted.template is<ZeException>());
    output.close();
    if (encrypted.template is<ZeException>()) return;
    ZiFile input{path, ZiFile::ReadOnly | ZiFile::GC};
    ZuCheckRT(input);
    if (!input) return;
    auto decrypted = age.decrypt(input, {&identity, 1}, {recovered.data(), size});
    ZuCheckRT(!decrypted.template is<ZeException>());
    if (!decrypted.template is<ZeException>()) {
      ZuCheckRT(decrypted.template p<size_t>() == size);
      ZuCheckRT(!memcmp(recovered.data(), plain.data(), size));
    }
    ZuClear(recovered.data(), size);
    input.close();
  }
  {
    ZtlsAge::Identity wrong = ZtlsAge::ScryptIdentity{ZuBSpan{"wrong passphrase"}};
    ZiFile	      input{path, ZiFile::ReadOnly | ZiFile::GC};
    ZuCheckRT(input);
    ZuCheckRT(age.decrypt(input, {&wrong, 1}, recovered).template is<ZeException>());
  }
  {
    ZiFile input{path, ZiFile::ReadOnly | ZiFile::GC};
    ZuCheckRT(input);
    auto shortResult = age.decrypt(input, {&identity, 1}, {recovered.data(), PlainSize - 1});
    ZuCheckRT(shortResult.template is<ZeException>());
    bool cleared = true;
    for (unsigned i = 0; i < PlainSize - 1; ++i) cleared &= !recovered[i];
    ZuCheckRT(cleared);
  }
  {
    ZiFile tamper{path, ZiFile::GC};
    ZuCheckRT(tamper);
    if (!tamper) return;
    auto    last = tamper.size() - 1;
    uint8_t c;
    ZuCheckRT(tamper.pread(last, &c, 1) == 1);
    c ^= 1;
    ZuCheckRT(tamper.pwrite(last, &c, 1) == Zi::OK);
  }
  {
    ZiFile input{path, ZiFile::ReadOnly | ZiFile::GC};
    ZuCheckRT(input);
    auto badTag = age.decrypt(input, {&identity, 1}, recovered);
    ZuCheckRT(badTag.template is<ZeException>());
    bool cleared = true;
    for (unsigned i = 0; i < PlainSize; ++i) cleared &= !recovered[i];
    ZuCheckRT(cleared);
  }
}

static void recipientFiles()
{
  ZuTestScopeRT(recipientFiles);
  auto	       path = ZiTestResidue::file("recipient.age");
  Ztls::Random rng;
  ZuCheckRT(rng.init());
  ZtlsAge age;
  uint8_t xSeed[2][Ztls::X25519KeySize];
  uint8_t xPublic[2][Ztls::X25519KeySize];
  uint8_t pqSeed[2][Ztls::PK::HybridSeedSize];
  uint8_t pqPublic[2][Ztls::PK::HybridPublicSize];
  ZuGuard clear{[&xSeed, &pqSeed]()
  {
    ZuClear(xSeed, sizeof(xSeed));
    ZuClear(pqSeed, sizeof(pqSeed));
  }};
  for (unsigned i = 0; i < 2; ++i) {
    ZuCheckRT(rng.random(xSeed[i]));
    ZuCheckRT(rng.random(pqSeed[i]));
    Ztls::PK::X25519Private	    x{xSeed[i]};
    Ztls::PK::MLKEM768X25519Private pq{pqSeed[i]};
    ZuCheckRT(!x.exportPK(xPublic[i]).template is<ZeException>());
    ZuCheckRT(!pq.exportPK(pqPublic[i]).template is<ZeException>());
  }
  uint8_t plain[257], recovered[sizeof(plain)];
  for (unsigned i = 0; i < sizeof(plain); ++i) plain[i] = uint8_t(i * 37);
  ZtlsAge::Recipient recipients[2];
  ZtlsAge::Identity  identities[2];
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
    ZiFile	      input{path, ZiFile::ReadOnly | ZiFile::GC};
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
    ZtlsAge::Identity wrong = profile ? ZtlsAge::Identity{ZtlsAge::HybridIdentity{xSeed[0]}} :
					ZtlsAge::Identity{ZtlsAge::X25519Identity{pqSeed[0]}};
    ZiFile	      wrongInput{path, ZiFile::ReadOnly | ZiFile::GC};
    ZuCheckRT(wrongInput);
    if (!wrongInput) return;
    auto rejected = age.decrypt(wrongInput, {&wrong, 1}, recovered);
    ZuCheckRT(rejected.template is<ZeException>());
    wrongInput.close();
  }
  ZtlsAge::Recipient mixed[2] = {
    ZtlsAge::X25519Recipient{xPublic[0]}, ZtlsAge::HybridRecipient{pqPublic[0]}};
  ZiFile output{path, ZiFile::Write | ZiFile::GC, 0600};
  ZuCheckRT(output);
  if (!output) return;
  ZuCheckRT(age.encrypt(rng, mixed, plain, output).template is<ZeException>());
  ZuCheckRT(output.size() == 0);
}

static void malformedFile()
{
  ZuTestScopeRT(malformedFile);
  auto	       path = ZiTestResidue::file("malformed.age");
  Ztls::Random rng;
  ZuCheckRT(rng.init());
  uint8_t seed[Ztls::X25519KeySize], publicKey[sizeof(seed)];
  ZuGuard clear{[&seed]() { ZuClear(seed, sizeof(seed)); }};
  ZuCheckRT(rng.random(seed));
  Ztls::PK::X25519Private key{seed};
  ZuCheckRT(!key.exportPK(publicKey).template is<ZeException>());
  ZtlsAge::Recipient recipient = ZtlsAge::X25519Recipient{publicKey};
  ZtlsAge::Identity  identity = ZtlsAge::X25519Identity{seed};
  Bytes		     plain, cipher, recovered;
  plain.length((64 << 10) + 17);
  recovered.length(plain.length());
  for (unsigned i = 0, n = plain.length(); i < n; ++i) plain[i] = uint8_t(i * 29);
  ZtlsAge age;
  {
    ZiFile output{path, ZiFile::Write | ZiFile::GC, 0600};
    ZuCheckRT(output);
    ZuCheckRT(!age.encrypt(rng, {&recipient, 1}, plain, output).template is<ZeException>());
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
      while (payload < cipher.length() && cipher[payload] != '\n') ++payload;
      ++payload;
      break;
    }
  }
  ZuCheckRT(payload && payload + 16 + (64 << 10) + 16 < cipher.length());
  struct Case {
    size_t	length;
    size_t	change;
    uint8_t	value;
    bool	clearsChunk;
  };
  // clang-format off
  const Case cases[] = {
    {1, 0, 0, false},
    {cipher.length(), 0, 'X', false},
    {payload, payload - 2, '!', false},
    {payload + 15, 0, 0, false},
    {payload + 16 + (64 << 10) + 16, 0, 0, true},
    {cipher.length() - 1, 0, 0, true},
    {cipher.length(), cipher.length() - 1, uint8_t(cipher[cipher.length() - 1] ^ 1), true}};
  // clang-format on
  for (auto &test : cases) {
    ZiFile output{path, ZiFile::Write | ZiFile::GC, 0600};
    ZuCheckRT(output);
    ZuCheckRT(output.write(cipher.data(), test.length) == Zi::OK);
    if (test.value) ZuCheckRT(output.pwrite(test.change, &test.value, 1) == Zi::OK);
    output.close();
    memset(recovered.data(), 0xa5, recovered.length());
    ZiFile input{path, ZiFile::ReadOnly | ZiFile::GC};
    ZuCheckRT(input);
    ZuCheckRT(age.decrypt(input, {&identity, 1}, recovered).template is<ZeException>());
    if (test.clearsChunk) {
      bool cleared = true;
      for (unsigned i = 0; i < (64 << 10); ++i) cleared &= !recovered[i];
      ZuCheckRT(cleared);
    }
  }
}

static void testkit()
{
  ZuTestScopeRT(testkit);
  uint8_t x[32], pq[32];
  ZuGuard clear{[&]() { ZuClear(x, sizeof(x)); ZuClear(pq, sizeof(pq)); }};
  ZuCheckRT(ZtlsTestKeys::read("alice", x), return);
  ZuCheckRT(ZtlsTestKeys::read("hybrid", pq), return);
  ZtlsAge::Identity identities[] = {ZtlsAge::X25519Identity{x}, ZtlsAge::HybridIdentity{pq},
    ZtlsAge::ScryptIdentity{ZuBSpan{"password"}}};
  enum { X, PQ, Scrypt };
  struct Case { const char *name; unsigned identity; bool success; };
  const Case cases[] = {
    {"x25519", X, true}, {"scrypt", Scrypt, true}, {"hybrid", PQ, true},
    {"hybrid_and_x25519", PQ, true}, {"x25519_grease", X, true},
    {"scrypt_and_x25519", Scrypt, false}, {"hmac_not_canonical", X, false},
    {"x25519_not_canonical_share", X, false}, {"scrypt_work_factor_23", Scrypt, false},
    {"scrypt_not_canonical_salt", Scrypt, false}, {"hybrid_not_canonical_enc", PQ, false},
    {"stream_no_final", X, false}, {"x25519_multiple_recipients", X, true},
    {"x25519_low_order", X, false}, {"hybrid_low_order", PQ, false},
    {"stanza_empty_body", X, true}, {"stanza_empty_last_line", X, true},
    {"stanza_valid_characters", X, true}, {"hybrid_grease", PQ, true}
  };
  for (auto &c : cases) {
    Zi::Path name;
    name << c.name << ".age";
    ZiFile input{ZtlsTestKeys::path(name), ZiFile::ReadOnly | ZiFile::GC};
    ZuCheckRT(input, return);
    Bytes recovered;
    recovered.length(input.size());
    auto result = ZtlsAge{}.decrypt(input, {&identities[c.identity], 1}, recovered);
    ZuCheckRT(result.is<ZeException>() != c.success);
    if (c.success && !result.is<ZeException>())
      ZuCheckRT((ZuCSpan{recovered.data(), result.p<size_t>()} == "test payload"));
  }
}

// Exercise malformed arguments through the public parser. The reference
// testkit above covers unknown stanza types with empty and multiline bodies.
static void stanzaArgs()
{
  ZuTestScopeRT(stanzaArgs);
  Bytes original;
  ZuCheckRT(ZtlsTestKeys::load("x25519.age", original), return);
  unsigned arg = 0;
  while (arg < original.length() && original[arg] != '\n') ++arg;
  arg += sizeof("\n-> X25519 ") - 1;
  ZuCheckRT(arg < original.length());
  if (arg >= original.length()) return;
  uint8_t seed[Ztls::X25519KeySize];
  ZuGuard clear{[&seed]() { ZuClear(seed, sizeof(seed)); }};
  ZuCheckRT(ZtlsTestKeys::read("alice", seed), return);
  ZtlsAge::Identity identity = ZtlsAge::X25519Identity{seed};
  auto		    path = ZiTestResidue::file("stanza-args.age");
  for (unsigned pass = 0; pass < 3; ++pass) {
    Bytes cipher{original};
    if (!pass)
      cipher[arg] = '!';
    else {
      unsigned end = arg;
      while (end < cipher.length() && cipher[end] != '\n') ++end;
      if (pass == 1)
	cipher.splice(arg, end - arg); // missing argument
      else
	cipher.splice(end, 0, ZuBSpan{" extra"});
    }
    ZiFile output{path, ZiFile::Write | ZiFile::GC, 0600};
    ZuCheckRT(output && output.write(cipher.data(), cipher.length()) == Zi::OK);
    output.close();
    Bytes plain;
    plain.length(cipher.length());
    ZiFile input{path, ZiFile::ReadOnly | ZiFile::GC};
    auto   result = ZtlsAge{}.decrypt(input, {&identity, 1}, plain);
    ZuCheckRT(result.template is<ZeException>());
    if (result.template is<ZeException>()) {
      ZeString message;
      message << result.template p<ZeException>();
      ZuCheckRT(strstr(message.data(), "no usable recipient"));
    }
  }
}

static void sshString(Bytes &blob, ZuBSpan bytes, bool mpint = false)
{
  if (mpint)
    while (bytes.length() > 1 && !bytes[0]) bytes.offset(1);
  bool	   sign = mpint && bytes && (bytes[0] & 0x80);
  unsigned n = bytes.length() + unsigned(sign);
  uint8_t  length[4] = {uint8_t(n >> 24), uint8_t(n >> 16), uint8_t(n >> 8), uint8_t(n)};
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
  const auto &recipient = result.template p<ZtlsAge::Recipient>();
  ZuBSpan     parsed = type == "ssh-rsa" ?
    recipient.template p<ZtlsAge::SshRSARecipient>().publicKey :
    recipient.template p<ZtlsAge::SshED25519Recipient>().publicKey;
  if (parsed != blob) return false;
  if (unsigned tail = blob.length() % 3) {
    constexpr char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    unsigned pos = type.length() + 1 + encoded.length() - (4 - tail);
    char     c = line[pos];
    line[pos] = alphabet[ZuBase64::lookup(c) + 1];
    bool rejected = ZtlsAge::decodeSSHRecipient(line, decoded).template is<ZeException>();
    line[pos] = c;
    if (!rejected) return false;
  }
  line[type.length() + 1] = '!';
  return ZtlsAge::decodeSSHRecipient(line, decoded).template is<ZeException>();
}

static void sshRSAFile()
{
  ZuTestScopeRT(sshRSAFile);
  auto	       path = ZiTestResidue::file("ssh-rsa.age");
  Ztls::Random rng;
  ZuCheckRT(rng.init());
  Ztls::PK::RSAPrivate key{rng, 2048};
  uint8_t	       modulus[256], exponent[8];
  ZuCheckRT(Ztls::PK::rsaExportPublic(key.key, modulus, exponent));
  Bytes blob;
  sshString(blob, ZuBSpan{"ssh-rsa"});
  sshString(blob, exponent, true);
  sshString(blob, modulus, true);
  ZuCheckRT(sshLine("ssh-rsa", blob));
  Bytes	  shortBlob;
  uint8_t smallExponent = 3, smallModulus[2] = {1, 1};
  sshString(shortBlob, ZuBSpan{"ssh-rsa"});
  sshString(shortBlob, {&smallExponent, 1});
  sshString(shortBlob, smallModulus);
  ZuCheckRT(sshLine("ssh-rsa", shortBlob));
  ZtlsAge::Recipient recipient = ZtlsAge::SshRSARecipient{blob};
  ZtlsAge::Identity  identity = ZtlsAge::SshRSAIdentity{blob, &key};
  uint8_t	     plain[37], recovered[sizeof(plain)];
  for (unsigned i = 0; i < sizeof(plain); ++i) plain[i] = uint8_t(i * 19);
  ZtlsAge age;
  ZiFile  output{path, ZiFile::Write | ZiFile::GC, 0600};
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
  auto	       path = ZiTestResidue::file("ssh-ed.age");
  Ztls::Random rng;
  ZuCheckRT(rng.init());
  Ztls::PK::Ed25519Private key{rng};
  uint8_t		   publicKey[32];
  ZuCheckRT(!key.exportPK(publicKey).template is<ZeException>());
  Bytes blob;
  sshString(blob, ZuBSpan{"ssh-ed25519"});
  sshString(blob, publicKey);
  ZuCheckRT(sshLine("ssh-ed25519", blob));
  ZtlsAge::Recipient recipient = ZtlsAge::SshED25519Recipient{blob};
  ZtlsAge::Identity  identity = ZtlsAge::SshED25519Identity{blob, &key};
  uint8_t	     plain[37], recovered[sizeof(plain)];
  for (unsigned i = 0; i < sizeof(plain); ++i) plain[i] = uint8_t(i * 23);
  ZtlsAge age;
  ZiFile  output{path, ZiFile::Write | ZiFile::GC, 0600};
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

static void sshReference()
{
  ZuTestScopeRT(sshReference);
  constexpr char plain[] = "test payload";
  ZtlsAge age;
  for (bool rsa : {true, false}) {
    Chars privateKey, publicLine;
    ZuCheckRT(ZtlsTestKeys::load(rsa ? "rsa.openssh" : "ed.openssh", privateKey), return);
    ZuCheckRT(ZtlsTestKeys::load(rsa ? "rsa.pub" : "ed.pub", publicLine), return);
    Bytes publicKey;
    publicKey.length(publicLine.length());
    auto loaded = ZtlsAge::loadOpenSSH(privateKey, publicKey);
    ZuCheckRT(!loaded.template is<ZeException>());
    if (loaded.template is<ZeException>()) return;
    auto key = ZuMv(loaded).template p<ZtlsAge::SshKey>();
    bool cleared = true;
    for (auto c : privateKey) cleared &= !c;
    ZuCheckRT(cleared);
    Bytes blob;
    blob.length(publicKey.length());
    auto decoded = ZtlsAge::decodeSSHRecipient(publicLine, blob);
    ZuCheckRT(!decoded.template is<ZeException>());
    if (decoded.template is<ZeException>()) return;
    const auto &recipient = decoded.template p<ZtlsAge::Recipient>();
    ZuBSpan	expected = rsa ? recipient.template p<ZtlsAge::SshRSARecipient>().publicKey :
				 recipient.template p<ZtlsAge::SshED25519Recipient>().publicKey;
    ZuCheckRT(key.publicKey == expected);
    ZtlsAge::Identity identity;
    if (rsa) {
      auto sk = dynamic_cast<Ztls::PK::RSAPrivate *>(key.key.ptr());
      ZuCheckRT(sk);
      identity = ZtlsAge::SshRSAIdentity{key.publicKey, sk};
    } else {
      auto sk = dynamic_cast<Ztls::PK::Ed25519Private *>(key.key.ptr());
      ZuCheckRT(sk);
      identity = ZtlsAge::SshED25519Identity{key.publicKey, sk};
    }
    ZiFile input{ZtlsTestKeys::path(rsa ? "rsa.age" : "ed.age"), ZiFile::ReadOnly | ZiFile::GC};
    ZuCheckRT(input, return);
    Bytes recovered;
    recovered.length(input.size());
    auto result = age.decrypt(input, {&identity, 1}, recovered);
    ZuCheckRT(!result.template is<ZeException>());
    if (result.template is<ZeException>()) return;
    ZuCheckRT(result.template p<size_t>() == (sizeof(plain) - 1));
    ZuCheckRT(!memcmp(recovered.data(), plain, (sizeof(plain) - 1)));
  }
}

int main()
{
  ZiTestResidue::init("ZtlsAgeTest");
  ZuTestMain();
  ZuCheck(ZtlsTestKeys::init(), return 1);
  ZuTestCall_("nativeKeys", nativeKeys);
  ZuTestCall_("scryptFile", scryptFile);
  ZuTestCall_("recipientFiles", recipientFiles);
  ZuTestCall_("malformedFile", malformedFile);
  ZuTestCall_("testkit", testkit);
  ZuTestCall_("stanzaArgs", stanzaArgs);
  ZuTestCall_("sshRSAFile", sshRSAFile);
  ZuTestCall_("sshEDFile", sshEDFile);
  ZuTestCall_("sshReference", sshReference);
}
