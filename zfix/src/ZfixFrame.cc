//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZfixFrame.hh>
#include <zlib/Zu_ntoa.hh>

namespace Zfix::Fmt {

ZfStructImpl(Header);
ZfStructImpl(Header, FIX);
ZfStructImpl(Trailer);
ZfStructImpl(Trailer, FIX);

static FrameResult failed(unsigned code, unsigned tag, unsigned offset)
{
  FrameResult r;
  r.state = FrameState::Error;
  r.error = {code, tag, offset};
  return r;
}

FrameResult frame(ZuCSpan input, Limits limits)
{
  // Coalesced input may exceed the per-frame limit; inspect only the first
  // frame's bounded prefix rather than rejecting all queued messages together.
  ZuCSpan bounded{input.data(), input.length() < limits.frameBytes ?
    input.length() : limits.frameBytes};
  Token begin{}, length{}, type{};
  auto leading = [bounded, limits](unsigned offset, unsigned tag, Token &out) {
    auto e = scan(bounded, offset, out);
    if (e) {
      if (e.code == ErrorCode::Truncated) {
	if (bounded.length() == limits.frameBytes ||
	    (out.offset > offset && bounded.length() - out.offset > limits.fieldBytes))
	  return Error{ErrorCode::Size, tag, offset};
      }
      return e;
    }
    if (out.tag != tag) return Error{ErrorCode::Envelope, out.tag, offset};
    if (!out.length) return Error{ErrorCode::Value, tag, out.offset};
    if (out.length > limits.fieldBytes) return Error{ErrorCode::Size, tag, out.offset};
    if (tag == 8 && !text({bounded.data() + out.offset, out.length}))
      return Error{ErrorCode::Value, tag, out.offset};
    return Error{};
  };
  if (auto e = leading(0, 8, begin)) {
    if (e.code == ErrorCode::Truncated) return {};
    return failed(e.code, e.tag, e.offset);
  }
  if (auto e = leading(begin.next, 9, length)) {
    if (e.code == ErrorCode::Truncated) return {};
    return failed(e.code, e.tag, e.offset);
  }
  uint128_t bodyLength;
  bool negative;
  if (!integer<32, false>({input.data() + length.offset, length.length},
      bodyLength, negative)) return failed(ErrorCode::Length, 9, length.offset);
  unsigned start = length.next;
  // Seven octets are mandatory for 10=ddd<SOH>. Subtract before adding so
  // hostile lengths cannot wrap either the checksum offset or frame size.
  if (start > limits.frameBytes || limits.frameBytes - start < 7 ||
      bodyLength > limits.frameBytes - start - 7)
    return failed(ErrorCode::Size, 9, length.offset);
  unsigned end = start + unsigned(bodyLength), total = end + 7;
  FrameResult pending;
  pending.expected = total;
  unsigned available = bounded.length() < end ? bounded.length() : end;
  auto e = scan({input.data(), available}, start, type);
  if (e) {
    if (e.code == ErrorCode::Truncated && type.offset > start &&
	available - type.offset > limits.fieldBytes)
      return failed(ErrorCode::Size, 35, type.offset);
    if (e.code == ErrorCode::Truncated && available < end) return pending;
    return failed(e.code == ErrorCode::Truncated ? ErrorCode::Length : e.code,
      35, start);
  }
  if (type.tag != 35) return failed(ErrorCode::Envelope, type.tag, start);
  if (!type.length || !text({input.data() + type.offset, type.length}))
    return failed(ErrorCode::Value, 35, type.offset);
  if (type.length > limits.fieldBytes) return failed(ErrorCode::Size, 35, type.offset);
  if (input.length() < total) return pending;
  if (input[end - 1] != '\1') return failed(ErrorCode::Envelope, 10, end - 1);
  const char *trailer = input.data() + end;
  if (memcmp(trailer, "10=", 3) || trailer[6] != '\1')
    return failed(ErrorCode::Envelope, 10, end);
  unsigned expected = 0;
  for (unsigned i = 3; i < 6; ++i) {
    char c = trailer[i];
    if (c < '0' || c > '9') return failed(ErrorCode::Checksum, 10, end + i);
    expected = expected * 10 + unsigned(c - '0');
  }
  unsigned sum = 0;
  for (unsigned i = 0; i < end; ++i) sum += uint8_t(input[i]);
  if ((sum & 255) != expected) return failed(ErrorCode::Checksum, 10, end);
  FrameResult r;
  r.state = FrameState::Complete;
  r.consumed = total;
  r.expected = total;
  r.view = {{input.data(), total}, {input.data() + begin.offset, begin.length},
    {input.data() + type.offset, type.length},
    {input.data() + type.next, end - type.next}, {expected}};
  return r;
}

FrameResult Framer::result(FrameResult r)
{
  if (r.state == FrameState::Error) m_error = r.error;
  else if (r.state == FrameState::Complete) m_complete = true;
  if (r.expected) m_expected = r.expected;
  return r;
}

FrameResult Framer::pending() const
{
  FrameResult r;
  r.expected = m_expected;
  return r;
}

void Framer::reset()
{
  m_error = {};
  m_seen = m_search = m_valueOffset = m_fields = m_expected = 0;
  m_complete = false;
}

FrameResult Framer::scan(ZuCSpan input)
{
  if (m_error) return failed(m_error.code, m_error.tag, m_error.offset);
  if (m_complete || input.length() < m_seen)
    return result(failed(ErrorCode::Envelope, 0, 0));
  m_seen = input.length();
  if (m_expected && (input.length() >= m_expected ||
      (m_fields < 3 && input.length() >= m_expected - 7)))
    return result(frame(input, m_limits));
  if (m_fields == 3) return pending();
  // Reuse the sole envelope parser for validation. Calling it only at field
  // boundaries/prefix bytes avoids rescanning a long partial value or body.
  if (!m_valueOffset) {
    auto r = frame(input, m_limits);
    if (r.state != FrameState::Incomplete) return result(r);
    if (r.expected) m_expected = r.expected;
  }
  unsigned bound = input.length() < m_limits.frameBytes ? input.length() : m_limits.frameBytes;
  while (m_search < bound && m_fields < 3) {
    char c = input[m_search++];
    if (!m_valueOffset && c == '=') m_valueOffset = m_search;
    if (c == '\1') {
      ++m_fields;
      m_valueOffset = 0;
      auto r = frame(input, m_limits);
      if (r.state != FrameState::Incomplete) return result(r);
      if (r.expected) m_expected = r.expected;
    } else if (m_valueOffset && m_search - m_valueOffset > m_limits.fieldBytes) {
      unsigned tag;
      switch (m_fields) {
	case 0: tag = 8; break;
	case 1: tag = 9; break;
	default: tag = 35; break;
      }
      return result(failed(ErrorCode::Size, tag, m_valueOffset));
    }
  }
  if (m_fields < 3 && bound == m_limits.frameBytes)
    return result(frame(input, m_limits));
  return pending();
}

FrameWriter::Layout FrameWriter::layout(unsigned capacity, unsigned beginLength, Limits cfg)
{
  if (capacity > cfg.frameBytes) capacity = cfg.frameBytes;
  // 8=<begin><SOH>9=<digits><SOH> ... 10=ddd<SOH>: fixed overhead is begin+13.
  if (beginLength > capacity || capacity - beginLength < 15) return {};
  unsigned available = capacity - beginLength - 13;
  unsigned width = Zu_ntoa::Log10<sizeof(unsigned)>::log(available);
  unsigned max = available - width;
  width = Zu_ntoa::Log10<sizeof(unsigned)>::log(max);
  max = available - width;
  // At a digit transition an extra octet may remain unusable: 99+2 fits 102
  // available octets, whereas 100+3 does not. Keep the largest feasible body.
  if (Zu_ntoa::Log10<sizeof(unsigned)>::log(max) > width) {
    max = ZuDecimalFn::pow10_32(width) - 1;
  }
  return {beginLength + 6 + width, max};
}

FrameWriter::FrameWriter(ZuSpan<char> storage, ZuCSpan beginString, Limits cfg) :
  FrameWriter{storage, beginString, cfg, layout(storage.length(), beginString.length(), cfg)}
{
}

FrameWriter::FrameWriter(ZuSpan<char> storage, ZuCSpan beginString, Limits cfg, Layout l) :
  m_storage{storage}, m_beginString{beginString}, m_limits{cfg}, m_prefix{l.prefix},
  m_body{{storage.data() && l.prefix <= storage.length() ?
    storage.data() + l.prefix : nullptr, l.body}, cfg}
{
  if (!l.prefix) m_error = {ErrorCode::Size};
  else if (!beginString.length()) m_error = {ErrorCode::Required, 8};
  else if (beginString.length() > cfg.fieldBytes) m_error = {ErrorCode::Size, 8};
  else if (memchr(beginString.data(), '\1', beginString.length()))
    m_error = {ErrorCode::Value, 8};
  if (m_error) m_body.fail(m_error.code);
}

FrameWriter::FrameWriter(ZuSpan<char> storage, ZuCSpan beginString,
    Grow grow, void *owner, Limits cfg) :
  FrameWriter{storage, beginString, cfg, layout(cfg.frameBytes, beginString.length(), cfg)}
{
  m_grow = grow; m_owner = owner;
  // The maximum envelope prefix is reserved once. Growing output retains its
  // body location; finish can return a subspan without compacting its bytes.
  unsigned available = storage.length() > m_prefix + 7 ? storage.length() - m_prefix - 7 : 0;
  unsigned max = m_prefix && cfg.frameBytes >= m_prefix + 7 ? cfg.frameBytes - m_prefix - 7 : 0;
  if (available > max) available = max;
  m_body = Sink{{storage.data() && m_prefix <= storage.length() ?
    storage.data() + m_prefix : nullptr, available},
    cfg, growBody, this};
  if (m_error) m_body.fail(m_error);
}
ZuSpan<char> FrameWriter::growBody(void *self_, unsigned used, unsigned needed)
{
  auto self = static_cast<FrameWriter *>(self_);
  if (!self->m_grow || !self->m_prefix ||
      self->m_limits.frameBytes < self->m_prefix + 7 ||
      needed > self->m_limits.frameBytes - self->m_prefix - 7) return {};
  auto storage = self->m_grow(self->m_owner, self->m_prefix + used,
    self->m_prefix + needed + 7);
  if (storage.length() <= self->m_prefix + 7) return {};
  self->m_storage = storage;
  unsigned n = storage.length() - self->m_prefix - 7;
  if (n > self->m_limits.frameBytes - self->m_prefix - 7)
    n = self->m_limits.frameBytes - self->m_prefix - 7;
  return {storage.data() + self->m_prefix, n};
}

FrameOutput FrameWriter::finish()
{
  if (m_finished) return {{}, {ErrorCode::Output}};
  m_finished = true;
  if (m_error) return {{}, m_error};
  if (!m_body.flush()) return {{}, m_body.error()};
  Token type;
  if (auto e = scan(m_body.data(), 0, type)) return {{}, e};
  if (type.tag != 35) return {{}, {ErrorCode::Envelope, type.tag}};
  if (!type.length) return {{}, {ErrorCode::Value, 35, type.offset}};
  unsigned bodyLength = m_body.length();
  if (m_body.data()[bodyLength - 1] != '\1')
    return {{}, {ErrorCode::Envelope, 10, bodyLength - 1}};
  unsigned prefix = m_beginString.length() + 6 +
    Zu_ntoa::Log10<sizeof(unsigned)>::log(bodyLength);

  unsigned start = m_prefix - prefix;
  auto output = m_storage.data() + start;
  Sink envelope{{output, prefix}, m_limits};
  envelope.field(8); envelope << m_beginString; envelope.end();
  envelope.field(9); envelope << ZuBox<unsigned>{bodyLength}; envelope.end();
  if (!envelope.flush()) return {{}, envelope.error()};
  unsigned checksum = 0, end = prefix + bodyLength;
  for (unsigned i = 0; i < end; ++i) checksum += uint8_t(output[i]);
  checksum &= 255;
  Sink trailer{{output + end, 7}, m_limits};
  trailer.field(10);
  trailer << char('0' + checksum/100) << char('0' + checksum/10%10) << char('0' + checksum%10);
  trailer.end();
  if (!trailer.flush()) return {{}, trailer.error()};
  return {{output, end + 7}, {}};
}

Error loadHeader_(ZuCSpan input, const Positions &positions, Header &h, Error parsed)
{
  Error error;
  ObjectView<Header, ZuFacet::FIX> view{input, positions, 0, error};
  if (!view.load(h)) {
    // A required-field failure precedes assignment in the ordinary loader.
    // Recover each independently valid header value for protocol diagnostics;
    // this does not turn a failed message into a successful decode.
    ZuUnroll::all<typename Schema<Header, ZuFacet::FIX>::Fields>(
      [input, &positions, &h]<typename F>() {
	Error fieldError;
	ObjectView<Header, ZuFacet::FIX> field{input, positions, 0, fieldError};
	auto value = field.template get<F>();
	if (!fieldError) F::set(h, ZuMv(value));
      });
  }
  if (parsed) error = parsed;
  if (error.code == ErrorCode::Duplicate && error.tag == 34) h.seqNo = 0;
  if (error) return error;
  auto bad = [&positions]<typename F>() {
    constexpr unsigned i = Schema<Header, ZuFacet::FIX>::template offset<F>();
    return Error{ErrorCode::Value, tag<F>(), positions[i].offset};
  };
  if (!h.sender.length()) return bad.template operator()<ZfField(Header, sender)>();
  if (!h.target.length()) return bad.template operator()<ZfField(Header, target)>();
  if (!h.seqNo) return bad.template operator()<ZfField(Header, seqNo)>();
  if (h.possDup && !*h.origSendingTime) return {ErrorCode::Required, 122};
  return {};
}

Error readHeader(const FrameView &frame, Header &header, Limits limits)
{
  return readHeader_(frame, header, nullptr, limits);
}
Error readHeader_(const FrameView &frame, Header &header,
    bool (*bodyContains)(unsigned), Limits limits)
{
  using S = Schema<Header, ZuFacet::FIX>;
  header = Header{};
  header.beginString = frame.beginString;
  header.msgType = frame.msgType;
  if (frame.wire.length() > limits.frameBytes ||
      S::Leaves::N > limits.parseBytes/sizeof(Position)) return {ErrorCode::Size};
  ZtBuiltin<Positions, S::Leaves::N> positions;
  positions.length(S::Leaves::N, false);
  for (unsigned i = 0; i < S::Leaves::N; ++i) positions[i].offset = UINT_MAX;
  Error error;
  Parse p{frame.content, limits, positions, error};
  unsigned offset = 0;
  Scope body{nullptr, bodyContains, 0, 0};
  Scope scope{bodyContains ? &body : nullptr, scopeContains<Header, ZuFacet::FIX>, 0, 0};
  parseScope<Header, ZuFacet::FIX>(p, 0, offset, scope, false);
  error = loadHeader_(frame.content, positions, header, error);
  if (error && error.code != ErrorCode::Required)
    error.offset += unsigned(frame.content.data() - frame.wire.data());
  return error;
}

Error writeHeader_(Sink &out, const Header &h)
{
  if (out.failed()) return out.error();
  if (!h.msgType.length()) return {ErrorCode::Required, 35};
  if (!h.sender.length()) return {ErrorCode::Required, 49};
  if (!h.target.length()) return {ErrorCode::Required, 56};
  if (!h.seqNo) return {ErrorCode::Value, 34};
  if (!*h.sendingTime) return {ErrorCode::Required, 52};
  if (h.possDup && !*h.origSendingTime) return {ErrorCode::Required, 122};
  if (h.possDup && h.origSendingTime > h.sendingTime) return {ErrorCode::Value, 122};
  out.field(35); out << h.msgType; out.end();
  out.field(49); out << h.sender; out.end();
  out.field(56); out << h.target; out.end();
  auto optional = [&out](unsigned tag, ZuCSpan value) {
    if (value.data()) { out.field(tag); out << value; out.end(); }
  };
  optional(50, h.senderSub);
  optional(57, h.targetSub);
  optional(142, h.senderLocation);
  optional(143, h.targetLocation);
  optional(1128, h.appVerID);
  out.field(34); out << ZuBox<uint64_t>{h.seqNo}; out.end();
  if (h.encoding.data()) { out.field(347); out << h.encoding; out.end(); }
  if (h.secureData.data()) {
    out.field(90); out << ZuBox<unsigned>{unsigned(h.secureData.length())}; out.end();
    out.field(91, true); out << h.secureData; out.end();
  }
  if (h.possDup) { out.field(43); out << 'Y'; out.end(); }
  return out.error();
}

Error writeHeader(Sink &out, const Header &h)
{
  return writeHeader<ZuFacet::FIX>(out, h);
}

Error writeTrailer(Sink &out, const Trailer &trailer)
{
  if (trailer.signature.data()) {
    out.field(93); out << ZuBox<unsigned>{unsigned(trailer.signature.length())}; out.end();
    out.field(89, true); out << trailer.signature; out.end();
  }
  return out.error();
}
} // Zfix::Fmt

