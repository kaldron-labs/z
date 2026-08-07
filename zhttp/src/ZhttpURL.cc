//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z http library - HTTP URL, origin, and request-target values

#include <zlib/ZhttpURL.hh>

#include <string.h>

#include <zlib/ZuICmp.hh>

#include <zlib/ZiIP.hh>

#include <zlib/ZhttpCore.hh>

namespace Zhttp {

ZtEnumImplNS(Scheme);
ZtEnumImplNS(TargetForm);
ZtEnumImplNS(URLParseCode);
ZtEnumImplNS(RequestTargetParseCode);
ZtEnumImplNS(RequestTargetField);

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

URLParseError error(URLParseCode::T code, unsigned offset = 0)
{
  return {offset, code};
}

RequestTargetParseError targetError(
  RequestTargetParseCode::T code, unsigned offset = 0,
  RequestTargetField::T field = RequestTargetField::Raw)
{
  return {offset, code, field};
}

URLParseError port(
  ZuBSpan s, uint16_t &out, unsigned offset, bool allowZeroPort)
{
  if (!s) return error(URLParseCode::InvalidPort, offset);
  unsigned n = 0;
  for (unsigned i = 0; i < s.length(); ++i) {
    if (!digit(s[i])) return error(URLParseCode::InvalidPort, offset + i);
    n = n * 10 + unsigned(s[i] - '0');
    if (n > 65535) return error(URLParseCode::InvalidPort, offset + i);
  }
  if (!n && !allowZeroPort)
    return error(URLParseCode::InvalidPort, offset);
  out = uint16_t(n);
  return {};
}

bool regName(ZuBSpan s)
{
  for (unsigned i = 0; i < s.length(); ++i) {
    unsigned c = s[i];
    if (alnum(c) || c == '-' || c == '.' || c == '_' || c == '~' ||
	c == '!' || c == '$' || c == '&' || c == '\'' || c == '(' ||
	c == ')' || c == '*' || c == '+' || c == ',' || c == ';' ||
	c == '=')
      continue;
    if (c == '%' && i + 2 < s.length() && hex(s[i + 1]) && hex(s[i + 2])) {
      i += 2;
      continue;
    }
    return false;
  }
  return true;
}

URLParseError authority(
  AuthorityView &out, ZuBSpan in, unsigned offset,
  uint16_t defltPort, bool requirePort, bool allowZeroPort = false)
{
  AuthorityView parsed;
  parsed.raw = in;
  parsed.port = defltPort;
  if (!in) return error(URLParseCode::MissingHost, offset);
  for (unsigned i = 0; i < in.length(); ++i)
    if (ctl(in[i]) || in[i] == '@')
      return error(in[i] == '@' ? URLParseCode::InvalidAuthority :
	URLParseCode::InvalidCharacter, offset + i);

  ZuBSpan portText;
  unsigned portOffset = 0;
  if (in[0] == '[') {
    int close = in.find([](auto c) { return c == ']'; });
    if (close <= 1) return error(URLParseCode::InvalidAuthority, offset);
    parsed.host = {in.data() + 1, unsigned(close - 1)};
    ZiIP ip;
    if (!ZiIP::parse(ip, parsed.host) || !ip.v6())
      return error(URLParseCode::InvalidAuthority, offset + 1);
    parsed.ipv6Literal = true;
    if (unsigned(close + 1) < in.length()) {
      if (in[close + 1] != ':')
	return error(URLParseCode::InvalidAuthority, offset + close + 1);
      portOffset = close + 2;
      portText = {in.data() + portOffset, in.length() - portOffset};
      parsed.explicitPort = true;
    }
  } else {
    int colon = -1;
    for (unsigned i = 0; i < in.length(); ++i) {
      if (in[i] != ':') continue;
      if (colon >= 0) return error(URLParseCode::InvalidAuthority, offset + i);
      colon = i;
    }
    if (colon >= 0) {
      parsed.host = {in.data(), unsigned(colon)};
      portOffset = colon + 1;
      portText = {in.data() + portOffset, in.length() - portOffset};
      parsed.explicitPort = true;
    } else
      parsed.host = in;
    if (!parsed.host) return error(URLParseCode::MissingHost, offset);
    if (!regName(parsed.host))
      return error(URLParseCode::InvalidAuthority, offset);
  }
  if (requirePort && !parsed.explicitPort)
    return error(URLParseCode::InvalidPort, offset + in.length());
  if (parsed.explicitPort) {
    auto e = port(
      portText, parsed.port, offset + portOffset, allowZeroPort);
    if (!e.ok()) return e;
  }
  out = parsed;
  return {};
}

URLParseError authority(
  AuthorityView &out, ZuSpan<uint8_t> in, unsigned offset,
  uint16_t defltPort, bool requirePort, bool allowZeroPort = false)
{
  auto e = authority(
    out, ZuBSpan{in}, offset, defltPort, requirePort, allowZeroPort);
  if (!e.ok()) return e;
  unsigned hostOffset = unsigned(out.host.data() - in.data());
  ZuSpan host{in.data() + hostOffset, out.host.length()};
  lowerASCII(host);
  out.host = host;
  out.normalized = true;
  return {};
}

bool schemeChar(unsigned c)
{
  return alnum(c) || c == '+' || c == '-' || c == '.';
}

bool tokenChar(unsigned c)
{
  return alnum(c) || c == '!' || c == '#' || c == '$' || c == '%' ||
    c == '&' || c == '\'' || c == '*' || c == '+' || c == '-' ||
    c == '.' || c == '^' || c == '_' || c == '`' || c == '|' || c == '~';
}

int schemeEnd(ZuBSpan s)
{
  if (!s || !alpha(s[0])) return -1;
  for (unsigned i = 1; i < s.length(); ++i) {
    if (s[i] == ':') return i;
    if (!schemeChar(s[i])) return -1;
  }
  return -1;
}

int invalidText(ZuBSpan s, bool fragment)
{
  for (unsigned i = 0; i < s.length(); ++i) {
    unsigned c = s[i];
    if (ctl(c) || (!fragment && c == '#')) return i;
    if (c == '%' &&
	(i + 2 >= s.length() || !hex(s[i + 1]) || !hex(s[i + 2])))
      return i;
  }
  return -1;
}

unsigned popSegment(URLString &out, unsigned floor, unsigned write)
{
  while (write > floor && out[write - 1] != '/') --write;
  if (write > floor) --write;
  return write;
}

void removeDots(URLString &out, unsigned floor)
{
  unsigned read = floor;
  unsigned write = floor;
  unsigned length = out.length();
  while (read < length) {
    ZuBSpan in{reinterpret_cast<const uint8_t *>(out.data()) + read,
	length - read};
    if (in.match("../")) {
      read += 3;
    } else if (in.match("./")) {
      read += 2;
    } else if (in.match("/./")) {
      read += 2;
    } else if (in.length() == 2 && in[0] == '/' && in[1] == '.') {
      read += 2;
      out[write++] = '/';
    } else if (in.match("/../")) {
      read += 3;
      write = popSegment(out, floor, write);
    } else if (in.length() == 3 && in[0] == '/' &&
	in[1] == '.' && in[2] == '.') {
      read += 3;
      write = popSegment(out, floor, write);
      out[write++] = '/';
    } else if ((in.length() == 1 && in[0] == '.') ||
	(in.length() == 2 && in[0] == '.' && in[1] == '.')) {
      read = length;
    } else {
      unsigned begin = read;
      if (out[read] == '/') {
	++read;
	while (read < length && out[read] != '/') ++read;
      } else {
	while (read < length && out[read] != '/') ++read;
      }
      unsigned n = read - begin;
      if (write != begin)
	memmove(out.data() + write, out.data() + begin, n);
      write += n;
    }
  }
  out.length(write);
}

void appendAbsoluteReference(
  URLString &out, ZuBSpan ref, unsigned pathStart)
{
  out << ZuBSpan{ref.data(), pathStart};
  unsigned floor = out.length();
  auto pathQuery = splitPathQuery({
    ref.data() + pathStart, ref.length() - pathStart});
  out << pathQuery.path;
  removeDots(out, floor);
  if (pathQuery.hasQuery) out << '?' << pathQuery.query;
}

} // namespace URL_

URLParseError parseAuthority(
  AuthorityView &out, ZuBSpan input, unsigned offset,
  uint16_t defltPort, bool requirePort, bool allowZeroPort)
{
  return URL_::authority(
    out, input, offset, defltPort, requirePort, allowZeroPort);
}

URLParseError parseAuthority(
  AuthorityView &out, ZuSpan<uint8_t> input, unsigned offset,
  uint16_t defltPort, bool requirePort, bool allowZeroPort)
{
  return URL_::authority(
    out, input, offset, defltPort, requirePort, allowZeroPort);
}

Scheme::T Scheme::parse(ZuBSpan s)
{
  if (ZuICmp<ZuBSpan>::equals(s, "http")) return Scheme::http;
  if (ZuICmp<ZuBSpan>::equals(s, "https")) return Scheme::https;
  return -1;
}

PathQuery splitPathQuery(ZuBSpan target)
{
  int q = target.find([](auto c) { return c == '?'; });
  if (q < 0) return {target, {}, false};
  return {
    {target.data(), unsigned(q)},
    {target.data() + q + 1, target.length() - unsigned(q + 1)},
    true
  };
}

bool OriginView::equals(const OriginView &o) const
{
  return scheme == o.scheme && host == o.host && port == o.port;
}

uint32_t OriginView::hash() const
{
  return ZuHash<Scheme::T>::hash(scheme) ^ host.hash() ^
    ZuHash<uint16_t>::hash(port);
}

Origin::Origin(const OriginView &v) :
  host{v.host}, port{v.port}, scheme{v.scheme}, ipv6Literal{v.ipv6Literal}
{
}

uint32_t Origin::hash() const
{
  return ZuHash<Scheme::T>::hash(scheme) ^ host.cspan().hash() ^
    ZuHash<uint16_t>::hash(port);
}

URL::URL(ZuSpan<uint8_t> input) : raw{input}
{
  using namespace URL_;
  int colon = schemeEnd(raw);
  if (colon < 0 || unsigned(colon + 2) >= raw.length() ||
      raw[colon + 1] != '/' || raw[colon + 2] != '/') {
    parseError = URL_::error(URLParseCode::UnsupportedScheme);
    return;
  }
  scheme = Scheme::parse({raw.data(), unsigned(colon)});
  if (scheme < 0) {
    parseError = URL_::error(URLParseCode::UnsupportedScheme);
    return;
  }

  unsigned authStart = colon + 3;
  unsigned authEnd = authStart;
  while (authEnd < raw.length() && raw[authEnd] != '/' &&
      raw[authEnd] != '?' && raw[authEnd] != '#') ++authEnd;
  authorityRaw = {raw.data() + authStart, authEnd - authStart};
  AuthorityView a;
  parseError = parseAuthority(a,
    ZuSpan<uint8_t>{input.data() + authStart, authEnd - authStart},
    authStart, Scheme::defltPort(scheme), false);
  if (!parseError.ok()) return;
  host = a.host;
  port = a.port;
  explicitPort = a.explicitPort;
  ipv6Literal = a.ipv6Literal;

  unsigned queryAt = raw.length();
  unsigned fragmentAt = raw.length();
  for (unsigned i = authEnd; i < raw.length(); ++i) {
    if (raw[i] == '#' && fragmentAt == raw.length()) {
      fragmentAt = i;
      break;
    }
    if (raw[i] == '?' && queryAt == raw.length()) queryAt = i;
  }
  unsigned pathEnd = queryAt < fragmentAt ? queryAt : fragmentAt;
  path = {raw.data() + authEnd, pathEnd - authEnd};
  if (path && path[0] != '/') {
    parseError = URL_::error(URLParseCode::InvalidCharacter, authEnd);
    return;
  }
  if (queryAt < fragmentAt) {
    hasQuery = true;
    query = {raw.data() + queryAt + 1, fragmentAt - queryAt - 1};
  }
  if (fragmentAt < raw.length()) {
    hasFragment = true;
    fragment = {
      raw.data() + fragmentAt + 1, raw.length() - fragmentAt - 1};
  }
  if (int i = invalidText(path, false); i >= 0) {
    parseError = URL_::error(URLParseCode::InvalidCharacter, authEnd + i);
    return;
  }
  if (int i = invalidText(query, false); i >= 0) {
    parseError = URL_::error(
      URLParseCode::InvalidCharacter, queryAt + 1 + i);
    return;
  }
  if (int i = invalidText(fragment, true); i >= 0) {
    parseError = URL_::error(
      URLParseCode::InvalidCharacter, fragmentAt + 1 + i);
    return;
  }
}

AuthorityView URL::authority() const
{
  return {
    authorityRaw, host, port, explicitPort, ipv6Literal, true
  };
}

OriginView URL::origin() const
{
  return {host, port, scheme, ipv6Literal};
}

PathQuery URL::pathQuery() const
{
  static const uint8_t slash[] = {'/'};
  return {path ? path : ZuBSpan{slash, 1}, query, hasQuery};
}

void URLStorage::commit_(URLString &&data, const URL &url)
{
  auto part = [&data](ZuBSpan s) {
    return Part{
      uint32_t(s.data() - reinterpret_cast<const uint8_t *>(data.data())),
      uint32_t(s.length())
    };
  };
  m_host = part(url.host);
  m_authority = part(url.authorityRaw);
  m_path = part(url.path);
  m_query = url.hasQuery ? part(url.query) : Part{};
  m_fragment = url.hasFragment ? part(url.fragment) : Part{};
  m_port = url.port;
  m_scheme = url.scheme;
  m_hasQuery = url.hasQuery;
  m_hasFragment = url.hasFragment;
  m_explicitPort = url.explicitPort;
  m_ipv6Literal = url.ipv6Literal;
  m_error = {};
  m_data = ZuMv(data);
}

URLParseError URLStorage::assign(ZuBSpan input)
{
  URLString candidate{input};
  return adopt(ZuMv(candidate));
}

URLParseError URLStorage::adopt(URLString &&candidate)
{
  candidate.ensure(candidate.length() + 1);
  URL parsed{ZuSpan<uint8_t>{
    reinterpret_cast<uint8_t *>(candidate.data()), candidate.length()}};
  if (!parsed.ok()) return parsed.error();
  commit_(ZuMv(candidate), parsed);
  return {};
}

URL URLStorage::url() const
{
  URL out;
  if (!ok()) {
    out.parseError = m_error;
    return out;
  }
  auto data = reinterpret_cast<const uint8_t *>(m_data.data());
  auto span = [data](Part part) {
    return ZuBSpan{data + part.offset, part.length};
  };
  out.raw = {data, m_data.length()};
  out.host = span(m_host);
  out.authorityRaw = span(m_authority);
  out.path = span(m_path);
  out.query = span(m_query);
  out.fragment = span(m_fragment);
  out.parseError = m_error;
  out.port = m_port;
  out.scheme = m_scheme;
  out.hasQuery = m_hasQuery;
  out.hasFragment = m_hasFragment;
  out.explicitPort = m_explicitPort;
  out.ipv6Literal = m_ipv6Literal;
  return out;
}

URLParseError URLStorage::resolve(const URL &base, ZuBSpan ref)
{
  using namespace URL_;
  ZuBSpan fullRef = ref;
  if (int i = invalidText(ref, true); i >= 0)
    return URL_::error(URLParseCode::InvalidCharacter, i);
  ZuBSpan fragmentText;
  bool hasFragment = false;
  int fragment = ref.find([](auto c) { return c == '#'; });
  if (fragment >= 0) {
    hasFragment = true;
    fragmentText = {
      ref.data() + fragment + 1, ref.length() - unsigned(fragment + 1)};
    ref.trunc(fragment);
  }
  int colon = schemeEnd(ref);
  if (colon >= 0) {
    if (Scheme::parse({ref.data(), unsigned(colon)}) < 0 ||
	unsigned(colon + 2) >= ref.length() ||
	ref[colon + 1] != '/' || ref[colon + 2] != '/')
      return assign(fullRef);
    unsigned pathStart = colon + 3;
    while (pathStart < ref.length() && ref[pathStart] != '/' &&
	ref[pathStart] != '?') ++pathStart;
    URLString candidate;
    appendAbsoluteReference(candidate, ref, pathStart);
    if (hasFragment) candidate << '#' << fragmentText;
    return adopt(ZuMv(candidate));
  }

  if (ref.match("//")) {
    unsigned pathStart = 2;
    while (pathStart < ref.length() && ref[pathStart] != '/' &&
	ref[pathStart] != '?') ++pathStart;
    URLString candidate;
    candidate << Scheme::name(base.scheme) << ':';
    appendAbsoluteReference(candidate, ref, pathStart);
    if (hasFragment) candidate << '#' << fragmentText;
    return adopt(ZuMv(candidate));
  }

  URLString candidate;
  candidate << Scheme::name(base.scheme) << ':';
  candidate << "//" << base.authority();
  unsigned pathFloor = candidate.length();
  auto refPQ = splitPathQuery(ref);
  auto basePQ = base.pathQuery();
  if (!refPQ.path) {
    candidate << base.path;
    if (refPQ.hasQuery)
      candidate << '?' << refPQ.query;
    else if (base.hasQuery)
      candidate << '?' << base.query;
  } else {
    if (refPQ.path[0] == '/') {
      candidate << refPQ.path;
    } else {
      int slash = -1;
      for (unsigned i = 0; i < basePQ.path.length(); ++i)
        if (basePQ.path[i] == '/') slash = i;
      if (slash >= 0)
        candidate << ZuBSpan{basePQ.path.data(), unsigned(slash + 1)};
      else
	candidate << '/';
      candidate << refPQ.path;
    }
    removeDots(candidate, pathFloor);
    if (refPQ.hasQuery) candidate << '?' << refPQ.query;
  }
  if (hasFragment) candidate << '#' << fragmentText;
  return adopt(ZuMv(candidate));
}

RequestTargetParseError RequestTarget::parseH1(
  RequestTarget &out, Method::T method, ZuSpan<uint8_t> input)
{
  using namespace URL_;
  RequestTarget parsed;
  parsed.raw = input;
  if (!input)
    return targetError(RequestTargetParseCode::InvalidForm);
  if (input.length() == 1 && input[0] == '*') {
    if (method != Method::OPTIONS)
      return targetError(RequestTargetParseCode::InvalidMethod);
    parsed.form = TargetForm::Asterisk;
    parsed.path = input;
    out = parsed;
    return {};
  }
  if (method == Method::CONNECT) {
    URLParseError e = parseAuthority(parsed.authority, input, 0, 0, true);
    if (!e.ok())
      return targetError(RequestTargetParseCode::InvalidAuthority, e.offset);
    parsed.form = TargetForm::Authority;
    out = parsed;
    return {};
  }
  if (input[0] == '/') {
    if (int i = invalidText(input, false); i >= 0)
      return targetError(RequestTargetParseCode::InvalidCharacter, i);
    auto pq = splitPathQuery(input);
    parsed.path = pq.path;
    parsed.query = pq.query;
    parsed.hasQuery = pq.hasQuery;
    parsed.form = TargetForm::Origin;
    out = parsed;
    return {};
  }
  int fragment = input.find([](auto c) { return c == '#'; });
  if (fragment >= 0)
    return targetError(RequestTargetParseCode::InvalidCharacter, fragment);
  URL url{input};
  if (!url.ok())
    switch (url.error().code) {
      case URLParseCode::UnsupportedScheme:
	return targetError(
	  RequestTargetParseCode::InvalidScheme, url.error().offset);
      case URLParseCode::MissingHost:
      case URLParseCode::InvalidAuthority:
      case URLParseCode::InvalidPort:
	return targetError(
	  RequestTargetParseCode::InvalidAuthority, url.error().offset);
      case URLParseCode::InvalidCharacter:
	return targetError(
	  RequestTargetParseCode::InvalidCharacter, url.error().offset);
      default:
	return targetError(
	  RequestTargetParseCode::InvalidForm, url.error().offset);
    }
  parsed.authority = url.authority();
  parsed.path = url.pathQuery().path;
  parsed.query = url.query;
  parsed.hasQuery = url.hasQuery;
  parsed.scheme = url.scheme;
  parsed.form = TargetForm::Absolute;
  out = parsed;
  return {};
}

RequestTargetParseError RequestTarget::fromPseudo(
  RequestTarget &out, Method::T method, Scheme::T scheme,
  ZuSpan<uint8_t> authorityText, ZuBSpan pathText, ZuBSpan protocolText)
{
  using namespace URL_;
  RequestTarget parsed;
  if (method == Method::CONNECT) {
    if (!protocolText) {
      if (scheme >= 0 || pathText)
	return targetError(RequestTargetParseCode::InvalidForm, 0,
	  pathText ? RequestTargetField::Path : RequestTargetField::Scheme);
      auto e = parseAuthority(
	parsed.authority, authorityText, 0, 0, true);
      if (!e.ok())
	return targetError(RequestTargetParseCode::InvalidAuthority, e.offset,
	  RequestTargetField::Authority);
      parsed.raw = authorityText;
      parsed.form = TargetForm::Authority;
      out = parsed;
      return {};
    }
    if (scheme < 0 || scheme >= Scheme::N)
      return targetError(RequestTargetParseCode::InvalidScheme, 0,
	RequestTargetField::Scheme);
    if (!tokenChar(protocolText[0]))
      return targetError(RequestTargetParseCode::InvalidProtocol, 0,
	RequestTargetField::Protocol);
    for (unsigned i = 1; i < protocolText.length(); ++i)
      if (!tokenChar(protocolText[i]))
	return targetError(RequestTargetParseCode::InvalidProtocol, i,
	  RequestTargetField::Protocol);
    auto e = parseAuthority(parsed.authority, authorityText, 0,
      Scheme::defltPort(scheme), false);
    if (!e.ok())
      return targetError(RequestTargetParseCode::InvalidAuthority, e.offset,
	RequestTargetField::Authority);
    if (!pathText || pathText[0] != '/')
      return targetError(RequestTargetParseCode::InvalidPath, 0,
	RequestTargetField::Path);
    if (int i = invalidText(pathText, false); i >= 0)
      return targetError(RequestTargetParseCode::InvalidCharacter, i,
	RequestTargetField::Path);
    auto pq = splitPathQuery(pathText);
    parsed.path = pq.path;
    parsed.query = pq.query;
    parsed.hasQuery = pq.hasQuery;
    parsed.protocol = protocolText;
    parsed.scheme = scheme;
    parsed.form = TargetForm::ExtendedConnect;
    out = parsed;
    return {};
  }

  if (protocolText)
    return targetError(RequestTargetParseCode::InvalidProtocol, 0,
      RequestTargetField::Protocol);
  if (scheme < 0 || scheme >= Scheme::N)
    return targetError(RequestTargetParseCode::InvalidScheme, 0,
      RequestTargetField::Scheme);
  auto e = parseAuthority(parsed.authority, authorityText, 0,
    Scheme::defltPort(scheme), false);
  if (!e.ok())
    return targetError(RequestTargetParseCode::InvalidAuthority, e.offset,
      RequestTargetField::Authority);
  if (pathText.length() == 1 && pathText[0] == '*') {
    if (method != Method::OPTIONS)
      return targetError(RequestTargetParseCode::InvalidMethod, 0,
	RequestTargetField::Path);
    parsed.raw = pathText;
    parsed.path = pathText;
    parsed.scheme = scheme;
    parsed.form = TargetForm::Asterisk;
    out = parsed;
    return {};
  }
  if (!pathText || pathText[0] != '/')
    return targetError(RequestTargetParseCode::InvalidPath, 0,
      RequestTargetField::Path);
  if (int i = invalidText(pathText, false); i >= 0)
    return targetError(RequestTargetParseCode::InvalidCharacter, i,
      RequestTargetField::Path);
  auto pq = splitPathQuery(pathText);
  parsed.raw = pathText;
  parsed.path = pq.path;
  parsed.query = pq.query;
  parsed.hasQuery = pq.hasQuery;
  parsed.scheme = scheme;
  parsed.form = TargetForm::Origin;
  out = parsed;
  return {};
}

} // namespace Zhttp
