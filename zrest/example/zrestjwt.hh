//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zrestjwt_HH
#define zrestjwt_HH

#include <zlib/ZtEnum.hh>

#include <zlib/ZtlsRandom.hh>

#include "zrestproto.hh"

struct TokenType {
  using T = int8_t;
  enum { Invalid = -1, access, refresh, N };
  ZtEnumNames(, TokenType, access, refresh);
  struct Map : public Map_ { };
};

bool jwtIssuePair(
  Ztls::Random &, ZuCSpan secret, ZuCSpan subject, int64_t now,
  uint64_t accessSecs, uint64_t refreshSecs, TokenResponse &);
bool jwtValidate(
  ZuCSpan secret, ZuCSpan token, TokenType::T requiredType,
  int64_t now, CredString &subject);

#endif /* zrestjwt_HH */
