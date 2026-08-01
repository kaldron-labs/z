//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuMatcher.hh>
#include <zlib/ZuSwitch.hh>

#include <zlib/ZtlsPK.hh>

namespace Ztls::PK {

namespace Load_ {

int keyType(ZuCSpan id)
{
  static constexpr auto matcher = ZuMatcher<
    OIDs::PKCS1_RSA,			// RSA
    OIDs::EC_ALG_UNRESTRICTED,		// EC
    OIDs::ED25519				// ED25519
  >();
  return matcher.exact(id);
}

} // namespace Load_

namespace Data {

int pemHead(ZuCSpan span, unsigned &offset, unsigned &length)
{
  static constexpr auto matcher = ZuMatcher<
    "-----BEGIN PRIVATE KEY-----",	// must line up with Type above
    "-----BEGIN EC PRIVATE KEY-----",
    "-----BEGIN RSA PRIVATE KEY-----",
    "-----BEGIN PUBLIC KEY-----",
    "-----BEGIN RSA PUBLIC KEY-----">();
  auto [offset_, type] = matcher.find(span);
  if (offset_ < 0) return -1;
  using Keys = ZuDecay<decltype(matcher.keys())>;
  offset = unsigned(offset_);
  length = ZuSwitch::dispatch<Keys::N>(unsigned(type), [](auto I) {
    return ZuType<I, Keys>{}().length();
  });
  return type;
}

int pemTail(ZuCSpan span, int type)
{
  static constexpr auto matcher = ZuMatcher<
    "-----END PRIVATE KEY-----",
    "-----END EC PRIVATE KEY-----",
    "-----END RSA PRIVATE KEY-----",
    "-----END PUBLIC KEY-----",
    "-----END RSA PUBLIC KEY-----">();
  auto [offset, type_] = matcher.find(span);
  return type == type_ ? offset : -1;
}

} // namespace Data

} // namespace Ztls::PK
