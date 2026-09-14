//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_secret.hh>

#include <limits.h>
#include <openssl/evp.h>

#include <zlib/ZtlsHMAC.hh>
#include <zlib/ZtlsMD.hh>

namespace Zum {

Bytes serverKeyCheck(ZuBSpan key)
{
  const ZuBSpan input{"zum.db.key.v1"};
  if (key.length() != 32) return {};
  Bytes digest;
  digest.length(Ztls::HMAC<>::Size, false);
  Ztls::HMAC<> hmac;
  hmac.start(key);
  hmac.update(input);
  hmac.finish(digest);
  return digest;
}

static Bytes encryptSecret(
    Ztls::Random &rng, ZuBSpan key, ZuBSpan aad, ZuBSpan plain)
{
  enum { Version = 1, AES256GCM = 1, KeyID = 1,
    Header = 6, Nonce = 12, Tag = 16 };
  if (key.length() != 32 || plain.length() > unsigned(INT_MAX) ||
      aad.length() > unsigned(INT_MAX)) return {};
  Bytes envelope;
  envelope.length(Header + Nonce + plain.length() + Tag, false);
  auto out = envelope.data();
  out[0] = Version;
  out[1] = AES256GCM;
  out[2] = uint8_t(KeyID >> 24);
  out[3] = uint8_t(KeyID >> 16);
  out[4] = uint8_t(KeyID >> 8);
  out[5] = uint8_t(KeyID);
  if (!rng.random({out + Header, Nonce})) return {};
  EVP_CIPHER_CTX *cipher = EVP_CIPHER_CTX_new();
  if (!cipher) return {};
  int n = 0, offset = 0;
  bool ok = EVP_EncryptInit_ex(
      cipher, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
    EVP_CIPHER_CTX_ctrl(cipher, EVP_CTRL_GCM_SET_IVLEN, Nonce, nullptr) == 1 &&
    EVP_EncryptInit_ex(
      cipher, nullptr, nullptr, key.data(), out + Header) == 1 &&
    (!aad || EVP_EncryptUpdate(cipher, nullptr, &n,
      aad.data(), int(aad.length())) == 1) &&
    (!plain || EVP_EncryptUpdate(cipher, out + Header + Nonce, &n,
      plain.data(), int(plain.length())) == 1);
  if (ok && plain) offset = n;
  ok = ok && EVP_EncryptFinal_ex(
    cipher, out + Header + Nonce + offset, &n) == 1;
  if (ok) offset += n;
  ok = ok && unsigned(offset) == plain.length() &&
    EVP_CIPHER_CTX_ctrl(cipher, EVP_CTRL_GCM_GET_TAG, Tag,
      out + Header + Nonce + plain.length()) == 1;
  EVP_CIPHER_CTX_free(cipher);
  if (!ok) {
    ZuClear(envelope.data(), envelope.length());
    return {};
  }
  return envelope;
}

static String secretAAD(
    ZuCSpan issuer, ZuCSpan recordType, ZuCSpan recordID, ZuCSpan field)
{
  String aad;
  aad << issuer << '\0' << recordType << '\0' << recordID << '\0' << field;
  return aad;
}

bool serverSecretDecrypt(
    ZuBSpan key, ZuCSpan issuer, ZuCSpan recordType, ZuCSpan recordID,
    ZuCSpan field, ZuBSpan envelope, Bytes &plain)
{
  enum { Version = 1, AES256GCM = 1, KeyID = 1,
    Header = 6, Nonce = 12, Tag = 16 };
  plain.null();
  if (key.length() != 32 || envelope.length() < Header + Nonce + Tag ||
      envelope[0] != Version || envelope[1] != AES256GCM || envelope[2] ||
      envelope[3] || envelope[4] || envelope[5] != KeyID) return false;
  auto length = envelope.length() - Header - Nonce - Tag;
  if (length > unsigned(INT_MAX)) return false;
  auto aad = secretAAD(issuer, recordType, recordID, field);
  ZuBSpan aadBytes{aad};
  if (aad.length() > unsigned(INT_MAX)) return false;
  Bytes next;
  next.length(length, false);
  EVP_CIPHER_CTX *cipher = EVP_CIPHER_CTX_new();
  if (!cipher) return false;
  int n = 0, offset = 0;
  bool ok = EVP_DecryptInit_ex(
      cipher, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
    EVP_CIPHER_CTX_ctrl(cipher, EVP_CTRL_GCM_SET_IVLEN, Nonce, nullptr) == 1 &&
    EVP_DecryptInit_ex(cipher, nullptr, nullptr, key.data(),
      envelope.data() + Header) == 1 &&
    (!aad || EVP_DecryptUpdate(cipher, nullptr, &n,
      aadBytes.data(), int(aadBytes.length())) == 1) &&
    (!length || EVP_DecryptUpdate(cipher, next.data(), &n,
      envelope.data() + Header + Nonce, int(length)) == 1);
  if (ok && length) offset = n;
  ok = ok && EVP_CIPHER_CTX_ctrl(cipher, EVP_CTRL_GCM_SET_TAG, Tag,
      const_cast<uint8_t *>(envelope.data()) + Header + Nonce + length) == 1 &&
    EVP_DecryptFinal_ex(cipher, next.data() + offset, &n) == 1;
  if (ok) offset += n;
  EVP_CIPHER_CTX_free(cipher);
  if (!ok || unsigned(offset) != length) {
    if (next) ZuClear(next.data(), next.length());
    return false;
  }
  plain = ZuMv(next);
  return true;
}

bool serverSecretEncrypt(
    Ztls::Random &rng, ZuBSpan key, ZuCSpan issuer, ZuCSpan recordType,
    ZuCSpan recordID, ZuCSpan field, ZuBSpan plain, Bytes &envelope)
{
  String aad = secretAAD(issuer, recordType, recordID, field);
  Bytes next = encryptSecret(rng, key, aad, plain);
  if (!next) return false;
  envelope = ZuMv(next);
  return true;
}

bool serverSecretRekey(
    Ztls::Random &rng, ZuBSpan oldKey, ZuBSpan newKey, ZuCSpan issuer,
    ZuCSpan recordType, ZuCSpan recordID, ZuCSpan field, Bytes &envelope)
{
  if (oldKey.length() != 32 || newKey.length() != 32) return false;
  Bytes plain;
  if (serverSecretDecrypt(newKey, issuer, recordType, recordID, field,
      envelope, plain)) {
    if (plain) ZuClear(plain.data(), plain.length());
    return true;
  }
  if (!serverSecretDecrypt(oldKey, issuer, recordType, recordID, field,
      envelope, plain)) return false;
  bool ok = serverSecretEncrypt(rng, newKey, issuer, recordType, recordID,
    field, plain, envelope);
  if (plain) ZuClear(plain.data(), plain.length());
  return ok;
}

} // namespace Zum
