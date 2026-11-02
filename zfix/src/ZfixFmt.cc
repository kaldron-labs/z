//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZfixFmt.hh>

namespace Zfix::Fmt {

Error prefix(ZuCSpan input, unsigned offset, Token &out)
{
  unsigned n = input.length(), start = offset;
  if (offset >= n) return {ErrorCode::Truncated, 0, offset};
  int32_t tag = 0;
  unsigned used = Zu_nscan_<ZuFmt::Default>::atoi_(tag, input.data() + offset, n - offset);
  if (!used) return {ErrorCode::Tag, 0, start};
  offset += used;
  if (offset == n) return {ErrorCode::Truncated, tag, start};
  if (input[offset] != '=') return {ErrorCode::Tag, tag, start};
  out.tag = tag; out.offset = offset + 1; return {};
}

// Returns bytes consumed from the initial value through the final SOH.
// Text/count syntax: Value; overflow/configured bounds: Size; advertised
// boundary/adjacency: Length; missing syntax: Truncated.
unsigned consume(ZuCSpan input, Token &token, Limits limits, Error &error, int32_t payload)
{
  unsigned start = token.offset, n = input.length();
  auto fail = [&error](unsigned code, int32_t tag, unsigned offset) {
    if (!error) error = {code, tag, offset};
    return 0U;
  };
  auto ptr = static_cast<const char *>(memchr(input.data() + start, '\1', n - start));
  if (!ptr) return fail(ErrorCode::Truncated, token.tag, n);
  unsigned end = ptr - input.data();
  token.length = end - start;
  token.next = end + 1;
  if (token.length > limits.fieldBytes) return fail(ErrorCode::Size, token.tag, start);
  if (!payload) return token.next - start;
  if (!token.length) return fail(ErrorCode::Value, token.tag, start);
  unsigned count = 0;
  for (unsigned i = start; i < end; ++i) {
    unsigned digit = unsigned(input[i] - '0');
    if (digit > 9) return fail(ErrorCode::Value, token.tag, start);
    if (count > (UINT_MAX - digit)/10) return fail(ErrorCode::Size, token.tag, start);
    count = count * 10 + digit;
  }
  if (count > limits.fieldBytes) return fail(ErrorCode::Size, token.tag, start);
  Token value;
  if (auto e = prefix(input, token.next, value)) {
    return fail(e.code == ErrorCode::Tag ? ErrorCode::Truncated : e.code,
      payload, e.offset);
  }
  if (value.tag != payload) return fail(ErrorCode::Length, payload, token.next);
  if (count >= n - value.offset) return fail(ErrorCode::Length, payload, value.offset);
  if (input[value.offset + count] != '\1')
    return fail(ErrorCode::Length, payload, value.offset + count);
  token.countLength = token.length;
  token.valueTag = payload;
  token.offset = value.offset;
  token.length = count;
  token.next = value.offset + count + 1;
  return token.next - start;
}

ZfixFmtExtern Error scan(ZuCSpan input, unsigned offset, Token &out)
{
  if (auto e = prefix(input, offset, out)) return e;
  unsigned n = input.length();
  auto ptr = static_cast<const char *>(memchr(input.data() + out.offset, '\1', n - out.offset));
  if (!ptr) return {ErrorCode::Truncated, out.tag, n};
  unsigned end = ptr - input.data();
  out.length = end - out.offset;
  out.next = end + 1;
  return {};
}

Error inspect(ZuCSpan input, int32_t tag, FieldPos &out, Limits limits, unsigned offset)
{
  out = {UINT_MAX, 0};
  if (input.length() > limits.frameBytes) return {ErrorCode::Size};
  unsigned n = input.length();
  while (offset < n) {
    Token token;
    if (auto error = scan(input, offset, token)) return error;
    if (token.length > limits.fieldBytes)
      return {ErrorCode::Size, token.tag, token.offset};
    if (token.tag == tag) {
      if (out.offset != UINT_MAX) return {ErrorCode::Duplicate, tag, token.offset};
      out = {token.offset, token.length};
    }
    offset = token.next;
  }
  return {};
}

char *cstring(ZuCSpan value, Error &error)
{
  auto ptr = static_cast<char *>(::malloc(size_t(value.length()) + 1));
  if (!ptr) { error.code = ErrorCode::Size; return nullptr; }
  if (value.length()) memcpy(ptr, value.data(), value.length());
  ptr[value.length()] = 0;
  return ptr;
}

Sink::Sink(ZuSpan<char> storage, Limits limits, Grow grow, void *owner) :
  m_storage{storage}, m_limits{limits}, m_grow{grow}, m_owner{owner} { }

void Sink::fail(unsigned code)
{
  if (!failed()) m_error = {code, m_tag, m_used};
}

void Sink::field(int32_t tag)
{
  if (failed()) return;
  m_value = false;
  m_tag = tag;
  *this << ZuBox<int32_t>{tag} << '=';
  m_start = m_used;
  m_value = true;
}

void Sink::end()
{
  m_value = false;
  *this << '\1';
}

bool Sink::check(ZuCSpan value)
{
  if (m_value && value.length() > m_limits.fieldBytes - (m_used - m_start)) {
    fail(ErrorCode::Size); return false;
  }
  if (value.length() > available() && m_grow &&
      value.length() <= m_limits.frameBytes - m_used) {
    auto grown = m_grow(m_owner, m_used, m_used + value.length());
    if (grown.length() >= m_used) m_storage = grown;
  }
  if (value.length() > available()) {
    fail(ErrorCode::Size); return false;
  }
  return true;
}

void Sink::put(ZuCSpan value)
{
  if (failed() || !check(value)) return;
  if (value.length()) memcpy(m_storage.data() + m_used, value.data(), value.length());
  m_used += value.length();
}

unsigned Parse::slots(unsigned count)
{
  unsigned base = positions.length();
  unsigned max = limits.parseBytes/sizeof(Position);
  if (base > max || count > max - base) {
    fail(ErrorCode::Size, 0, 0); return UINT_MAX;
  }
  positions.length(base + count, false);
  for (unsigned i = base, n = base + count; i < n; ++i)
    positions[i].offset = UINT_MAX;
  return base;
}

bool Sink::group(uint64_t count)
{
  if (failed()) return false;
  if (m_depth >= m_limits.groupDepth) { fail(ErrorCode::Depth); return false; }
  if (count > m_limits.groupEntries - m_entries) { fail(ErrorCode::Size); return false; }
  m_entries += count;
  ++m_depth;
  return true;
}

void Sink::entry(unsigned offset, int32_t &delimiter)
{
  if (failed()) return;
  if (offset == m_used) { fail(ErrorCode::Group); return; }
  int32_t tag = 0;
  Zu_nscan<>::atoi(tag, m_storage.data() + offset, m_used - offset);
  if (delimiter && delimiter != tag) { fail(ErrorCode::Group); return; }
  delimiter = tag;
}

void Sink::endGroup()
{
  --m_depth;
}
} // Zfix::Fmt

