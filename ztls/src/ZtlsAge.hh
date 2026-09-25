//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// age v1 binary file format and native key text

#ifndef ZtlsAge_HH
#define ZtlsAge_HH

#ifndef ZtlsLib_HH
#include <zlib/ZtlsLib.hh>
#endif

#include <zlib/ZuSpan.hh>
#include <zlib/ZuUnion.hh>

#include <zlib/ZtEnum.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZiFile.hh>

#include <zlib/ZtlsRandom.hh>
#include <zlib/ZtlsPK.hh>

namespace Ztls_ {

ZtEnumStruct(ZtlsAPI, AgeKeyType, int8_t, X25519, Hybrid);

class ZtlsAPI Age {
public:
  using KeyType = AgeKeyType::T;

  struct X25519Recipient { ZuBSpan key; };
  struct HybridRecipient { ZuBSpan key; };
  struct ScryptRecipient { ZuBSpan passphrase; };
  struct SshRSARecipient { ZuBSpan publicKey; };
  struct SshED25519Recipient { ZuBSpan publicKey; };
  using Recipient = ZuUnion<
    X25519Recipient, HybridRecipient, ScryptRecipient,
    SshRSARecipient, SshED25519Recipient>;

  struct X25519Identity { ZuBSpan seed; };
  struct HybridIdentity { ZuBSpan seed; };
  struct ScryptIdentity { ZuBSpan passphrase; };
  struct SshRSAIdentity {
    ZuBSpan publicKey;
    const Ztls::PK::SK_RSA *key;
  };
  struct SshED25519Identity {
    ZuBSpan publicKey;
    const Ztls::PK::SK_ED25519 *key;
  };
  using Identity = ZuUnion<
    X25519Identity, HybridIdentity, ScryptIdentity,
    SshRSAIdentity, SshED25519Identity>;

  // The native key text format is Bech32, not ordinary base32. These
  // functions return the number of bytes written, without a terminator.
  static ZuUnion<size_t, ZeException>
  encodeRecipient(KeyType, ZuBSpan key, ZuSpan<char> output);

  static ZuUnion<size_t, ZeException>
  decodeRecipient(KeyType, ZuCSpan text, ZuSpan<uint8_t> output);

  static ZuUnion<size_t, ZeException>
  encodeIdentity(KeyType, ZuBSpan key, ZuSpan<char> output);

  static ZuUnion<size_t, ZeException>
  decodeIdentity(KeyType, ZuCSpan text, ZuSpan<uint8_t> output);

  static ZuUnion<Recipient, ZeException>
  decodeSSHRecipient(ZuCSpan authorizedKey, ZuSpan<uint8_t> output);

  // Parses an unencrypted OpenSSH private key in place. Clears privateKey
  // on every exit; the returned publicKey span borrows caller storage.
  struct SshKey {
    ZmRef<Ztls::PK::AnyKey> key;
    ZuBSpan publicKey;
  };
  static ZuUnion<SshKey, ZeException>
  loadOpenSSH(ZuSpan<char> privateKey, ZuSpan<uint8_t> publicKey);

  ZuUnion<void, ZeException>
  encrypt(Ztls::Random &, ZuSpan<const Recipient>, ZuBSpan plaintext, ZiFile &);
  ZuUnion<size_t, ZeException>
  decrypt(ZiFile &, ZuSpan<const Identity>, ZuSpan<uint8_t> plaintext);
};

} // Ztls_

using ZtlsAge = Ztls_::Age;
using ZtlsAgeKeyType = Ztls_::AgeKeyType;

#endif /* ZtlsAge_HH */
