//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z HTTP Alt-Svc field values and cache

#include <zlib/ZhttpDiscovery.hh>

#include <zlib/ZuICmp.hh>

#include <zlib/ZhttpCore.hh>

namespace Zhttp {

ZtEnumImplNS(AltSvcParseCode);

namespace AltSvc_ {

inline bool digit(unsigned c) { return c >= '0' && c <= '9'; }
inline bool alpha(unsigned c) {
  c |= 0x20;
  return c >= 'a' && c <= 'z';
}
inline bool alnum(unsigned c) { return alpha(c) || digit(c); }

bool tokenChar(unsigned c)
{
  return alnum(c) || c == '!' || c == '#' || c == '$' || c == '%' ||
    c == '&' || c == '\'' || c == '*' || c == '+' || c == '-' ||
    c == '.' || c == '^' || c == '_' || c == '`' || c == '|' || c == '~';
}

bool uint32(ZuBSpan s, uint32_t &v)
{
  if (!s) return false;
  uint64_t n = 0;
  for (unsigned i = 0; i < s.length(); ++i) {
    if (!digit(s[i])) return false;
    n = n * 10 + unsigned(s[i] - '0');
    if (n > UINT32_MAX) return false;
  }
  v = uint32_t(n);
  return true;
}

AltSvcParseError error(AltSvcParseCode::T code, unsigned offset = 0)
{
  return {offset, code};
}

AltSvcParseError authorityError(URLParseError e)
{
  return error(e.code == URLParseCode::InvalidPort ?
    AltSvcParseCode::InvalidPort : AltSvcParseCode::InvalidAuthority,
    e.offset);
}

} // namespace AltSvc_

bool h3Protocol(ZuBSpan s)
{
  if (s == "h3") return true;
  if (!s.match("h3-") || s.length() == 3) return false;
  for (unsigned i = 3; i < s.length(); ++i)
    if (!AltSvc_::digit(s[i])) return false;
  return true;
}

AltSvcCursor::AltSvcCursor(
  ZuBSpan input, OriginView origin, unsigned maxAlternatives) :
  m_input{input}, m_origin{origin}, m_maxAlternatives{maxAlternatives}
{
  init_();
}

AltSvcCursor::AltSvcCursor(
  ZuSpan<uint8_t> input, OriginView origin, unsigned maxAlternatives) :
  m_input{input}, m_origin{origin}, m_maxAlternatives{maxAlternatives}
{
  init_();
  m_mutable = const_cast<uint8_t *>(m_input.data());
}

void AltSvcCursor::init_()
{
  while (m_input && (m_input[0] == ' ' || m_input[0] == '\t'))
    m_input.offset(1);
  while (m_input && (m_input[m_input.length() - 1] == ' ' ||
      m_input[m_input.length() - 1] == '\t'))
    m_input.trunc(m_input.length() - 1);
  if (m_input == "clear") {
    m_clear = true;
    m_done = true;
  }
}

bool AltSvcCursor::next(AltSvcValue &out)
{
  using namespace AltSvc_;
  if (m_done || !m_error.ok()) return false;
  if (!m_input) {
    m_error = AltSvc_::error(AltSvcParseCode::InvalidSyntax);
    return false;
  }
  if (m_more) {
    while (m_offset < m_input.length() &&
	(m_input[m_offset] == ' ' || m_input[m_offset] == '\t')) ++m_offset;
    if (m_offset >= m_input.length()) {
      m_error = AltSvc_::error(AltSvcParseCode::InvalidSyntax, m_offset);
      return false;
    }
    m_more = false;
  }

  unsigned protocolStart = m_offset;
  while (m_offset < m_input.length() && tokenChar(m_input[m_offset]))
    ++m_offset;
  if (m_offset == protocolStart || m_offset >= m_input.length() ||
      m_input[m_offset++] != '=') {
    m_error = AltSvc_::error(AltSvcParseCode::InvalidSyntax, m_offset);
    return false;
  }
  if (m_count >= m_maxAlternatives) {
    m_error = AltSvc_::error(
      AltSvcParseCode::TooManyAlternatives, protocolStart);
    return false;
  }
  AltSvcValue value;
  value.protocolID = {
    m_input.data() + protocolStart, m_offset - protocolStart - 1};
  value.h3 = h3Protocol(value.protocolID);
  if (m_offset >= m_input.length() || m_input[m_offset++] != '"') {
    m_error = AltSvc_::error(AltSvcParseCode::InvalidSyntax, m_offset);
    return false;
  }
  unsigned authStart = m_offset;
  while (m_offset < m_input.length() && m_input[m_offset] != '"') {
    if (m_input[m_offset] == '\\') {
      m_error = AltSvc_::error(AltSvcParseCode::InvalidSyntax, m_offset);
      return false;
    }
    ++m_offset;
  }
  if (m_offset >= m_input.length()) {
    m_error = AltSvc_::error(AltSvcParseCode::InvalidSyntax, m_offset);
    return false;
  }
  ZuBSpan authorityText{
    m_input.data() + authStart, m_offset - authStart};
  ++m_offset;
  if (authorityText && authorityText[0] == ':') {
    ZuBSpan portText{
      authorityText.data() + 1, authorityText.length() - 1};
    unsigned n = 0;
    if (!uint32(portText, n) || !n || n > 65535) {
      m_error = AltSvc_::error(
	AltSvcParseCode::InvalidPort, authStart + 1);
      return false;
    }
    value.host = m_origin.host;
    value.port = uint16_t(n);
    value.ipv6Literal = m_origin.ipv6Literal;
    value.hostOmitted = true;
    value.normalized = true;
  } else {
    AuthorityView authority;
    URLParseError e;
    if (m_mutable)
      e = parseAuthority(authority,
	ZuSpan<uint8_t>{m_mutable + authStart, authorityText.length()},
	authStart, 0, true);
    else
      e = parseAuthority(
	authority, authorityText, authStart, 0, true);
    if (!e.ok()) {
      m_error = authorityError(e);
      return false;
    }
    value.host = authority.host;
    value.port = authority.port;
    value.ipv6Literal = authority.ipv6Literal;
    value.normalized = authority.normalized;
  }

  for (;;) {
    while (m_offset < m_input.length() &&
	(m_input[m_offset] == ' ' || m_input[m_offset] == '\t')) ++m_offset;
    if (m_offset >= m_input.length() || m_input[m_offset] == ',') break;
    if (m_input[m_offset++] != ';') {
      m_error = AltSvc_::error(
	AltSvcParseCode::InvalidSyntax, m_offset - 1);
      return false;
    }
    while (m_offset < m_input.length() &&
	(m_input[m_offset] == ' ' || m_input[m_offset] == '\t')) ++m_offset;
    unsigned keyStart = m_offset;
    while (m_offset < m_input.length() && tokenChar(m_input[m_offset]))
      ++m_offset;
    if (m_offset == keyStart) {
      m_error = AltSvc_::error(AltSvcParseCode::InvalidSyntax, m_offset);
      return false;
    }
    ZuBSpan key{m_input.data() + keyStart, m_offset - keyStart};
    ZuBSpan param;
    if (m_offset < m_input.length() && m_input[m_offset] == '=') {
      ++m_offset;
      unsigned valueStart = m_offset;
      while (m_offset < m_input.length() && tokenChar(m_input[m_offset]))
	++m_offset;
      if (m_offset == valueStart) {
	m_error = AltSvc_::error(AltSvcParseCode::InvalidSyntax, m_offset);
	return false;
      }
      param = {m_input.data() + valueStart, m_offset - valueStart};
    }
    if (ZuICmp<ZuBSpan>::equals(key, "ma")) {
      if (!uint32(param, value.maxAge)) {
	m_error = AltSvc_::error(AltSvcParseCode::InvalidSyntax, keyStart);
	return false;
      }
    } else if (ZuICmp<ZuBSpan>::equals(key, "persist")) {
      if (param == "1") value.persist = true;
      else if (param != "0") {
	m_error = AltSvc_::error(AltSvcParseCode::InvalidSyntax, keyStart);
	return false;
      }
    }
  }
  ++m_count;
  if (m_offset < m_input.length()) {
    ++m_offset;
    m_more = true;
  } else
    m_done = true;
  out = value;
  return true;
}

AltSvcValueStorage::AltSvcValueStorage(const AltSvcValue &value) :
  maxAge{value.maxAge}, port{value.port}, persist{value.persist}, h3{value.h3},
  ipv6Literal{value.ipv6Literal}, hostOmitted{value.hostOmitted}
{
  data << value.protocolID;
  protocolID.length = data.length();
  if (!hostOmitted) {
    host.offset = data.length();
    data << value.host;
    host.length = data.length() - host.offset;
    data.ensure(data.length() + 1);
    lowerASCII({data.data() + host.offset, host.length});
  }
}

AltSvcValue AltSvcValueStorage::view(OriginView origin) const
{
  auto bytes = data.data();
  auto span = [bytes](Part part) {
    return ZuBSpan{bytes + part.offset, part.length};
  };
  return {
    span(protocolID),
    hostOmitted ? origin.host : span(host),
    maxAge, port, persist, h3, ipv6Literal, hostOmitted, true
  };
}

template <typename Span>
bool AltSvcCache::update_(
  const Origin &origin, Span field, unsigned maxAlternatives, ZuTime now)
{
  AltSvcCursor validate{field, origin.view(), maxAlternatives};
  AltSvcValue value;
  while (validate.next(value)) { }
  if (!validate.error().ok()) return false;
  if (validate.clear()) {
    m_entries->del(origin);
    return true;
  }
  auto existing = m_entries->find(origin);
  bool exists = !!existing;
  if (!exists && m_entries->count_() >= m_maxOrigins) return false;

  AltSvcEntry entry;
  AltSvcCursor snapshot{field, origin.view(), maxAlternatives};
  while (snapshot.next(value)) {
    if (!value.maxAge) continue;
    entry.values.push(CachedAltSvc{
      .value = AltSvcValueStorage{value},
      .expires = now + ZuTime{int64_t(value.maxAge)}
    });
  }
  if (!snapshot.error().ok()) return false;
  if (entry.values) {
    m_entries->add(origin, ZuMv(entry));
    if (existing) m_entries->delNode(existing);
  } else if (existing)
    m_entries->delNode(existing);
  return true;
}

bool AltSvcCache::update(
  const Origin &origin, ZuBSpan field,
  unsigned maxAlternatives, ZuTime now)
{
  return update_(origin, field, maxAlternatives, now);
}

bool AltSvcCache::update(
  const Origin &origin, ZuSpan<uint8_t> field,
  unsigned maxAlternatives, ZuTime now)
{
  return update_(origin, field, maxAlternatives, now);
}

template bool AltSvcCache::update_<ZuBSpan>(
  const Origin &, ZuBSpan, unsigned, ZuTime);
template bool AltSvcCache::update_<ZuSpan<uint8_t>>(
  const Origin &, ZuSpan<uint8_t>, unsigned, ZuTime);

} // namespace Zhttp
