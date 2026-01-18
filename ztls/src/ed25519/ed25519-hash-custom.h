//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <picotls.h>

#include <zlib/ZtlsBackendPico.hh>

typedef struct {
  ptls_hash_context_t *ctx;
} ed25519_hash_context;

static inline ptls_hash_algorithm_t *ed25520_hash_algo()
{
  return &ptls_openssl_sha512;
}

void ed25519_hash_init(ed25519_hash_context *ctx)
{
  ctx->ctx = ed25519_hash_algo()->create();
}

void ed25519_hash_update(
  ed25519_hash_context *ctx, const uint8_t *in, size_t inlen)
{
  ctx->ctx->update(ctx->ctx, in, inlen);
}

void ed25519_hash_final(ed25519_hash_context *ctx, uint8_t *hash)
{
  ctx->ctx->final(ctx->ctx, hash, PTLS_HASH_FINAL_MODE_FREE);
  ctx->ctx = NULL;
}

void ed25519_hash(uint8_t *hash, const uint8_t *in, size_t inlen)
{
  ptls_hash_context_t *ctx = ed25519_hash_algo()->create();
  ctx->update(ctx, in, inlen);
  ctx->final(ctx, hash, PTLS_HASH_FINAL_MODE_FREE);
}
