//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

#include <mbedtls/md.h>

typedef mbedtls_md_context_t ed25519_hash_context;

void ed25519_hash_init(ed25519_hash_context *ctx)
{
  mbedtls_md_init(ctx);
  mbedtls_md_setup(ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA512), 0);
  mbedtls_md_starts(ctx);
}

void ed25519_hash_update(
  ed25519_hash_context *ctx, const uint8_t *in, size_t inlen)
{
  mbedtls_md_update(ctx, in, inlen);
}

void ed25519_hash_final(ed25519_hash_context *ctx, uint8_t *hash)
{
  mbedtls_md_finish(ctx, hash);
  mbedtls_md_free(ctx);
}

void ed25519_hash(uint8_t *hash, const uint8_t *in, size_t inlen)
{
  mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA512), in, inlen, hash);
}
