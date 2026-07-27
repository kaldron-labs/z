//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z http library - HTTPS/SVCB records and HTTP endpoint discovery

#include <zlib/ZhttpDiscovery.hh>

#include <string.h>

#include <zlib/ZuSort.hh>

#include <zlib/ZiResolver.hh>

namespace Zhttp {
namespace Discovery_ {

enum {
  DNSHeaderLen = 12,
  NameMaxSteps = 64,
  SvcMandatory = 0,
  SvcALPN = 1,
  SvcNoDefaultALPN = 2,
  SvcPort = 3,
  SvcIPv4Hint = 4,
  SvcIPv6Hint = 6
};

inline unsigned lower(unsigned c) {
  return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

inline uint16_t u16(const uint8_t *p)
{
  return (uint16_t(p[0])<<8) | p[1];
}

DiscoveryError error(int code, unsigned offset)
{
  return {offset, int8_t(code)};
}

ZmRef<ZiResolver_::Query> query(
  void *, ZiResolver_::Host host, uint16_t type, uint16_t class_,
  ZiResolver_::QueryFn fn)
{
  return ZiResolver::query(ZuMv(host), type, class_, ZuMv(fn));
}

ZmRef<ZiResolver_::Query> resolve(
  void *, ZiResolver_::Host host, ZiResolver_::ResolveFn fn)
{
  return ZiResolver::resolve(ZuMv(host), ZuMv(fn));
}

void cancel(void *, ZmRef<ZiResolver_::Query> resolver)
{
  ZiResolver::cancel(ZuMv(resolver));
}

const DiscoveryResolver resolver{
  .query = query,
  .resolve = resolve,
  .cancel = cancel
};

bool name(
  const uint8_t *msg, unsigned msgLen, unsigned &off,
  URLString &out)
{
  unsigned p = off, next = off, steps = 0;
  bool jumped = false;
  out.length(0);
  for (;;) {
    if (p >= msgLen || ++steps > NameMaxSteps) return false;
    uint8_t n = msg[p++];
    if (!n) {
      if (!jumped) next = p;
      off = next;
      if (!out.length()) out = ".";
      return true;
    }
    if ((n & 0xc0) == 0xc0) {
      if (p >= msgLen) return false;
      unsigned ptr = ((n & 0x3f)<<8) | msg[p++];
      if (ptr >= msgLen) return false;
      if (!jumped) next = p;
      jumped = true;
      p = ptr;
      continue;
    }
    if (n & 0xc0 || p + n > msgLen) return false;
    if (out.length()) out << '.';
    for (unsigned i = 0; i < n; ++i) out << char(lower(msg[p + i]));
    p += n;
    if (!jumped) next = p;
  }
}

bool knownMandatory(uint16_t key)
{
  switch (key) {
    case SvcMandatory:
    case SvcALPN:
    case SvcNoDefaultALPN:
    case SvcPort:
    case SvcIPv4Hint:
    case SvcIPv6Hint:
      return true;
    default:
      return false;
  }
}

bool sameName(ZuCSpan parsed, ZuCSpan owner)
{
  unsigned n = owner.length();
  if (n && owner[n - 1] == '.') --n;
  if (parsed.length() != n) return false;
  for (unsigned i = 0; i < n; ++i)
    if (parsed[i] != lower(owner[i])) return false;
  return true;
}

DiscoveryError params(
  const uint8_t *msg, unsigned end, unsigned &off,
  SvcRecord &record, const DiscoveryLimits &limits)
{
  uint16_t last = 0;
  uint16_t mandatory = 0;
  uint16_t present = 0;
  bool haveLast = false;
  while (off < end) {
    if (off + 4 > end) return error(DiscoveryCode::Malformed, off);
    unsigned paramOffset = off;
    uint16_t key = u16(msg + off);
    uint16_t len = u16(msg + off + 2);
    off += 4;
    if (haveLast && key <= last)
      return error(DiscoveryCode::Malformed, paramOffset);
    haveLast = true;
    last = key;
    if (key <= SvcIPv6Hint) present |= uint16_t(1U<<key);
    if (off + len > end) return error(DiscoveryCode::Malformed, off);
    const uint8_t *value = msg + off;
    switch (key) {
      case SvcMandatory:
	if (len & 1) return error(DiscoveryCode::Malformed, off);
	for (unsigned i = 0, previous = 0; i < len; i += 2) {
	  uint16_t mandatoryKey = u16(value + i);
	  if (!mandatoryKey || (i && mandatoryKey <= previous))
	    return error(DiscoveryCode::Malformed, off + i);
	  previous = mandatoryKey;
	  if (!knownMandatory(mandatoryKey))
	    record.unsupportedMandatory = true;
	  else
	    mandatory |= uint16_t(1U<<mandatoryKey);
	}
	break;
      case SvcALPN: {
	record.hasALPN = true;
	unsigned i = 0;
	while (i < len) {
	  unsigned n = value[i++];
	  if (!n || i + n > len)
	    return error(DiscoveryCode::Malformed, off + i - 1);
	  if (Zhttp::h3ALPN(ZuCSpan{value + i, n})) record.h3 = true;
	  i += n;
	}
	break;
      }
      case SvcNoDefaultALPN:
	if (len) return error(DiscoveryCode::Malformed, off);
	record.noDefaultALPN = true;
	break;
      case SvcPort:
	if (len != 2) return error(DiscoveryCode::Malformed, off);
	record.port = u16(value);
	if (!record.port) return error(DiscoveryCode::Malformed, off);
	break;
      case SvcIPv4Hint:
	if (len % 4) return error(DiscoveryCode::Malformed, off);
	if (record.ipv4Hints.length() + len / 4 > limits.maxHints)
	  return error(DiscoveryCode::HintLimit, off);
	for (unsigned i = 0; i < len; i += 4) {
	  in_addr addr;
	  memcpy(&addr, value + i, sizeof(addr));
	  record.ipv4Hints.push(ZiIP{addr});
	}
	break;
      case SvcIPv6Hint:
	if (len % sizeof(in6_addr))
	  return error(DiscoveryCode::Malformed, off);
	if (record.ipv6Hints.length() + len / sizeof(in6_addr) >
	    limits.maxHints)
	  return error(DiscoveryCode::HintLimit, off);
	for (unsigned i = 0; i < len; i += sizeof(in6_addr)) {
	  in6_addr addr;
	  memcpy(&addr, value + i, sizeof(addr));
	  record.ipv6Hints.push(ZiIP{addr});
	}
	break;
      default:
	break;
    }
    off += len;
  }
  if (mandatory & ~present) record.unsupportedMandatory = true;
  return {};
}

DiscoveryError rdata(
  const uint8_t *msg, unsigned &off, unsigned end,
  ZuCSpan owner, SvcRecord &record, const DiscoveryLimits &limits)
{
  if (off + 2 > end) return error(DiscoveryCode::Malformed, off);
  record.priority = u16(msg + off);
  off += 2;
  if (!name(msg, end, off, record.target))
    return error(DiscoveryCode::Malformed, off);
  if (record.target == ".") {
    record.target.length(0);
    for (unsigned i = 0; i < owner.length(); ++i)
      record.target << char(lower(owner[i]));
  }
  if (!record.priority && off != end)
    return error(DiscoveryCode::Malformed, off);
  return params(msg, end, off, record, limits);
}

} // namespace Discovery_

DiscoveryError parseHTTPS(
  SvcRecords &out, ZuBSpan span, ZuCSpan owner,
  const DiscoveryLimits &limits)
{
  using namespace Discovery_;
  SvcRecords records;
  const uint8_t *msg = span.data();
  unsigned msgLen = span.length();
  if (msgLen < DNSHeaderLen)
    return error(DiscoveryCode::Malformed, msgLen);

  unsigned qd = u16(msg + 4);
  unsigned an = u16(msg + 6);
  unsigned ns = u16(msg + 8);
  unsigned ar = u16(msg + 10);
  unsigned off = DNSHeaderLen;
  URLString name_;
  for (unsigned i = 0; i < qd; ++i) {
    if (!name(msg, msgLen, off, name_) || off + 4 > msgLen)
      return error(DiscoveryCode::Malformed, off);
    off += 4;
  }

  for (unsigned i = 0, n = an + ns + ar; i < n; ++i) {
    if (!name(msg, msgLen, off, name_) || off + 10 > msgLen)
      return error(DiscoveryCode::Malformed, off);
    uint16_t type = u16(msg + off);
    uint16_t class_ = u16(msg + off + 2);
    uint16_t rdlen = u16(msg + off + 8);
    off += 10;
    if (off + rdlen > msgLen)
      return error(DiscoveryCode::Malformed, off);
    unsigned end = off + rdlen;
    if (i < an && sameName(name_, owner) &&
	(type == ZiResolver_::DNSType::HTTPS ||
	type == ZiResolver_::DNSType::SVCB) &&
	class_ == ZiResolver_::DNSClass::IN) {
      if (records.length() >= limits.maxRecords)
	return error(DiscoveryCode::RecordLimit, off);
      SvcRecord record;
      record.order = records.length();
      unsigned recordOff = off;
      auto e = rdata(msg, recordOff, end, owner, record, limits);
      if (!e.ok()) return e;
      records.push(ZuMv(record));
    }
    off = end;
  }
  if (!records.length())
    return error(DiscoveryCode::NoRecord, off);
  unsigned aliases = 0;
  for (unsigned i = 0; i < records.length(); ++i)
    aliases += records[i].alias();
  if (aliases && (aliases != 1 || records.length() != 1))
    return error(DiscoveryCode::Malformed, off);
  ZuSort(records.data(), records.length(), [](const auto &l, const auto &r) {
    int cmp = int(l.priority > r.priority) - int(l.priority < r.priority);
    return cmp ? cmp : int(l.order > r.order) - int(l.order < r.order);
  });
  out = ZuMv(records);
  return {};
}

DiscoveryRequest::DiscoveryRequest(
  Origin origin, ZuCSpan host, uint16_t port, bool blindFallback,
  DiscoveryLimits limits, DiscoveryFn fn, const DiscoveryResolver *resolver)
:
  m_origin{ZuMv(origin)}, m_host{host}, m_query{host}, m_fn{ZuMv(fn)},
  m_resolverOps{resolver ? resolver : &Discovery_::resolver},
  m_limits{limits}, m_port{port}, m_blindFallback{blindFallback}
{
}

DiscoveryRequest::DiscoveryRequest(
  Endpoint endpoint, DiscoveryLimits limits, DiscoveryFn fn,
  const DiscoveryResolver *resolver)
:
  m_origin{endpoint.origin}, m_directEndpoint{ZuMv(endpoint)},
  m_host{m_directEndpoint.tlsName}, m_target{m_directEndpoint.target},
  m_fn{ZuMv(fn)}, m_resolverOps{resolver ? resolver : &Discovery_::resolver},
  m_limits{limits}, m_port{m_directEndpoint.port},
  m_direct{true}
{
}

void DiscoveryRequest::start()
{
  if (m_direct) {
    resolveNext();
    return;
  }
  m_aliases.push(m_query);
  query();
}

void DiscoveryRequest::cancel()
{
  m_resolverOps->cancel(m_resolverOps->context, ZuMv(m_resolver));
  finish({0, DiscoveryCode::Cancelled});
}

void DiscoveryRequest::query()
{
  m_resolver = m_resolverOps->query(
    m_resolverOps->context, m_query,
    ZiResolver_::DNSType::HTTPS, ZiResolver_::DNSClass::IN,
    ZiResolver_::QueryFn{[
      request = ZmRef<DiscoveryRequest>{this}](auto result) mutable {
	request->queried(ZuMv(result));
    }});
}

void DiscoveryRequest::queried(ZiResolver_::QueryResult result)
{
  if (m_done.load_()) return;
  if (result.template is<ZiResolver_::Event>()) {
    if (m_blindFallback && !m_advertised) {
      m_blind = true;
      m_records.length(0);
      resolveNext();
    } else
      finish({0, DiscoveryCode::NoRecord});
    return;
  }

  auto msg = ZuMv(result).template p<ZiResolver_::DNSMsg>();
  SvcRecords records;
  auto e = parseHTTPS(
    records, ZuBSpan{msg.buf.data(), msg.buf.length()}, m_query, m_limits);
  if (!e.ok()) {
    if (e.code == DiscoveryCode::NoRecord &&
	m_blindFallback && !m_advertised) {
      m_blind = true;
      resolveNext();
    } else
      finish(e);
    return;
  }

  for (unsigned i = 0; i < records.length(); ++i) {
    if (!records[i].alias()) continue;
    if (m_aliases.length() > m_limits.maxAliasDepth) {
      finish({0, DiscoveryCode::AliasLimit});
      return;
    }
    for (unsigned j = 0; j < m_aliases.length(); ++j)
      if (m_aliases[j] == records[i].target) {
	finish({0, DiscoveryCode::AliasLoop});
	return;
      }
    m_query = records[i].target;
    m_aliases.push(m_query);
    query();
    return;
  }

  m_records = ZuMv(records);
  m_record = 0;
  m_advertised = true;
  resolveNext();
}

bool DiscoveryRequest::add(Endpoint endpoint)
{
  for (unsigned i = 0; i < m_endpoints.length(); ++i)
    if (m_endpoints[i].ip == endpoint.ip &&
	m_endpoints[i].port == endpoint.port &&
	m_endpoints[i].target == endpoint.target)
      return true;
  if (m_endpoints.length() >= m_limits.maxEndpoints) {
    finish({0, DiscoveryCode::EndpointLimit});
    return false;
  }
  m_endpoints.push(ZuMv(endpoint));
  return true;
}

void DiscoveryRequest::resolveNext()
{
  if (m_done.load_()) return;
  if (m_direct) {
    m_resolver = m_resolverOps->resolve(
      m_resolverOps->context, m_target,
      ZiResolver_::ResolveFn{[
	request = ZmRef<DiscoveryRequest>{this}](auto result) mutable {
	  return request->resolved(ZuMv(result));
	}});
    return;
  }
  if (m_blind) {
    m_target = m_host;
    m_resolver = m_resolverOps->resolve(
      m_resolverOps->context, m_host,
      ZiResolver_::ResolveFn{[
	request = ZmRef<DiscoveryRequest>{this}](auto result) mutable {
	  return request->resolved(ZuMv(result));
	}});
    return;
  }

  while (m_record < m_records.length()) {
    auto &record = m_records[m_record++];
    if (record.unsupportedMandatory || !record.h3 ||
	(record.noDefaultALPN && !record.hasALPN))
      continue;
    m_target = record.target;
    uint16_t port = record.port ? record.port : m_port;
    for (unsigned i = 0; i < record.ipv4Hints.length(); ++i)
      if (!add({m_origin, m_target, m_host, record.ipv4Hints[i], port,
	    record.priority, EndpointSource::IPv4Hint, Version::H3}))
	return;
    for (unsigned i = 0; i < record.ipv6Hints.length(); ++i)
      if (!add({m_origin, m_target, m_host, record.ipv6Hints[i], port,
	    record.priority, EndpointSource::IPv6Hint, Version::H3}))
	return;
    m_resolver = m_resolverOps->resolve(
      m_resolverOps->context, m_target,
      ZiResolver_::ResolveFn{[
	request = ZmRef<DiscoveryRequest>{this}](auto result) mutable {
	  return request->resolved(ZuMv(result));
	}});
    return;
  }
  finish(m_endpoints.length() ? DiscoveryError{} :
    DiscoveryError{0, DiscoveryCode::UnsupportedMandatory});
}

bool DiscoveryRequest::resolved(ZiResolver_::ResolveResult result)
{
  if (m_done.load_()) return false;
  if (result.template is<ZiResolver_::Event>()) {
    if (m_blind || m_direct)
      finish({0, DiscoveryCode::ResolveFailure});
    else
      resolveNext();
    return false;
  }
  if (result.template is<void>()) {
    if (m_blind || m_direct)
      finish(m_endpoints.length() ? DiscoveryError{} :
	DiscoveryError{0, DiscoveryCode::ResolveFailure});
    else
      resolveNext();
    return false;
  }
  if (m_direct) {
    Endpoint endpoint = m_directEndpoint;
    endpoint.ip = result.template p<ZiIP>();
    return add(ZuMv(endpoint));
  }
  auto record = m_blind ? nullptr : &m_records[m_record - 1];
  uint16_t port = !record || !record->port ? m_port : record->port;
  int source = m_blind ? EndpointSource::Origin : EndpointSource::HTTPS;
  uint16_t priority = record ? record->priority : 0;
  return add({m_origin, m_target, m_host,
    result.template p<ZiIP>(), port, priority,
    int8_t(source), Version::H3});
}

void DiscoveryRequest::finish(DiscoveryError error)
{
  if (m_done.cmpXch(1, 0)) return;
  m_resolver = nullptr;
  auto fn = ZuMv(m_fn);
  fn(error, ZuMv(m_endpoints));
}

DiscoveryRequestRef discoverH3(
  Origin origin, ZuCSpan host, uint16_t port, bool blindFallback,
  DiscoveryLimits limits, DiscoveryFn fn,
  const DiscoveryResolver *resolver)
{
  DiscoveryRequestRef request = new DiscoveryRequest{
    ZuMv(origin), host, port, blindFallback, limits, ZuMv(fn), resolver};
#ifdef ZmObject_DEBUG
  request->ZmObject::debug();
#endif
  request->start();
  return request;
}

DiscoveryRequestRef resolveH3(
  Endpoint endpoint, DiscoveryLimits limits, DiscoveryFn fn,
  const DiscoveryResolver *resolver)
{
  DiscoveryRequestRef request = new DiscoveryRequest{
    ZuMv(endpoint), limits, ZuMv(fn), resolver};
#ifdef ZmObject_DEBUG
  request->ZmObject::debug();
#endif
  request->start();
  return request;
}

} // namespace Zhttp
