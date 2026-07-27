//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z http library - HTTP URL, origin, redirect, and Alt-Svc values

#include <zlib/ZhttpURL.hh>

#include <zlib/ZiPlatform.hh>

namespace Zhttp {
namespace URL_ {

inline bool ctl(unsigned c) { return c <= 0x20 || c == 0x7f; }
inline bool digit(unsigned c) { return c >= '0' && c <= '9'; }
inline bool alpha(unsigned c) {
  c |= 0x20;
  return c >= 'a' && c <= 'z';
}
inline bool alnum(unsigned c) { return alpha(c) || digit(c); }
inline bool hex(unsigned c) {
  c |= 0x20;
  return digit(c) || (c >= 'a' && c <= 'f');
}
inline unsigned lower(unsigned c) {
  return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

ZuCSpan trim(ZuCSpan s)
{
  while (s.length() && (s[0] == ' ' || s[0] == '\t')) s.offset(1);
  while (s.length() &&
      (s[s.length() - 1] == ' ' || s[s.length() - 1] == '\t'))
    s.trunc(s.length() - 1);
  return s;
}

bool equalCI(ZuCSpan l, ZuCSpan r)
{
  if (l.length() != r.length()) return false;
  for (unsigned i = 0; i < l.length(); ++i)
    if (lower(l[i]) != lower(r[i])) return false;
  return true;
}

void lowerCopy(URLString &out, ZuCSpan in)
{
  out.length(0);
  for (unsigned i = 0; i < in.length(); ++i) out << char(lower(in[i]));
}

URLError error(int code, unsigned offset = 0)
{
  return {offset, int8_t(code)};
}

URLError port(ZuCSpan s, uint16_t &out, unsigned offset)
{
  if (!s.length()) return error(URLCode::InvalidPort, offset);
  unsigned n = 0;
  for (unsigned i = 0; i < s.length(); ++i) {
    if (!digit(s[i])) return error(URLCode::InvalidPort, offset + i);
    n = n * 10 + unsigned(s[i] - '0');
    if (n > 65535) return error(URLCode::InvalidPort, offset + i);
  }
  if (!n) return error(URLCode::InvalidPort, offset);
  out = uint16_t(n);
  return {};
}

struct Authority {
  ZuCSpan	host;
  ZuCSpan	port;
  bool		explicitPort = false;
  bool		ipv6Literal = false;
};

bool regName(ZuCSpan s)
{
  for (unsigned i = 0; i < s.length(); ++i) {
    unsigned c = s[i];
    if (alnum(c) || c == '-' || c == '.' || c == '_' || c == '~' ||
	c == '!' || c == '$' || c == '&' || c == '\'' || c == '(' ||
	c == ')' || c == '*' || c == '+' || c == ',' || c == ';' ||
	c == '=')
      continue;
    if (c == '%' && i + 2 < s.length() &&
	hex(s[i + 1]) && hex(s[i + 2])) {
      i += 2;
      continue;
    }
    return false;
  }
  return true;
}

bool ipv6(ZuCSpan s)
{
  URLString text{s};
  in6_addr addr;
#ifndef _WIN32
  return ::inet_pton(AF_INET6, text.ndata(), &addr) == 1;
#else
  return ::InetPtonA(AF_INET6, text.ndata(), &addr) == 1;
#endif
}

URLError authority(Authority &out, ZuCSpan in, unsigned offset)
{
  if (!in.length()) return error(URLCode::MissingHost, offset);
  for (unsigned i = 0; i < in.length(); ++i)
    if (ctl(in[i]) || in[i] == '@')
      return error(in[i] == '@' ?
	URLCode::InvalidAuthority : URLCode::InvalidCharacter, offset + i);

  if (in[0] == '[') {
    int close = in.find([](auto c) { return c == ']'; });
    if (close <= 1) return error(URLCode::InvalidAuthority, offset);
    out.host = in;
    out.host.offset(1);
    out.host.trunc(close - 1);
    if (!ipv6(out.host))
      return error(URLCode::InvalidAuthority, offset + 1);
    out.ipv6Literal = true;
    if (unsigned(close + 1) == in.length()) return {};
    if (in[close + 1] != ':')
      return error(URLCode::InvalidAuthority, offset + close + 1);
    out.port = in;
    out.port.offset(close + 2);
    out.explicitPort = true;
    return out.port.length() ? URLError{} :
      error(URLCode::InvalidPort, offset + close + 2);
  }

  int colon = -1;
  for (unsigned i = 0; i < in.length(); ++i) {
    if (in[i] != ':') continue;
    if (colon >= 0) return error(URLCode::InvalidAuthority, offset + i);
    colon = i;
  }
  if (colon >= 0) {
    out.host = in;
    out.host.trunc(colon);
    out.port = in;
    out.port.offset(colon + 1);
    out.explicitPort = true;
  } else
    out.host = in;
  if (!out.host.length()) return error(URLCode::MissingHost, offset);
  if (!regName(out.host))
    return error(URLCode::InvalidAuthority, offset);
  if (out.explicitPort && !out.port.length())
    return error(URLCode::InvalidPort, offset + colon + 1);
  return {};
}

bool schemeChar(unsigned c)
{
  return alnum(c) || c == '+' || c == '-' || c == '.';
}

int schemeEnd(ZuCSpan s)
{
  if (!s.length() || !alpha(s[0])) return -1;
  for (unsigned i = 1; i < s.length(); ++i) {
    if (s[i] == ':') return i;
    if (!schemeChar(s[i])) return -1;
  }
  return -1;
}

void splitTarget(
  ZuCSpan target, ZuCSpan &path, ZuCSpan &query, bool &hasQuery)
{
  int q = target.find([](auto c) { return c == '?'; });
  hasQuery = q >= 0;
  path = target;
  query = {};
  if (hasQuery) {
    path.trunc(q);
    query = target;
    query.offset(q + 1);
  }
}

void popSegment(URLString &out)
{
  while (out.length() && out[out.length() - 1] != '/')
    out.length(out.length() - 1);
  if (out.length()) out.length(out.length() - 1);
}

URLString removeDots(ZuCSpan input)
{
  ZuCSpan in{input};
  URLString out;
  while (in.length()) {
    if (in.match("../")) {
      in.offset(3);
    } else if (in.match("./")) {
      in.offset(2);
    } else if (in.match("/./")) {
      in.offset(2);
    } else if (in.length() == 2 && in[0] == '/' && in[1] == '.') {
      in = "/";
    } else if (in.match("/../")) {
      in.offset(3);
      popSegment(out);
    } else if (in.length() == 3 && in[0] == '/' &&
	in[1] == '.' && in[2] == '.') {
      in = "/";
      popSegment(out);
    } else if ((in.length() == 1 && in[0] == '.') ||
	(in.length() == 2 && in[0] == '.' && in[1] == '.')) {
      in = {};
    } else {
      unsigned n = 0;
      if (in[0] == '/') {
	n = 1;
	while (n < in.length() && in[n] != '/') ++n;
      } else {
	while (n < in.length() && in[n] != '/') ++n;
      }
      out << ZuCSpan{in.data(), n};
      in.offset(n);
    }
  }
  return out;
}

URLString mergePath(ZuCSpan base, ZuCSpan ref)
{
  int slash = -1;
  for (unsigned i = 0; i < base.length(); ++i)
    if (base[i] == '/') slash = i;
  URLString merged;
  if (slash >= 0) merged << ZuCSpan{base.data(), unsigned(slash + 1)};
  else merged << '/';
  merged << ref;
  return removeDots(merged);
}

bool tokenChar(unsigned c)
{
  return alnum(c) || c == '!' || c == '#' || c == '$' || c == '%' ||
    c == '&' || c == '\'' || c == '*' || c == '+' || c == '-' ||
    c == '.' || c == '^' || c == '_' || c == '`' || c == '|' || c == '~';
}

bool uint32(ZuCSpan s, uint32_t &v)
{
  if (!s.length()) return false;
  uint64_t n = 0;
  for (unsigned i = 0; i < s.length(); ++i) {
    if (!digit(s[i])) return false;
    n = n * 10 + unsigned(s[i] - '0');
    if (n > UINT32_MAX) return false;
  }
  v = uint32_t(n);
  return true;
}

} // namespace URL_

bool h3ALPN(ZuCSpan s)
{
  if (s == "h3") return true;
  if (!s.match("h3-") || s.length() == 3) return false;
  for (unsigned i = 3; i < s.length(); ++i)
    if (!URL_::digit(s[i])) return false;
  return true;
}

void URL::pathQuery(ZuCSpan &path, ZuCSpan &query) const
{
  bool hasQuery;
  URL_::splitTarget(target, path, query, hasQuery);
  if (!path.length()) path = "/";
}

URLString URL::authority() const
{
  URLString out;
  if (ipv6Literal) out << '[' << host << ']';
  else out << host;
  if (explicitPort) out << ':' << port;
  return out;
}

URLString URL::str() const
{
  URLString out;
  out << scheme << "://" << authority() << target;
  return out;
}

URLError URL::parse(URL &out, ZuCSpan input)
{
  URL parsed;
  for (unsigned i = 0; i < input.length(); ++i)
    if (URL_::ctl(input[i]))
      return URL_::error(URLCode::InvalidCharacter, i);

  int colon = URL_::schemeEnd(input);
  if (colon < 0 || unsigned(colon + 2) >= input.length() ||
      input[colon + 1] != '/' || input[colon + 2] != '/')
    return URL_::error(URLCode::UnsupportedScheme);
  ZuCSpan scheme = input;
  scheme.trunc(colon);
  if (URL_::equalCI(scheme, "http")) {
    parsed.scheme = "http";
    parsed.port = 80;
  } else if (URL_::equalCI(scheme, "https")) {
    parsed.scheme = "https";
    parsed.port = 443;
  } else
    return URL_::error(URLCode::UnsupportedScheme);

  unsigned authStart = colon + 3;
  unsigned authEnd = authStart;
  while (authEnd < input.length() && input[authEnd] != '/' &&
      input[authEnd] != '?' && input[authEnd] != '#') ++authEnd;
  ZuCSpan auth{input.data() + authStart, authEnd - authStart};
  URL_::Authority authority;
  auto e = URL_::authority(authority, auth, authStart);
  if (!e.ok()) return e;
  URL_::lowerCopy(parsed.host, authority.host);
  parsed.ipv6Literal = authority.ipv6Literal;
  parsed.explicitPort = authority.explicitPort;
  if (authority.explicitPort) {
    e = URL_::port(
      authority.port, parsed.port,
      authStart + unsigned(authority.port.data() - auth.data()));
    if (!e.ok()) return e;
  }

  unsigned targetEnd = authEnd;
  while (targetEnd < input.length() && input[targetEnd] != '#') ++targetEnd;
  if (authEnd == targetEnd)
    parsed.target = "/";
  else if (input[authEnd] == '?')
    parsed.target << '/' <<
      ZuCSpan{input.data() + authEnd, targetEnd - authEnd};
  else if (input[authEnd] == '#')
    parsed.target = "/";
  else
    parsed.target =
      ZuCSpan{input.data() + authEnd, targetEnd - authEnd};
  out = ZuMv(parsed);
  return {};
}

URLError URL::resolve(URL &out, const URL &base, ZuCSpan ref)
{
  for (unsigned i = 0; i < ref.length(); ++i)
    if (URL_::ctl(ref[i]))
      return URL_::error(URLCode::InvalidCharacter, i);

  int fragment = ref.find([](auto c) { return c == '#'; });
  if (fragment >= 0) ref.trunc(fragment);
  int scheme = URL_::schemeEnd(ref);
  if (scheme >= 0) return parse(out, ref);
  if (ref.match("//")) {
    URLString absolute;
    absolute << base.scheme << ':' << ref;
    return parse(out, absolute);
  }

  URL resolved = base;
  ZuCSpan basePath, baseQuery, refPath, refQuery;
  bool baseHasQuery, refHasQuery;
  URL_::splitTarget(base.target, basePath, baseQuery, baseHasQuery);
  URL_::splitTarget(ref, refPath, refQuery, refHasQuery);

  URLString path;
  URLString query;
  bool hasQuery = false;
  if (!refPath.length()) {
    path = basePath;
    if (refHasQuery) {
      query = refQuery;
      hasQuery = true;
    } else if (baseHasQuery) {
      query = baseQuery;
      hasQuery = true;
    }
  } else {
    path = refPath[0] == '/' ?
      URL_::removeDots(refPath) : URL_::mergePath(basePath, refPath);
    if (refHasQuery) {
      query = refQuery;
      hasQuery = true;
    }
  }
  if (!path.length()) path = "/";
  resolved.target = path;
  if (hasQuery) resolved.target << '?' << query;
  out = ZuMv(resolved);
  return {};
}

URLError AltSvc::parse(
  AltSvc &out, ZuCSpan input, const Origin &origin,
  unsigned maxAlternatives)
{
  using namespace URL_;
  AltSvc parsed;
  input = trim(input);
  if (input == "clear") {
    parsed.clear = true;
    out = ZuMv(parsed);
    return {};
  }

  unsigned offset = 0;
  while (offset < input.length()) {
    while (offset < input.length() &&
	(input[offset] == ' ' || input[offset] == '\t')) ++offset;
    unsigned alpnStart = offset;
    while (offset < input.length() && tokenChar(input[offset])) ++offset;
    if (offset == alpnStart || offset >= input.length() ||
	input[offset++] != '=')
      return error(URLCode::InvalidAltSvc, offset);
    ZuCSpan alpn{input.data() + alpnStart, offset - alpnStart - 1};
    if (offset >= input.length() || input[offset++] != '"')
      return error(URLCode::InvalidAltSvc, offset);
    unsigned authStart = offset;
    while (offset < input.length() && input[offset] != '"') {
      if (input[offset] == '\\')
	return error(URLCode::InvalidAltSvc, offset);
      ++offset;
    }
    if (offset >= input.length())
      return error(URLCode::InvalidAltSvc, offset);
    ZuCSpan auth{input.data() + authStart, offset - authStart};
    ++offset;

    if (parsed.values.length() >= maxAlternatives)
      return error(URLCode::TooManyAlternatives, alpnStart);
    AltSvcValue value;
    lowerCopy(value.alpn, alpn);
    value.h3 = Zhttp::h3ALPN(value.alpn);
    if (auth.length() && auth[0] == ':') {
      value.host = origin.host;
      auto e = port(
	ZuCSpan{auth.data() + 1, auth.length() - 1},
	value.port, authStart + 1);
      if (!e.ok()) return e;
    } else {
      Authority a;
      auto e = authority(a, auth, authStart);
      if (!e.ok()) return e;
      lowerCopy(value.host, a.host);
      value.ipv6Literal = a.ipv6Literal;
      if (!a.explicitPort)
	return error(URLCode::InvalidAltSvc, authStart);
      e = port(a.port, value.port,
	authStart + unsigned(a.port.data() - auth.data()));
      if (!e.ok()) return e;
    }

    for (;;) {
      while (offset < input.length() &&
	  (input[offset] == ' ' || input[offset] == '\t')) ++offset;
      if (offset >= input.length() || input[offset] == ',') break;
      if (input[offset++] != ';')
	return error(URLCode::InvalidAltSvc, offset - 1);
      while (offset < input.length() &&
	  (input[offset] == ' ' || input[offset] == '\t')) ++offset;
      unsigned keyStart = offset;
      while (offset < input.length() && tokenChar(input[offset])) ++offset;
      if (offset == keyStart)
	return error(URLCode::InvalidAltSvc, offset);
      ZuCSpan key{input.data() + keyStart, offset - keyStart};
      ZuCSpan param;
      if (offset < input.length() && input[offset] == '=') {
	++offset;
	unsigned valueStart = offset;
	while (offset < input.length() && tokenChar(input[offset])) ++offset;
	if (offset == valueStart)
	  return error(URLCode::InvalidAltSvc, offset);
	param = {input.data() + valueStart, offset - valueStart};
      }
      if (equalCI(key, "ma")) {
	if (!uint32(param, value.maxAge))
	  return error(URLCode::InvalidAltSvc, keyStart);
      } else if (equalCI(key, "persist")) {
	if (param == "1") value.persist = true;
	else if (param != "0")
	  return error(URLCode::InvalidAltSvc, keyStart);
      }
    }
    parsed.values.push(ZuMv(value));
    if (offset >= input.length()) break;
    ++offset;
    if (offset >= input.length())
      return error(URLCode::InvalidAltSvc, offset);
  }
  if (!parsed.values.length())
    return error(URLCode::InvalidAltSvc);
  out = ZuMv(parsed);
  return {};
}

bool AltSvcCache::update(
  const Origin &origin, const AltSvc &altSvc, ZuTime now)
{
  if (altSvc.clear) {
    m_entries->del(origin);
    return true;
  }

  AltSvcEntry entry;
  for (unsigned i = 0; i < altSvc.values.length(); ++i) {
    const auto &value = altSvc.values[i];
    if (!value.maxAge) continue;
    entry.values.push(CachedAltSvc{
      .value = value,
      .expires = now + ZuTime{int64_t(value.maxAge)}
    });
  }

  bool exists = !!m_entries->find(origin);
  if (!exists && m_entries->count_() >= m_maxOrigins) return false;
  if (exists) m_entries->del(origin);
  if (entry.values.length())
    m_entries->add(origin, ZuMv(entry));
  return true;
}

bool AltSvcCache::get(
  const Origin &origin, AltSvcValues &values, ZuTime now)
{
  return get_(origin, &values, now);
}

bool AltSvcCache::get_(
  const Origin &origin, AltSvcValues *values, ZuTime now)
{
  if (values) values->length(0);
  auto node = m_entries->find(origin);
  if (!node) return false;
  auto &cached = node->val().values;
  bool h3 = false;
  unsigned n = 0;
  for (unsigned i = 0; i < cached.length(); ++i) {
    if (cached[i].expires <= now) continue;
    if (n != i) cached[n] = ZuMv(cached[i]);
    if (values)
      values->push(cached[n].value);
    else
      h3 |= cached[n].value.h3;
    ++n;
  }
  if (n < cached.length()) cached.splice(n);
  if (n) return values ? true : h3;
  m_entries->del(origin);
  return false;
}

} // namespace Zhttp
