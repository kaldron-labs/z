//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>
#include <openssl/evp.h>
#include <openssl/err.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZtlsAEAD.hh>
#include <zlib/ZtlsEngine.hh>
#include <zlib/ZtlsHMAC.hh>
#include <zlib/ZtlsKDF.hh>
#include <zlib/ZtlsMD.hh>

using namespace ZuTestUtil;

template <typename L> static bool throws(L l)
{
  try {
    l();
  } catch (const ZeException &) {
    return true;
  }
  return false;
}

static bool zero(ZuBSpan bytes)
{
  for (auto byte : bytes)
    if (byte) return false;
  return true;
}

static void unavailable()
{
  ZuTestScope(unavailable);
  // This executable runs alone, before any algorithm singleton can be populated.
  ZuCheck(EVP_set_default_properties(nullptr, "provider=ztls-unavailable") == 1);
  ZuCheck(throws([]() { Ztls::MD<> md; }));
  Ztls::HMAC<> mac;
  ZuCheck(throws([&mac]() { mac.start(ZuBSpan{"key"}); }));
  uint8_t output[Ztls::HMAC<>::Size + 1];
  memset(output, 0x5a, sizeof(output));
  ZuCheck(throws([&mac, &output]() { mac.finish(output); }));
  ZuCheck(zero({output, Ztls::HMAC<>::Size}) && output[Ztls::HMAC<>::Size] == 0x5a);

  Ztls::KDF::HKDF kdf;
  ZuCheck(!kdf.init());
  memset(output, 0x5a, sizeof(output));
  ZuCheck(!kdf.derive({}, ZuBSpan{"input"}, {}, output));
  ZuCheck(zero(output));

  Ztls::ChaCha20Poly1305 aead;
  uint8_t		 key[32]{}, nonce[12]{};
  ZuCheck(!aead.init(true, key));
  memset(output, 0x5a, sizeof(output));
  ZuCheck(!aead.seal(nonce, {}, {}, output));
  ZuCheck(zero({output, 16}));
  ZuCheck(output[16] == 0x5a);
}

static void deferredError()
{
  ZuTestScope(deferredError);
  Ztls::clearTLSError();
  ERR_raise(ERR_LIB_EVP, ERR_R_INTERNAL_ERROR);
  char expected[256];
  ERR_error_string_n(ERR_peek_last_error(), expected, sizeof(expected));
  ZeException error;
  {
    ZeString operation{"certificate verification"};
    error = Ztls::tlsError(operation.data(), Ztls_ErrorLibrary);
  }
  ZuCheck(error.file == ZuCSpan{__FILE__});
  ZuCheck(error.function == "deferredError"_z);
  Ztls::clearTLSError();
  ERR_raise(ERR_LIB_RSA, ERR_R_PASSED_INVALID_ARGUMENT);
  ZeString message;
  message << error;
  ZuCheck(strstr(message.data(), "certificate verification"));
  ZuCheck(strstr(message.data(), expected));
  Ztls::clearTLSError();
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(deferredError);
  ZuTestCall(unavailable);
  return 0;
}
