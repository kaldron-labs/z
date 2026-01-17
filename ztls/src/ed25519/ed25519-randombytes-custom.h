//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <picotls/picotls.h>

#include <zlib/ZtlsBackendPico.hh>

void ED25519_FN(ed25519_randombytes_unsafe) (void *p, size_t len)
{
  if (len) ptls_openssl_random_bytes(p, len);
}
