//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// security-sensitive byte comparisons

#include <zlib/ZtlsSec.hh>

#include <openssl/evp.h>
#include <zlib/ZtlsRandom.hh>

namespace Ztls {

bool ctEqual(ZuBSpan l, ZuBSpan r)
{
  unsigned n = l.length();
  if (n != r.length()) return false;
  uint8_t v = 0;
  for (unsigned i = 0; i < n; ++i) v |= l[i] ^ r[i];
  return !v;
}

static bool secretHash_(ZuBSpan secret, ZuBSpan salt, ZuSpan<uint8_t> digest)
{
  // RFC 7914 scrypt; this fixed profile uses about 32MiB per operation.
  enum { R = 8, P = 1 };
  constexpr uint64_t N = 1U<<15;
  constexpr uint64_t MaxMem = 64U<<20;
  return EVP_PBE_scrypt(
    reinterpret_cast<const char *>(secret.data()), secret.length(),
    salt.data(), salt.length(), N, R, P, MaxMem,
    digest.data(), SecretHash::DigestSize) == 1;
}

bool secretHash(Random &rng, ZuBSpan secret, ZuSpan<uint8_t> output)
{
  if (output.length() < SecretHash::Size) return false;
  output[0] = SecretHash::Version;
  ZuSpan<uint8_t> salt{output.data() + 1, SecretHash::SaltSize};
  ZuSpan<uint8_t> digest{
    output.data() + 1 + SecretHash::SaltSize, SecretHash::DigestSize};
  if (rng.random(salt) && secretHash_(secret, salt, digest)) return true;
  ZuClear(output.data(), SecretHash::Size);
  return false;
}

bool secretVerify(ZuBSpan stored, ZuBSpan secret)
{
  if (stored.length() != SecretHash::Size ||
      stored[0] != SecretHash::Version) return false;
  ZuBSpan salt{stored.data() + 1, SecretHash::SaltSize};
  ZuBSpan digest{
    stored.data() + 1 + SecretHash::SaltSize, SecretHash::DigestSize};
  uint8_t candidate[SecretHash::DigestSize];
  bool ok = secretHash_(secret, salt, candidate) &&
    ctEqual(candidate, digest);
  ZuClear(candidate, sizeof(candidate));
  return ok;
}

}
