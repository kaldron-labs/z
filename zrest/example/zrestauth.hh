//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zrestauth_HH
#define zrestauth_HH

#include <new>

#include <zlib/ZmObject.hh>
#include <zlib/ZmVHeap.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZfJSON.hh>
#include <zlib/ZfURI.hh>

enum {
  ReqBodyMax = 1U<<20,
  RespBodyMax = 1U<<20,
  JWTPartMax = 4U<<10,
  JWTScratchSize = 512,
  JWTMax = JWTPartMax * 3 + 2
};

enum {
  AccessLifetimeDefault = 5 * 60,
  RefreshLifetimeDefault = 24 * 60 * 60
};

using AuthPath = ZuStringT<"/api/auth">;
using RefreshPath = ZuStringT<"/api/refresh">;
using DefaultUser = ZuStringT<"test">;
using DefaultPass = ZuStringT<"test123">;
using DefaultJWTSecret = ZuStringT<"your_secret_key">;
using DefaultAccessLifetime = ZuStringT<"5m">;
using DefaultRefreshLifetime = ZuStringT<"24h">;

ZuDerive(CredString, (ZtString<ZtStringHeapID<"zrest.CredString">>));
ZuDerive(TokenString, (ZtString<ZtStringHeapID<"zrest.TokenString">>));

class ExampleObject {
  using Heap = ZmVHeap<"zrest.Object", Zm::CacheLineSize,
    ZmVHeap_DefltMax, Zm::CacheLineSize>;

public:
  static void *operator new(size_t size) {
    if (void *ptr = Heap::valloc(size)) return ptr;
    throw std::bad_alloc{};
  }
  static void operator delete(void *ptr) noexcept { Heap::vfree(ptr); }
  static void operator delete(void *ptr, size_t) noexcept { Heap::vfree(ptr); }
};

struct Credentials : public ExampleObject, public ZmObject {
  CredString username;
  CredString password;
};
struct RefreshRequest : public ExampleObject, public ZmObject {
  TokenString refreshToken;
};
struct TokenResponse : public ExampleObject, public ZmObject {
  TokenString accessToken;
  TokenString refreshToken;
  uint64_t expiresIn = 0;
};
struct Unauthorized : public ExampleObject, public ZmObject { };
struct InternalError : public ExampleObject, public ZmObject { };

ZfStruct((Credentials, JSON),
  (((username), (Required)), (String)),
  (((password), (Required)), (String)));
ZfStruct((RefreshRequest, JSON),
  (((refreshToken), (JSON::ID<"refresh_token">, Required)), (String)));
ZfStruct((TokenResponse, JSON),
  (((accessToken), (JSON::ID<"access_token">, Required)), (String)),
  (((refreshToken), (JSON::ID<"refresh_token">, Required)), (String)),
  (((expiresIn), (JSON::ID<"expires_in">, Required)), (UInt64)));

inline bool parseDuration(ZuCSpan value, bool allowZero, uint64_t &seconds)
{
  unsigned length = value.length();
  if (length < 2) return false;
  uint64_t n = 0;
  for (unsigned i = 0; i + 1 < length; ++i) {
    unsigned digit = unsigned(uint8_t(value[i]) - uint8_t('0'));
    if (digit > 9 || n > (UINT64_MAX - digit) / 10) return false;
    n = n * 10 + digit;
  }
  uint64_t multiplier;
  switch (value[length - 1]) {
    case 's': multiplier = 1; break;
    case 'm': multiplier = 60; break;
    case 'h': multiplier = 60 * 60; break;
    default: return false;
  }
  if ((!n && !allowZero) || n > UINT64_MAX / multiplier) return false;
  seconds = n * multiplier;
  return true;
}

#endif /* zrestauth_HH */
