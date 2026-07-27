//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z http library - HTTP URL, origin, redirect, and Alt-Svc values

#ifndef ZhttpURL_HH
#define ZhttpURL_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <stdint.h>

#include <zlib/ZuHash.hh>
#include <zlib/ZuString.hh>
#include <zlib/ZuTime.hh>

#include <zlib/ZmHash.hh>
#include <zlib/ZmRef.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>

namespace Zhttp {

struct URLCode {
  enum {
    OK,
    UnsupportedScheme,
    MissingHost,
    InvalidAuthority,
    InvalidPort,
    InvalidCharacter,
    InvalidReference,
    InvalidAltSvc,
    TooManyAlternatives
  };
};

struct URLError {
  uint32_t	offset = 0;
  int8_t	code = URLCode::OK;

  bool ok() const { return code == URLCode::OK; }
};

ZuDerive(URLString, ZtString<ZtStringHeapID<"Zhttp.URL">>);

struct Origin {
  URLString	scheme;
  URLString	host;
  uint16_t	port = 0;

  bool equals(const Origin &o) const {
    return scheme == o.scheme && host == o.host && port == o.port;
  }
  friend bool operator ==(const Origin &l, const Origin &r) {
    return l.equals(r);
  }
  uint32_t hash() const {
    uint32_t h = ZuHash<URLString>::hash(scheme);
    h ^= ZuHash<URLString>::hash(host) + 0x9e3779b9U + (h<<6) + (h>>2);
    h ^= ZuHash<uint16_t>::hash(port) + 0x9e3779b9U + (h<<6) + (h>>2);
    return h;
  }
};

struct URL {
  URLString	scheme;
  URLString	host;
  URLString	target;
  uint16_t	port = 0;
  bool		explicitPort = false;
  bool		ipv6Literal = false;

  Origin origin() const { return {scheme, host, port}; }
  bool secure() const { return scheme == "https"; }

  void pathQuery(ZuCSpan &path, ZuCSpan &query) const;
  URLString authority() const;
  URLString str() const;

  static URLError parse(URL &, ZuCSpan);
  static URLError resolve(URL &, const URL &, ZuCSpan);
};

struct AltSvcValue {
  URLString	alpn;
  URLString	host;
  uint32_t	maxAge = 86400;
  uint16_t	port = 0;
  bool		persist = false;
  bool		h3 = false;
  bool		ipv6Literal = false;
};

using AltSvcValues =
  ZtArray<AltSvcValue, ZtArrayHeapID<"Zhttp.AltSvc">>;

struct AltSvc {
  AltSvcValues	values;
  bool		clear = false;

  static URLError parse(
    AltSvc &, ZuCSpan, const Origin &, unsigned maxAlternatives);
};

ZhttpAPI bool h3ALPN(ZuCSpan);

struct CachedAltSvc {
  AltSvcValue	value;
  ZuTime	expires;
};

using CachedAltSvcValues =
  ZtArray<CachedAltSvc, ZtArrayHeapID<"Zhttp.AltSvc.CacheValue">>;

struct AltSvcEntry {
  CachedAltSvcValues	values;
};

using AltSvcTable = ZmHashKV<
  Origin, AltSvcEntry, ZmHashHeapID<"Zhttp.AltSvc.Cache">>;
using AltSvcTableRef = ZmRef<AltSvcTable>;

class AltSvcCache {
public:
  AltSvcCache(unsigned maxOrigins) :
    m_entries{new AltSvcTable}, m_maxOrigins{maxOrigins} { }

  bool update(const Origin &, const AltSvc &, ZuTime now);
  bool get(const Origin &, AltSvcValues &, ZuTime now);
  bool hasH3(const Origin &origin, ZuTime now) {
    return get_(origin, nullptr, now);
  }
  void del(const Origin &origin) { m_entries->del(origin); }
  unsigned count() const { return m_entries->count_(); }

private:
  bool get_(const Origin &, AltSvcValues *, ZuTime);

  AltSvcTableRef	m_entries;
  unsigned	m_maxOrigins = 0;
};

} // namespace Zhttp

#endif /* ZhttpURL_HH */
