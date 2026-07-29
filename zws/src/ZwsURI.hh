//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ws:// and wss:// URI parsing

#ifndef ZwsURI_HH
#define ZwsURI_HH

#ifndef ZwsLib_HH
#include <zlib/ZwsLib.hh>
#endif

#include <stdint.h>

#include <zlib/ZuSpan.hh>

#include <zlib/ZtString.hh>

namespace Zws {

struct URICode {
  enum {
    OK,
    UnsupportedScheme,
    MissingHost,
    InvalidAuthority,
    InvalidPort,
    InvalidCharacter,
    Fragment,
    UserInfo
  };
};

struct URIError {
  uint32_t	offset = 0;
  int8_t	code = URICode::OK;

  bool ok() const { return code == URICode::OK; }
};

ZuDerive(URIString, ZtString<ZtStringHeapID<"Zws.URI">>);

struct URI {
  URIString	scheme;
  URIString	host;
  URIString	target;
  uint16_t	port = 0;
  bool		explicitPort = false;
  bool		ipv6Literal = false;

  bool secure() const { return scheme == "wss"; }
  URIString authority() const;
  URIString str() const;

  static URIError parse(URI &, ZuCSpan);
};

} // namespace Zws

#endif /* ZwsURI_HH */
