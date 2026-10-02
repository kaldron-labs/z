//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Reference-age interoperability driver; not an ordinary TAP test.

#include <string.h>

#include <iostream>

#include <zlib/ZtArray.hh>
#include <zlib/ZtlsAge.hh>
#include <zlib/ZtlsKEM.hh>

using Bytes = ZtBArray<ZtArrayHeapID<"Ztls.Age.Interop">>;

static bool readFile(ZuCSpan path, Bytes &bytes)
{
  ZiFile file{path, ZiFile::ReadOnly | ZiFile::GC};
  if (!file) return false;
  auto n = file.size();
  if (n < 0 || n > (64 << 20)) return false;
  bytes.length(n);
  return file.read(bytes.data(), n) == n;
}

static void trimLine(Bytes &bytes)
{
  unsigned n = bytes.length();
  while (n && (bytes[n - 1] == '\n' || bytes[n - 1] == '\r')) --n;
  bytes.length(n);
}

static ZuCSpan keyLine(const Bytes &bytes, bool recipient)
{
  unsigned cursor = 0, n = bytes.length();
  while (cursor < n) {
    unsigned start = cursor;
    while (cursor < n && bytes[cursor] != '\n') ++cursor;
    unsigned end = cursor;
    if (end > start && bytes[end - 1] == '\r') --end;
    ZuCSpan line{bytes.data() + start, end - start};
    if (recipient ? line.prefix("age1"_z) == 4 :
        line.prefix("AGE-SECRET-KEY-"_z) == 15) return line;
    if (cursor < n) ++cursor;
  }
  return {};
}

static int run(int argc, char **argv)
{
  if (argc != 6 && argc != 7) return 2;
  bool encrypt = !strcmp(argv[1], "encrypt");
  if (!encrypt && strcmp(argv[1], "decrypt")) return 2;
  bool x25519 = !strcmp(argv[2], "x25519");
  bool hybrid = !strcmp(argv[2], "hybrid");
  bool scrypt = !strcmp(argv[2], "scrypt");
  bool sshRSA = !strcmp(argv[2], "ssh-rsa");
  bool sshED = !strcmp(argv[2], "ssh-ed25519");
  bool ssh = sshRSA || sshED;
  if (!x25519 && !hybrid && !scrypt && !ssh) return 2;
  if (argc != (ssh && !encrypt ? 7 : 6)) return 2;
  Bytes keyText;
  if (!readFile(ssh && !encrypt ? argv[6] : argv[3], keyText)) return 1;
  if (scrypt) trimLine(keyText);
  uint8_t key[Ztls::PK::HybridPublicSize];
  ZuGuard clear{[&key, &keyText]() {
    ZuClear(key, sizeof(key));
    ZuClear(keyText);
  }};
  unsigned keySize = x25519 ? 32 :
    encrypt ? Ztls::PK::HybridPublicSize : Ztls::PK::HybridSeedSize;
  ZtlsAge::Recipient sshRecipient;
  if (ssh) {
    auto decoded = ZtlsAge::decodeSSHRecipient(keyText,
      {keyText.data(), keyText.length()});
    if (decoded.template is<ZeException>()) return 1;
    sshRecipient = ZuMv(decoded).template p<ZtlsAge::Recipient>();
    if (sshRSA != sshRecipient.template is<ZtlsAge::SshRSARecipient>() ||
        sshED != sshRecipient.template is<ZtlsAge::SshED25519Recipient>())
      return 1;
  } else if (!scrypt) {
    auto line = keyLine(keyText, encrypt);
    if (!line) return 1;
    auto type = x25519 ? ZtlsAgeKeyType::X25519 : ZtlsAgeKeyType::Hybrid;
    auto decoded = encrypt ?
      ZtlsAge::decodeRecipient(type, line, {key, keySize}) :
      ZtlsAge::decodeIdentity(type, line, {key, keySize});
    if (decoded.template is<ZeException>() ||
        decoded.template p<size_t>() != keySize) return 1;
  }
  ZtlsAge age;
  if (encrypt) {
    Bytes plaintext;
    if (!readFile(argv[4], plaintext)) return 1;
    ZtlsAge::Recipient recipient = scrypt ?
      ZtlsAge::Recipient{ZtlsAge::ScryptRecipient{keyText}} :
      ssh ? ZuMv(sshRecipient) : x25519 ?
        ZtlsAge::Recipient{ZtlsAge::X25519Recipient{{key, keySize}}} :
        ZtlsAge::Recipient{ZtlsAge::HybridRecipient{{key, keySize}}};
    ZiFile output{argv[5], ZiFile::Write | ZiFile::GC, 0600};
    if (!output) return 1;
    Ztls::Random rng;
    if (!rng.init()) return 1;
    return age.encrypt(rng, {&recipient, 1}, plaintext, output)
      .template is<ZeException>() ? 1 : 0;
  }
  ZiFile input{argv[4], ZiFile::ReadOnly | ZiFile::GC};
  if (!input) return 1;
  auto n = input.size();
  if (n < 0 || n > (64 << 20)) return 1;
  Bytes plaintext;
  plaintext.length(n);
  Ztls::Random rng;
  if (!rng.init()) return 1;
  ZmRef<Ztls::PK::AnyKey> privateKey;
  if (ssh) {
    Ztls::PK::LoadSK loader{rng};
    auto loaded = loader.loadFile(argv[3]);
    if (!loaded.template is<ZeException>())
      privateKey = ZuMv(loaded).template p<ZmRef<Ztls::PK::AnyKey>>();
    else {
      Bytes privateText, embedded;
      if (!readFile(argv[3], privateText)) return 1;
      embedded.length(4096);
      auto decoded = ZtlsAge::loadOpenSSH(
        {privateText.data(), privateText.length()},
        {embedded.data(), embedded.length()});
      if (decoded.template is<ZeException>()) return 1;
      auto key = ZuMv(decoded).template p<ZtlsAge::SshKey>();
      ZuBSpan expected = sshRSA ?
        sshRecipient.template p<ZtlsAge::SshRSARecipient>().publicKey :
        sshRecipient.template p<ZtlsAge::SshED25519Recipient>().publicKey;
      if (key.publicKey != expected) return 1;
      privateKey = ZuMv(key.key);
    }
  }
  ZtlsAge::Identity identity = scrypt ?
    ZtlsAge::Identity{ZtlsAge::ScryptIdentity{keyText}} :
    sshRSA ? ZtlsAge::Identity{ZtlsAge::SshRSAIdentity{
      sshRecipient.template p<ZtlsAge::SshRSARecipient>().publicKey,
      dynamic_cast<Ztls::PK::SK_RSA *>(privateKey.ptr())}} :
    sshED ? ZtlsAge::Identity{ZtlsAge::SshED25519Identity{
      sshRecipient.template p<ZtlsAge::SshED25519Recipient>().publicKey,
      dynamic_cast<Ztls::PK::SK_ED25519 *>(privateKey.ptr())}} :
    x25519 ?
      ZtlsAge::Identity{ZtlsAge::X25519Identity{{key, keySize}}} :
      ZtlsAge::Identity{ZtlsAge::HybridIdentity{{key, keySize}}};
  auto decrypted = age.decrypt(input, {&identity, 1},
    {plaintext.data(), plaintext.length()});
  if (decrypted.template is<ZeException>()) return 1;
  ZiFile output{argv[5], ZiFile::Write | ZiFile::GC, 0600};
  if (!output) return 1;
  return output.write(plaintext.data(),
    decrypted.template p<size_t>()) == Zi::OK ? 0 : 1;
}

int main(int argc, char **argv)
{
  int r = run(argc, argv);
  if (r) std::cerr << "Usage: ZtlsAgeInterop encrypt|decrypt "
    "x25519|hybrid|scrypt|ssh-rsa|ssh-ed25519 "
    "key-file input-file output-file [ssh-public-file]\n";
  return r;
}
