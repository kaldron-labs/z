//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// ed25519-donna C++ wrapper

#ifndef Ztls_ED25519_H
#define Ztls_ED25519_H

#ifdef _WIN32

#ifdef ZTLS_EXPORTS
#define ZtlsAPI __declspec(dllexport)
#define ZtlsExplicit
#else
#define ZtlsAPI __declspec(dllimport)
#define ZtlsExplicit extern
#endif
#define ZtlsExtern extern ZtlsAPI

#else

#define ZtlsAPI
#define ZtlsExplicit
#define ZtlsExtern extern

#endif

#include <stdlib.h>

#if defined(__cplusplus)
extern "C" {
#endif

typedef unsigned char ed25519_signature[64];
typedef unsigned char ed25519_public_key[32];
typedef unsigned char ed25519_secret_key[32];

// typedef unsigned char curved25519_key[32];

ZtlsExtern void ed25519_publickey(
  const ed25519_secret_key sk, ed25519_public_key pk);
ZtlsExtern int ed25519_sign_open(
  const unsigned char *m, size_t mlen,
  const ed25519_public_key pk, const ed25519_signature RS);
ZtlsExtern void ed25519_sign(
  const unsigned char *m, size_t mlen,
  const ed25519_secret_key sk, const ed25519_public_key pk,
  ed25519_signature RS);

// below functions are unused by Ztls
#if 0
ZtlsExtern int ed25519_sign_open_batch(
  const unsigned char **m, size_t *mlen,
  const unsigned char **pk, const unsigned char **RS,
  size_t num, int *valid);

ZtlsExtern void ed25519_randombytes_unsafe(void *out, size_t count);

ZtlsExtern void curved25519_scalarmult_basepoint(
  curved25519_key pk, const curved25519_key e);
#endif

#if defined(__cplusplus)
}
#endif

#endif /* Ztls_ED25519_H */
