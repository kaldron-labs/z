//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Shared protocol for the zrest example programs

#ifndef zrestproto_HH
#define zrestproto_HH

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
using PingPath = ZuStringT<"/">;
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

struct Ping : public ExampleObject, public ZmObject {
  bool ping = false;
};

struct Pong : public ExampleObject, public ZmObject {
  bool pong = false;
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
ZfStruct((Ping, URI),
  (((ping), (Required)), (Bool)));
ZfStruct((Pong, JSON),
  (((pong), (Required)), (Bool)));

bool parseDuration(ZuCSpan, bool allowZero, uint64_t &seconds);

#endif /* zrestproto_HH */
