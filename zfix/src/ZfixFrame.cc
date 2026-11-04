//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZfixFrame.hh>
#include <zlib/Zu_ntoa.hh>

namespace Zfix::Fmt {


static FrameResult failed(unsigned code, int32_t tag, unsigned offset)
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
  auto leading = [bounded, limits](unsigned offset, int32_t tag, Token &out) {
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
  unsigned bodyLength = ZuBox<unsigned>{ZuCSpan{
    input.data() + length.offset, length.length}}.val();
  unsigned start = length.next;
  // Seven octets are mandatory for 10=ddd<SOH>. Subtract before adding so
  // hostile lengths cannot wrap either the checksum offset or frame size.
  if (start > limits.frameBytes || limits.frameBytes - start < 7 ||
      bodyLength > limits.frameBytes - start - 7)
    return failed(ErrorCode::Size, 9, length.offset);
  unsigned end = start + bodyLength, total = end + 7;
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
  if (!type.length)
    return failed(ErrorCode::Value, 35, type.offset);
  if (type.length > limits.fieldBytes) return failed(ErrorCode::Size, 35, type.offset);
  if (input.length() < total) return pending;
  if (input[end - 1] != '\1') return failed(ErrorCode::Envelope, 10, end - 1);
  const char *trailer = input.data() + end;
  if (memcmp(trailer, "10=", 3) || trailer[6] != '\1')
    return failed(ErrorCode::Envelope, 10, end);
  unsigned expected = 0;
  unsigned digits = Zu_nscan<>::atou(expected, trailer + 3, 3);
  if (digits != 3) return failed(ErrorCode::Checksum, 10, end + 3 + digits);
  unsigned sum = 0;
  for (unsigned i = 0; i < end; ++i) sum += uint8_t(input[i]);
  if ((sum & 255) != expected) return failed(ErrorCode::Checksum, 10, end);
  FrameResult r;
  r.state = FrameState::Complete;
  r.consumed = total;
  r.expected = total;
  r.view = {{input.data(), total}, {input.data() + begin.offset, begin.length},
    {input.data() + type.offset, type.length},
    {input.data() + type.next, end - type.next}};
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
      int32_t tag;
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

Error FrameWriter::finishBody_()
{
  if (m_finished) return {ErrorCode::Output};
  m_finished = true;
  if (m_error) return m_error;
  if (!m_body.flush()) return m_body.error();
  Token type;
  if (auto e = scan(m_body.data(), 0, type)) return e;
  if (type.tag != 35) return {ErrorCode::Envelope, type.tag};
  if (!type.length) return {ErrorCode::Value, 35, type.offset};
  return {};
}

FrameParts FrameWriter::envelope_(unsigned bodyLength, unsigned bodyChecksum)
{
  unsigned prefix = m_beginString.length() + 6 +
    Zu_ntoa::Log10<sizeof(unsigned)>::log(bodyLength);
  if (prefix > m_prefix || uint64_t(prefix) + bodyLength + 7 > m_limits.frameBytes)
    return {{}, {}, {ErrorCode::Size}};
  auto output = m_storage.data() + m_prefix - prefix;
  Sink envelope{{output, prefix}, m_limits};
  envelope.field(8); envelope << m_beginString; envelope.end();
  envelope.field(9); envelope << ZuBox<unsigned>{bodyLength}; envelope.end();
  if (!envelope.flush()) return {{}, {}, envelope.error()};
  unsigned checksum = bodyChecksum;
  for (unsigned i = 0; i < prefix; ++i) checksum += uint8_t(output[i]);
  checksum &= 255;
  auto end = m_storage.data() + m_prefix + m_body.length();
  Sink trailer{{end, 7}, m_limits};
  trailer.field(10);
  trailer << ZuBox<unsigned>{checksum}.fmt<ZuFmt::Right<3, '0'>>();
  trailer.end();
  if (!trailer.flush()) return {{}, {}, trailer.error()};
  return {{output, prefix}, {end, 7}, {}};
}

FrameOutput FrameWriter::finish()
{
  if (auto error = finishBody_()) return {{}, error};
  auto bytes = m_body.data();
  unsigned bodyLength = bytes.length();
  if (bytes[bodyLength - 1] != '\1')
    return {{}, {ErrorCode::Envelope, 10, bodyLength - 1}};
  unsigned checksum = 0;
  for (unsigned i = 0; i < bodyLength; ++i) checksum += uint8_t(bytes[i]);
  auto output = envelope_(bodyLength, checksum);
  if (!output) return {{}, output.error};
  return {{output.prefix.data(), output.prefix.length() + bodyLength + 7}, {}};
}

FrameParts FrameWriter::finishParts(unsigned bodyLength, unsigned bodyChecksum)
{
  if (auto error = finishBody_()) return {{}, {}, error};
  if (bodyLength < m_body.length()) return {{}, {}, {ErrorCode::Size}};
  return envelope_(bodyLength, bodyChecksum);
}

} // Zfix::Fmt
