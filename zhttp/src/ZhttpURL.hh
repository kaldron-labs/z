//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z http library - HTTP URL, origin, and request-target values

#ifndef ZhttpURL_HH
#define ZhttpURL_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <stdint.h>

#include <zlib/ZuBox.hh>
#include <zlib/ZuHash.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZuString.hh>
#include <zlib/ZtEnum.hh>
#include <zlib/ZtArray.hh>

#include <zlib/ZhttpCore.hh>

namespace Zhttp {

ZtEnumNS(ZhttpAPI, Scheme, int8_t, http, https);

namespace Scheme {
ZhttpAPI T parse(ZuBSpan);

inline uint16_t defltPort(T v)
{
  switch (v) {
    case http: return 80;
    case https: return 443;
    default: return 0;
  }
}
}

ZtEnumNS(ZhttpAPI, TargetForm, int8_t,
  Origin, Absolute, Authority, Asterisk, ExtendedConnect);

ZtEnumNS(ZhttpAPI, URLParseCode, int8_t,
  OK, UnsupportedScheme, MissingHost, InvalidAuthority, InvalidPort,
  InvalidCharacter, InvalidReference);

struct URLParseError {
  uint32_t		offset = 0;
  URLParseCode::T	code = URLParseCode::OK;

  bool ok() const { return code == URLParseCode::OK; }
};

ZtEnumNS(ZhttpAPI, TargetParseCode, int8_t,
  OK, InvalidForm, InvalidMethod, InvalidScheme, InvalidAuthority, InvalidPath,
  InvalidProtocol, InvalidCharacter);

ZtEnumNS(ZhttpAPI, TargetField, int8_t,
  Raw, Scheme, Authority, Path, Protocol);

struct TargetParseError {
  uint32_t			offset = 0;
  TargetParseCode::T	code = TargetParseCode::OK;
  TargetField::T		field = TargetField::Raw;

  bool ok() const { return code == TargetParseCode::OK; }
};

ZuDerive(URLString, ZtBArray<ZtArrayHeapID<"Zhttp.URL">>);

struct AuthorityView {
  ZuBSpan	raw;
  ZuBSpan	host;
  uint16_t	port = 0;
  bool		explicitPort = false;
  bool		ipv6Literal = false;
  bool		normalized = false;

  template <typename S> void print(S &s) const {
    if (ipv6Literal) s << '[';
    s << host;
    if (ipv6Literal) s << ']';
    if (explicitPort) s << ':' << ZuBoxed(port);
  }
  friend ZuPrintFn ZuPrintType(AuthorityView *);
};

ZhttpAPI URLParseError parseAuthority(
  AuthorityView &, ZuBSpan, unsigned offset,
  uint16_t defltPort, bool requirePort, bool allowZeroPort = false);
ZhttpAPI URLParseError parseAuthority(
  AuthorityView &, ZuSpan<uint8_t>, unsigned offset,
  uint16_t defltPort, bool requirePort, bool allowZeroPort = false);

struct OriginView {
  ZuBSpan	host;
  uint16_t	port = 0;
  Scheme::T	scheme = -1;
  bool		ipv6Literal = false;

  bool equals(const OriginView &) const;
  friend bool operator ==(const OriginView &l, const OriginView &r) {
    return l.equals(r);
  }
  uint32_t hash() const;

  template <typename S> void print(S &s) const {
    s << Scheme::name(scheme) << "://";
    if (ipv6Literal) s << '[';
    s << host;
    if (ipv6Literal) s << ']';
    if (port != Scheme::defltPort(scheme)) s << ':' << ZuBoxed(port);
  }
  friend ZuPrintFn ZuPrintType(OriginView *);
};

struct Origin {
  URLString	host;
  uint16_t	port = 0;
  Scheme::T	scheme = -1;
  bool		ipv6Literal = false;

  Origin() = default;
  explicit Origin(const OriginView &);

  OriginView view() const {
    return {host, port, scheme, ipv6Literal};
  }
  bool equals(const Origin &o) const {
    return scheme == o.scheme && host == o.host && port == o.port;
  }
  friend bool operator ==(const Origin &l, const Origin &r) {
    return l.equals(r);
  }
  uint32_t hash() const;
};

struct URL {
  ZuBSpan	raw;
  ZuBSpan	authorityRaw;
  ZuBSpan	host;
  ZuBSpan	path;
  ZuBSpan	query;
  ZuBSpan	fragment;
  URLParseError	parseError;
  uint16_t	port = 0;
  Scheme::T	scheme = -1;
  bool		hasQuery = false;
  bool		hasFragment = false;
  bool		explicitPort = false;
  bool		ipv6Literal = false;

  URL() : parseError{0, URLParseCode::InvalidReference} { }
  explicit URL(ZuSpan<uint8_t>);

  URLParseError error() const { return parseError; }
  bool ok() const { return parseError.ok(); }
  bool secure() const { return scheme == Scheme::https; }
  AuthorityView authority() const;
  OriginView origin() const;

  template <typename S> void writeTarget(S &s) const {
    if (path) s << path;
    else s << '/';
    if (hasQuery) s << '?' << query;
  }

  template <typename S> void print(S &s) const {
    s << Scheme::name(scheme) << "://" << authority() << path;
    if (hasQuery) s << '?' << query;
    if (hasFragment) s << '#' << fragment;
  }
  friend ZuPrintFn ZuPrintType(URL *);
};

class URLStorage {
public:
  URLStorage() = default;
  explicit URLStorage(ZuBSpan s) { assign(s); }

  URLParseError assign(ZuBSpan);
  URLParseError adopt(URLString &&);
  URLParseError resolve(const URL &, ZuBSpan);
  URL url() const;
  bool ok() const { return m_error.ok(); }
  URLParseError error() const { return m_error; }

private:
  struct Part {
    uint32_t offset = 0;
    uint32_t length = 0;
  };
  void commit_(URLString &&, const URL &);

  URLString	m_data;
  URLParseError	m_error{0, URLParseCode::InvalidReference};
  Part		m_host;
  Part		m_authority;
  Part		m_path;
  Part		m_query;
  Part		m_fragment;
  uint16_t	m_port = 0;
  Scheme::T	m_scheme = -1;
  bool		m_hasQuery = false;
  bool		m_hasFragment = false;
  bool		m_explicitPort = false;
  bool		m_ipv6Literal = false;
};

struct Target {
  AuthorityView	authority;
  ZuBSpan	raw;
  ZuBSpan	pathQuery;
  ZuBSpan	protocol;
  Scheme::T	scheme = -1;
  TargetForm::T	form = TargetForm::Origin;

  static TargetParseError parseH1(
    Target &, Method::T, ZuSpan<uint8_t>);
  static TargetParseError fromPseudo(
    Target &, Method::T, Scheme::T, ZuSpan<uint8_t>,
    ZuBSpan, ZuBSpan);
};

} // namespace Zhttp

#endif /* ZhttpURL_HH */
