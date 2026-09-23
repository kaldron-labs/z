//  -*- mode:c++; indent-tabs-mode:t; tab-width=8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <zlib/ZuMatcher.hh>
#include <zlib/ZdbusAddress.hh>

namespace Zdbus_ {

struct AddrKeys {
  using Keys = ZuStringTL<"path", "abstract">;
  enum { Path, Abstract };
};

static int hex(uint8_t c)
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

static bool decode(Address::Text &out, ZuCSpan encoded, bool path)
{
  // sun_path reserves one byte for the terminator or abstract-name prefix.
  enum { NameLimit = sizeof(sockaddr_un{}.sun_path) - 1 };
  uint64_t n = encoded.length();
  unsigned cap = n < NameLimit ? unsigned(n) : NameLimit;
  out.length(cap);
  auto p = out.data();
  unsigned length = 0;
  for (uint64_t i = 0; i < n; ++i) {
    uint8_t c = encoded[i];
    if (c == '%') {
      if (n - i < 3) return false;
      int hi = hex(encoded[++i]);
      int lo = hex(encoded[++i]);
      if (hi < 0 || lo < 0) return false;
      c = uint8_t((hi << 4) | lo);
    }
    if (!c && path) return false;
    if (length == cap) return false;
    p[length++] = char(c);
  }
  out.length(length);
  return length;
}

bool Address::parse(Address &out, ZuCSpan input)
{
  static constexpr auto keyMatcher = ZuMatcher<AddrKeys>();
  out.kind = AddrKind::Invalid;
  out.value.length(0);
  unsigned n = input.length();
  for (unsigned begin = 0; begin < n;) {
    unsigned end = begin;
    while (end < n && input[end] != ';') ++end;
    ZuCSpan entry{input.begin() + begin, end - begin};
    begin = end + 1;
    if (entry.length() < 5 || ZuCSpan{entry.begin(), 5} != "unix:")
      continue;

    Address candidate;
    bool bad = false;
    for (unsigned pos = 5, length = entry.length(); pos < length;) {
      unsigned tokenEnd = pos;
      while (tokenEnd < length && entry[tokenEnd] != ',') ++tokenEnd;
      unsigned eq = pos;
      while (eq < tokenEnd && entry[eq] != '=') ++eq;
      if (eq == pos || eq == tokenEnd) { bad = true; break; }
      ZuCSpan key{entry.begin() + pos, eq - pos};
      int keyID = keyMatcher.exact(key);
      if (keyID >= 0) {
        if (candidate) { bad = true; break; }
        bool path = keyID == AddrKeys::Path;
        ZuCSpan encoded{entry.begin() + eq + 1, tokenEnd - eq - 1};
        if (!decode(candidate.value, encoded, path)) { bad = true; break; }
        candidate.kind = path ? AddrKind::Path : AddrKind::Abstract;
      }
      pos = tokenEnd + 1;
    }
    if (bad || !candidate) continue;
    sockaddr_un addr;
    socklen_t length;
    if (!candidate.socketAddr(addr, length)) continue;
    out = ZuMv(candidate);
    return true;
  }
  return false;
}

bool Address::session(Address &out)
{
  auto address = getenv("DBUS_SESSION_BUS_ADDRESS");
  return address && parse(out, address);
}

bool Address::system(Address &out)
{
  auto address = getenv("DBUS_SYSTEM_BUS_ADDRESS");
  if (address) return parse(out, address);
  return parse(out, "unix:path=/run/dbus/system_bus_socket");
}

bool Address::socketAddr(sockaddr_un &out, socklen_t &length) const
{
  if (!*this) return false;
  unsigned n = value.length();
  if (!n || n >= sizeof(out.sun_path)) return false;
  memset(&out, 0, sizeof(out));
  out.sun_family = AF_UNIX;
  switch (kind) {
    case AddrKind::Path:
      if (value[0] != '/' || memchr(value.data(), 0, n)) return false;
      memcpy(out.sun_path, value.data(), n);
      out.sun_path[n] = 0;
      break;
    case AddrKind::Abstract:
      out.sun_path[0] = 0;
      memcpy(out.sun_path + 1, value.data(), n);
      break;
    default:
      return false;
  }
  length = socklen_t(offsetof(sockaddr_un, sun_path) + n + 1);
  return true;
}

} // Zdbus_
