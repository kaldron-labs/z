//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <limits.h>
#include <zlib/ZuBase64.hh>
#include <zlib/ZuBox.hh>
#include <zlib/ZuPercent.hh>
#include <zlib/ZgrpcMeta.hh>

namespace Zgrpc {

static int64_t timeoutUnit(char unit)
{
  switch (unit) {
    case 'n': return 1;
    case 'u': return 1000;
    case 'm': return 1000000;
    case 'S': return 1000000000;
    case 'M': return int64_t(60) * 1000000000;
    case 'H': return int64_t(3600) * 1000000000;
    default: return 0;
  }
}

Timeout encodeTimeout(int64_t nanos)
{
  if (nanos <= 0) return "0n";
  int64_t unit = 1;
  char suffix = 'n';
  for (;;) {
    auto n = nanos/unit + bool(nanos%unit);
    if (n <= 99999999) {
      Timeout out;
      // n fits eight digits. Print directly: the generic array stream
      // reserves the integer type's worst-case width, which exceeds this
      // protocol-sized destination even when the value itself fits.
      unsigned length = ZuBoxed(unsigned(n)).print(out.data());
      out.length(length + 1);
      out[length] = suffix;
      return out;
    }
    switch (suffix) {
      case 'n': suffix = 'u'; break;
      case 'u': suffix = 'm'; break;
      case 'm': suffix = 'S'; break;
      case 'S': suffix = 'M'; break;
      case 'M': suffix = 'H'; break;
    }
    unit = timeoutUnit(suffix);
  }
}

bool decodeTimeout(ZuCSpan input, int64_t &nanos)
{
  unsigned n = input.length();
  if (n < 2 || n > 9) return false;
  auto unit = timeoutUnit(input[n - 1]);
  if (!unit) return false;
  int64_t value = 0;
  for (unsigned i = 0; i < n - 1; i++) {
    auto c = input[i];
    if (c < '0' || c > '9') return false;
    value = value * 10 + (c - '0');
  }
  nanos = value > INT64_MAX/unit ? INT64_MAX : value * unit;
  return true;
}

bool decodeCode(ZuCSpan input, Code::T &code)
{
  if (!input.length()) return false;
  unsigned value = 0;
  for (unsigned i = 0, n = input.length(); i < n; i++) {
    auto c = input[i];
    if (c < '0' || c > '9') return false;
    value = value * 10 + (c - '0');
    if (value >= Code::N) return false;
  }
  code = value;
  return true;
}

Code::T httpCode(unsigned status)
{
  switch (status) {
    case 400: return Code::INTERNAL;
    case 401: return Code::UNAUTHENTICATED;
    case 403: return Code::PERMISSION_DENIED;
    case 404: return Code::UNIMPLEMENTED;
    case 429: case 502: case 503: case 504: return Code::UNAVAILABLE;
    default: return Code::UNKNOWN;
  }
}

bool contentType(ZuCSpan value)
{
  constexpr auto bare = "application/grpc"_z;
  constexpr auto proto = "application/grpc+proto"_z;
  return value == bare || value == proto;
}

String encodeText(ZuBSpan input)
{
  String out;
  for (unsigned i = 0, n = input.length(); i < n; i++) {
    auto c = input[i];
    if (c < 0x20 || c >= 0x7f || c == '%')
      out << '%' << ZuPercent::digit(c>>4) << ZuPercent::digit(c & 15);
    else
      out << char(c);
  }
  return out;
}

String decodeText(ZuCSpan input)
{
  String out;
  for (unsigned i = 0, n = input.length(); i < n; i++) {
    auto c = input[i];
    if (c == '%' && n - i >= 3) {
      int hi = ZuPercent::hex(input[i + 1]), lo = ZuPercent::hex(input[i + 2]);
      if (hi >= 0 && lo >= 0) { c = (hi<<4) | lo; i += 2; }
    }
    out << c;
  }
  return out;
}

// Return the validated data length without padding, before destination growth.
static bool binarySize(ZuBSpan input, unsigned &data, unsigned &size)
{
  unsigned n = input.length();
  data = n;
  while (data && input[data - 1] == '=') --data;
  unsigned pad = n - data, rem = data & 3;
  if (pad > 2 || rem == 1 || (pad && ((n & 3) || pad != 4 - rem))) return false;
  for (unsigned i = 0; i < data; i++)
    if (!ZuBase64::is(input[i])) return false;
  size = ZuBase64::declen(data);
  return true;
}

bool decodeBinary(ZuCSpan input, Bytes &out, unsigned limit)
{
  unsigned data, size;
  if (!binarySize(input, data, size) || size > limit) return false;
  out.length(size);
  if (size) ZuBase64::decode(out, ZuBSpan{input.data(), data});
  return true;
}

String encodeBinary(ZuBSpan input)
{
  String out;
  auto n = ZuBase64::enclen<false>(input.length());
  out.length(n);
  if (n) ZuBase64::encode<false>(out, input);
  return out;
}

unsigned BinaryPrint::length(const BinaryValue &value)
{
  return ZuBase64::enclen<false>(value.bytes.length());
}

unsigned BinaryPrint::print(char *dest, unsigned size, const BinaryValue &value)
{
  return ZuBase64::encode<false>({dest, size}, value.bytes);
}

unsigned TextPrint::length(const TextValue &value)
{
  unsigned length = 0;
  for (auto c : value.bytes) length += c < 0x20 || c >= 0x7f || c == '%' ? 3 : 1;
  return length;
}

unsigned TextPrint::print(char *dest, unsigned, const TextValue &value)
{
  auto p = dest;
  for (auto c : value.bytes) {
    if (c < 0x20 || c >= 0x7f || c == '%') {
      *p++ = '%';
      *p++ = ZuPercent::digit(c>>4);
      *p++ = ZuPercent::digit(c & 15);
    } else *p++ = c;
  }
  return p - dest;
}

ZtEnumImplNS(MetaError);

int Protocol::match(ZuBSpan name)
{
  static constexpr auto matcher = ZuMatcher<Protocol>();
  return matcher.exact(name);
}

static bool grpcName(ZuBSpan name)
{
  return name.length() >= 5 && !memcmp(name.data(), "grpc-", 5);
}

static bool binaryName(ZuBSpan name)
{
  auto n = name.length();
  return n >= 4 && !memcmp(name.data() + n - 4, "-bin", 4);
}

static bool metaName(ZuBSpan name)
{
  if (!name.length()) return false;
  for (unsigned i = 0, n = name.length(); i < n; i++) {
    auto c = name[i];
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) continue;
    switch (c) {
      case '_': case '-': case '.': break;
      default: return false;
    }
  }
  return true;
}

void Metadata::reset(unsigned limit)
{
  m_fields.null();
  m_bytes.null();
  m_limit = limit;
  m_size = 0;
  m_error = MetaError::OK;
}

bool Metadata::add(ZuBSpan name, ZuBSpan value)
{
  return add_(name, value, -1, false, false);
}

bool Metadata::receive(ZuBSpan name, ZuBSpan value)
{
  return add_(name, value, -1, false, true);
}

bool Metadata::add_(ZuBSpan name, ZuBSpan value, int key,
  bool fixed, bool wire, bool checked)
{
  if (failed()) return false;
  if (!metaName(name)) return fail(MetaError::Name);
  if (!checked && (grpcName(name) || Protocol::match(name) >= 0))
    return fail(MetaError::Reserved);
  bool binary = binaryName(name);
  // Text metadata is opaque printable ASCII; only binary metadata is split.
  if (!binary) {
    for (unsigned i = 0, n = value.length(); i < n; i++)
      if (value[i] < 0x20 || value[i] > 0x7e) return fail(MetaError::Name);
  }
  unsigned namePos = m_bytes.length();
  bool first = true;
  unsigned pos = 0, length = value.length();
  do {
    unsigned end = length;
    if (binary && wire) {
      for (end = pos; end < length && value[end] != ','; end++);
    }
    auto part = ZuBSpan{value.data() ? value.data() + pos : nullptr, end - pos};
    unsigned data = part.length(), size = data;
    if (binary && wire) {
      // HTTP field coalescing can insert OWS around comma-separated values.
      while (part.length() && (part[0] == ' ' || part[0] == '\t')) part.offset(1);
      while (part.length()) {
	auto c = part[part.length() - 1];
	if (c != ' ' && c != '\t') break;
	part.trunc(part.length() - 1);
      }
      if (!binarySize(part, data, size)) return fail(MetaError::Binary);
    }
    // RFC 7540 field-list accounting charges 32 octets per entry. Charging
    // each decoded binary entry bounds even a joined list of empty values.
    uint64_t cost = uint64_t(name.length()) + size + 32;
    if (cost > uint64_t(m_limit) - m_size) return fail(MetaError::Limit);
    unsigned old = m_bytes.length();
    unsigned nameLen = fixed || !first ? 0 : unsigned(name.length());
    m_bytes.length(old + nameLen + size);
    if (nameLen) memcpy(m_bytes.data() + old, name.data(), nameLen);
    unsigned valuePos = old + nameLen;
    if (size) {
      auto dest = m_bytes.data() + valuePos;
      if (binary && wire) ZuBase64::decode({dest, size}, {part.data(), data});
      else memcpy(dest, part.data(), size);
    }
    new (m_fields.push()) MetaField{
      fixed ? name : ZuBSpan{}, namePos, unsigned(name.length()),
      valuePos, size, key, binary};
    m_size += unsigned(cost);
    first = false;
    if (end == length) break;
    pos = end + 1;
  } while (true);
  return true;
}

void MetaRx::reset(bool response, unsigned limit)
{
  m_initial.reset(limit);
  m_trailers.reset(limit);
  m_status = {};
  m_timeout = 0;
  m_limit = limit;
  m_httpStatus = m_seen = m_controlBytes = 0;
  m_statusSection = m_terminalSection = Zhttp::FieldSection::Invalid;
  m_error = MetaError::OK;
  m_response = response;
  m_accepted = m_trailing = m_finished = false;
}

bool MetaRx::fail(MetaError::T error)
{
  if (failed()) return false;
  m_error = error;
  switch (error) {
    case MetaError::Limit: m_status.code = Code::RESOURCE_EXHAUSTED; break;
    case MetaError::Encoding: m_status.code = Code::UNIMPLEMENTED; break;
    case MetaError::MissingStatus: m_status.code = httpCode(m_httpStatus); break;
    default: m_status.code = Code::INTERNAL; break;
  }
  return false;
}

bool MetaRx::section_(Zhttp::FieldSection::T section)
{
  if (failed()) return false;
  if (m_finished) return fail(MetaError::Section);
  switch (section) {
    case Zhttp::FieldSection::Final:
      return !m_accepted && !m_trailing ? true : fail(MetaError::Section);
    case Zhttp::FieldSection::Trailers:
      if (!m_response || !m_accepted || trailersOnly()) return fail(MetaError::Section);
      if (!m_trailing) { m_trailing = true; m_controlBytes = 0; }
      return true;
    default: return fail(MetaError::Section);
  }
}

bool MetaRx::header(Zhttp::FieldSection::T section, ZuBSpan name, ZuBSpan value)
{
  if (section == Zhttp::FieldSection::Informational) return true;
  int id = Protocol::match(name);
  if (id >= 0) return control_(section, id, value);
  return application_(section, name, value, false);
}

bool MetaRx::application_(Zhttp::FieldSection::T section,
  ZuBSpan name, ZuBSpan value, bool fixed, int key)
{
  if (section == Zhttp::FieldSection::Informational) return true;
  if (!section_(section)) return false;
  // Unknown grpc-* controls are reserved for protocol extensions. They never
  // enter the application's dynamic metadata collection.
  if (grpcName(name)) return true;
  auto &meta = section == Zhttp::FieldSection::Trailers ? m_trailers : m_initial;
  if (!meta.add_(name, value, key, fixed, true, true)) return fail(meta.error());
  return true;
}

bool MetaRx::control_(Zhttp::FieldSection::T section, unsigned id, ZuBSpan value)
{
  if (section == Zhttp::FieldSection::Informational) return true;
  if (!section_(section)) return false;
  if (id == Protocol::ContentLength) return true; // HTTP owns its validation
  bool terminal = id == Protocol::Status || id == Protocol::Message || id == Protocol::Details;
  if (terminal) {
    if (!m_response) return fail(MetaError::Section);
    if (m_terminalSection != Zhttp::FieldSection::Invalid && m_terminalSection != section)
      return fail(MetaError::Section);
    m_terminalSection = section;
  } else if (section != Zhttp::FieldSection::Final)
    return fail(MetaError::Section);
  if (id != Protocol::AcceptEncoding && (m_seen & (1U<<id))) return fail(MetaError::Duplicate);
  uint64_t cost = uint64_t(value.length()) + ZuSwitch::dispatch<Protocol::N>(id,
    [](auto I) { return ZuType<I, Protocol::Keys>{}().length(); }, 0U) + 32;
  if (cost > uint64_t(m_limit) - m_controlBytes) return fail(MetaError::Limit);
  m_controlBytes += unsigned(cost);
  m_seen |= 1U<<id;
  switch (id) {
    case Protocol::ContentType:
      if (!contentType(value)) return fail(MetaError::ContentType);
      break;
    case Protocol::TE:
      if (m_response || value != ZuBSpan{"trailers"}) return fail(MetaError::Section);
      break;
    case Protocol::Timeout:
      if (m_response) return fail(MetaError::Section);
      if (!decodeTimeout(value, m_timeout)) return fail(MetaError::Timeout);
      break;
    case Protocol::Status:
      if (!decodeCode(value, m_status.code)) return fail(MetaError::Status);
      m_statusSection = section;
      break;
    case Protocol::Message: m_status.message = decodeText(value); break;
    case Protocol::Details:
      if (!decodeBinary(value, m_status.details, m_limit)) return fail(MetaError::Binary);
      break;
    case Protocol::Encoding:
      if (value != ZuBSpan{"identity"}) return fail(MetaError::Encoding);
      break;
    case Protocol::AcceptEncoding: case Protocol::ContentLength: break;
  }
  return true;
}

bool MetaRx::initial()
{
  if (failed()) return false;
  if (m_accepted || m_finished) return fail(MetaError::Section);
  if (!(m_seen & (1U<<Protocol::ContentType))) return fail(MetaError::ContentType);
  if (!m_response && !(m_seen & (1U<<Protocol::TE))) return fail(MetaError::MissingTE);
  m_accepted = true;
  return true;
}

bool MetaRx::end()
{
  if (failed()) return false;
  if (!m_accepted || m_finished) return fail(MetaError::Section);
  if (m_response && !(m_seen & (1U<<Protocol::Status))) return fail(MetaError::MissingStatus);
  m_finished = true;
  return true;
}

} // Zgrpc
