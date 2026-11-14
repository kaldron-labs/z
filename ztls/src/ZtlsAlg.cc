//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZtlsAlg.hh>

#include <openssl/evp.h>

namespace Ztls {

ZtEnumImplNS(MDAlg);

const EVP_MD *evpMD(MDType type)
{
  switch (type) {
    case SHA1: return EVP_sha1();
    case SHA256: return EVP_sha256();
    case SHA384: return EVP_sha384();
    case SHA512: return EVP_sha512();
  }
  return EVP_sha256();
}

} // Ztls
