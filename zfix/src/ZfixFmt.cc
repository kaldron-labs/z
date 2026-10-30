//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZfixFmt.hh>

namespace Zfix::Fmt {

static Error prefix(ZuCSpan input, unsigned offset, Token &out)
{
  unsigned n = input.length(), start = offset;
  if (offset >= n) return {ErrorCode::Truncated, 0, offset};
  auto first = input[offset];
  if (first < '1' || first > '9')
    return {ErrorCode::Tag, 0, offset};
  unsigned tag = 0;
  while (offset < n) {
    unsigned c = unsigned(input[offset]);
    if (c == '=') { out.tag = tag; out.offset = offset + 1; return {}; }
    if (c < '0' || c > '9' || tag > (UINT_MAX - (c - '0'))/10)
      return {ErrorCode::Tag, tag, start};
    tag = tag * 10 + c - '0';
    ++offset;
  }
  return {ErrorCode::Truncated, tag, start};
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

ZfixFmtExtern Error scanData(
    ZuCSpan input, unsigned offset, unsigned tag, unsigned length, Token &out)
{
  if (auto e = prefix(input, offset, out)) return e;
  if (out.tag != tag) return {ErrorCode::Pair, out.tag, offset};
  unsigned remaining = input.length() - out.offset;
  if (length >= remaining) return {ErrorCode::Truncated, tag, out.offset};
  if (input[out.offset + length] != '\1')
    return {ErrorCode::Pair, tag, out.offset + length};
  out.length = length;
  out.next = out.offset + length + 1;
  return {};
}

ZfixFmtExtern bool decimal(ZuCSpan value)
{
  unsigned i = 0, n = value.length(), digits = 0;
  if (!n) return false;
  if (value[0] == '-') ++i;
  auto scanDigits = [&value, &i, &digits, n]() {
    while (i < n) {
      auto c = value[i];
      if (c < '0' || c > '9') return;
      ++i; ++digits;
    }
  };
  scanDigits();
  if (i < n && value[i] == '.') {
    ++i;
    scanDigits();
  }
  return digits && i == n;
}

// Validate only lexical structure around the framework scanner. Calendar and
// time conversion remain ZuDateTime's responsibility.
ZfixFmtExtern bool timestamp(ZuCSpan value)
{
  unsigned n = value.length();
  if (n != 17 && (n < 19 || n > 27)) return false;
  if (value[8] != '-' || value[11] != ':' || value[14] != ':') return false;
  if (n > 17 && value[17] != '.') return false;
  for (unsigned i = 0; i < n; ++i) {
    switch (i) { case 8: case 11: case 14: case 17: continue; }
    unsigned c = unsigned(value[i]);
    if (c < '0' || c > '9') return false;
  }
  return true;
}

bool text(ZuCSpan value)
{
  for (unsigned i = 0, n = value.length(); i < n; ++i) {
    unsigned c = uint8_t(value[i]);
    if (c < 0x20 || (c >= 0x7f && c < 0xa0)) return false;
  }
  return true;
}

char *cstring(ZuCSpan value, Error &error)
{
  if (memchr(value.data(), 0, value.length())) {
    error.code = ErrorCode::Value; return nullptr;
  }
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

void Sink::field(unsigned tag, bool binary)
{
  if (failed()) return;
  m_value = false;
  m_tag = tag;
  *this << ZuBox<unsigned>{tag} << '=';
  m_start = m_used;
  m_value = true;
  m_binary = binary;
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
  if (m_value && !m_binary && !text(value)) {
    fail(ErrorCode::Value); return false;
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

void Sink::entry(unsigned offset, unsigned &delimiter)
{
  if (failed()) return;
  if (offset == m_used) { fail(ErrorCode::Group); return; }
  unsigned tag = 0;
  while (offset < m_used) {
    char c = m_storage[offset++];
    if (c == '=') break;
    tag = tag * 10 + unsigned(c - '0');
  }
  if (delimiter && delimiter != tag) { fail(ErrorCode::Group); return; }
  delimiter = tag;
}

void Sink::endGroup()
{
  --m_depth;
}
} // Zfix::Fmt

