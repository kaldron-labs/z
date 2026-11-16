//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

#include <zlib/ZmTime.hh>
#include <zlib/ZutcpISN.hh>

namespace Zutcp {

ISN::~ISN()
{
  OPENSSL_cleanse(m_secret, sizeof(m_secret));
}

bool ISN::init(unsigned interface)
{
  return init(interface, Zm::now(), RAND_priv_bytes);
}

bool ISN::init(unsigned interface, ZuTime origin, int (*random)(uint8_t *, int))
{
  if (*m_origin || !*origin || !interface) return false;
  uint8_t secret[SecretSize];
  bool ok = random(secret, sizeof(secret)) == 1 &&
    init({secret, sizeof(secret)}, origin, interface);
  OPENSSL_cleanse(secret, sizeof(secret));
  return ok;
}

bool ISN::init(ZuBSpan secret, ZuTime origin, unsigned interface)
{
  if (*m_origin || secret.length() != SecretSize || !*origin || !interface) return false;
  memcpy(m_secret, secret.data(), SecretSize);
  m_origin = origin;
  m_interface = interface;
  return true;
}

bool ISN::encode(const ZiSockAddr &local, const ZiSockAddr &remote, uint8_t *out) const
{
  if (local.type() != remote.type() || !local.port() || !remote.port() ||
      (local.type() != ZiIPType::V4 && local.type() != ZiIPType::V6)) return false;
  memcpy(out, "Zutcp.ISN.v1", 12);
  out[12] = uint8_t(m_interface>>24);
  out[13] = uint8_t(m_interface>>16);
  out[14] = uint8_t(m_interface>>8);
  out[15] = uint8_t(m_interface);
  out[16] = local.type() == ZiIPType::V4 ? 4 : 6;
  if (local.type() == ZiIPType::V4) {
    memset(out + 17, 0, 12);
    memcpy(out + 29, &local.sin().sin_addr, 4);
    memset(out + 35, 0, 12);
    memcpy(out + 47, &remote.sin().sin_addr, 4);
  } else {
    memcpy(out + 17, &local.sin6().sin6_addr, 16);
    memcpy(out + 35, &remote.sin6().sin6_addr, 16);
  }
  out[33] = uint8_t(local.port()>>8);
  out[34] = uint8_t(local.port());
  out[51] = uint8_t(remote.port()>>8);
  out[52] = uint8_t(remote.port());
  return true;
}

bool ISN::generate(const ZiSockAddr &local, const ZiSockAddr &remote, uint32_t &out) const
{
  return generate(local, remote, Zm::now(), out);
}

bool ISN::generate(
  const ZiSockAddr &local, const ZiSockAddr &remote, ZuTime now, uint32_t &out) const
{
  if (!*m_origin || !*now) return false;
  uint8_t input[InputSize], digest[SecretSize];
  if (!encode(local, remote, input)) return false;
  unsigned length = 0;
  if (!HMAC(EVP_sha256(), m_secret, SecretSize, input, sizeof(input), digest, &length) ||
      length != sizeof(digest)) return false;
  uint32_t prf = (uint32_t(digest[0])<<24) | (uint32_t(digest[1])<<16) |
    (uint32_t(digest[2])<<8) | uint32_t(digest[3]);
  // The port uses the existing Zm time base, including its realtime semantics.
  out = prf + uint32_t((now - m_origin).microsecs()/4);
  return true;
}

} // namespace Zutcp
