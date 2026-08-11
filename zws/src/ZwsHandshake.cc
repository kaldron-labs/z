//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// RFC 6455 opening-handshake utilities

#include <zlib/ZwsHandshake.hh>

#include <zlib/ZuBase64.hh>
#include <zlib/ZuICmp.hh>

#include <zlib/ZtlsMD.hh>
#include <zlib/ZtlsRandom.hh>

namespace Zws {

static const ZuBSpan GUID =
  "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

static bool protocolToken(ZuBSpan value)
{
  if (!value) return false;
  const unsigned n = value.length();
  const uint8_t *data = value.data();
  for (unsigned i = 0; i < n; ++i) {
    const unsigned c = uint8_t(data[i]);
    if (c >= 0x21 && c <= 0x7e &&
	c != '"' && c != '(' && c != ')' && c != ',' && c != '/' &&
	c != ':' && c != ';' && c != '<' && c != '=' && c != '>' &&
	c != '?' && c != '@' && c != '[' && c != '\\' && c != ']' &&
	c != '{' && c != '}')
      continue;
    return false;
  }
  return true;
}

bool nonce(Ztls::Random &random, HandshakeString &out)
{
  uint8_t raw[16];
  if (!random.random(raw)) return false;
  out.length(ZuBase64::enclen(sizeof(raw)));
  out.length(ZuBase64::encode(out.span(), raw));
  return true;
}

bool accept(HandshakeString &out, ZuBSpan key)
{
  if (!key) return false;
  uint8_t digest[Ztls::MD<Ztls::SHA1>::Size];
  Ztls::MD<Ztls::SHA1> md;
  md.update(key);
  md.update(GUID);
  md.finish(digest);
  out.length(ZuBase64::enclen(sizeof(digest)));
  out.length(ZuBase64::encode(out.span(), digest));
  return true;
}

bool validKey(ZuBSpan key)
{
  if (key.length() != ZuBase64::enclen(16)) return false;
  uint8_t raw[16];
  return ZuBase64::decode(raw, key) == sizeof(raw);
}

bool validAccept(ZuBSpan value, ZuBSpan key)
{
  HandshakeString expected;
  return accept(expected, key) && value == expected;
}

bool token(ZuBSpan value, ZuBSpan expected)
{
  unsigned valueLen = value.length();
  unsigned expectedLen = expected.length();
  unsigned offset = 0;
  while (offset < valueLen) {
    while (offset < valueLen &&
	(value[offset] == ' ' || value[offset] == '\t' ||
	 value[offset] == ','))
      ++offset;
    unsigned end = offset;
    while (end < valueLen && value[end] != ',') ++end;
    unsigned trimmed = end;
    while (trimmed > offset &&
	(value[trimmed - 1] == ' ' || value[trimmed - 1] == '\t'))
      --trimmed;
    if (trimmed - offset == expectedLen &&
	ZuICmp<ZuBSpan>::equals(
	  {value.data() + offset, expectedLen}, expected))
      return true;
    offset = end + (end < valueLen);
  }
  return false;
}

bool subprotocol(ZuBSpan offered, ZuBSpan selected)
{
  if (!protocolToken(selected)) return false;
  const unsigned offeredLen = offered.length();
  if (!offeredLen) return false;
  unsigned offset = 0;
  bool found = false;
  while (offset < offeredLen) {
    while (offset < offeredLen &&
	(offered[offset] == ' ' || offered[offset] == '\t'))
      ++offset;
    unsigned end = offset;
    while (end < offeredLen && offered[end] != ',') ++end;
    unsigned trimmed = end;
    while (trimmed > offset &&
	(offered[trimmed - 1] == ' ' || offered[trimmed - 1] == '\t'))
      --trimmed;
    ZuBSpan token{offered.data() + offset, trimmed - offset};
    if (!protocolToken(token)) return false;
    found |= token == selected;
    if (end == offeredLen) return found;
    offset = end + 1;
    if (offset == offeredLen) return false;
  }
  return found;
}

} // namespace Zws
