//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zrestauth_HH
#define zrestauth_HH

#include <zlib/ZmHeap.hh>
#include <zlib/ZmObject.hh>
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

template <typename Heap>
struct Credentials_ : public Heap, public ZmObject {
  CredString username;
  CredString password;
};
using Credentials_Heap = ZmHeap<"zrest.Credentials", Credentials_<ZuVoid>>;
ZuDerive(Credentials, (Credentials_<Credentials_Heap>));

template <typename Heap>
struct RefreshRequest_ : public Heap, public ZmObject {
  TokenString refreshToken;
};
using RefreshRequest_Heap =
  ZmHeap<"zrest.RefreshRequest", RefreshRequest_<ZuVoid>>;
ZuDerive(RefreshRequest, (RefreshRequest_<RefreshRequest_Heap>));

template <typename Heap>
struct TokenResponse_ : public Heap, public ZmObject {
  TokenString accessToken;
  TokenString refreshToken;
  uint64_t expiresIn = 0;
};
using TokenResponse_Heap =
  ZmHeap<"zrest.TokenResponse", TokenResponse_<ZuVoid>>;
ZuDerive(TokenResponse, (TokenResponse_<TokenResponse_Heap>));

template <typename Heap>
struct Unauthorized_ : public Heap, public ZmObject { };
using Unauthorized_Heap =
  ZmHeap<"zrest.Unauthorized", Unauthorized_<ZuVoid>>;
ZuDerive(Unauthorized, (Unauthorized_<Unauthorized_Heap>));

template <typename Heap>
struct InternalError_ : public Heap, public ZmObject { };
using InternalError_Heap =
  ZmHeap<"zrest.InternalError", InternalError_<ZuVoid>>;
ZuDerive(InternalError, (InternalError_<InternalError_Heap>));

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
