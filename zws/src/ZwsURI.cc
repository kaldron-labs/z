//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ws:// and wss:// URI parsing

#include <zlib/ZwsURI.hh>

#include <zlib/ZuBox.hh>
#include <zlib/ZuMatcher.hh>

#include <zlib/ZiPlatform.hh>

namespace Zws {
namespace URI_ {

inline URIError error(int code, unsigned offset = 0)
{
  return {offset, int8_t(code)};
}

inline bool alpha(unsigned c)
{
  c |= 0x20;
  return c >= 'a' && c <= 'z';
}

inline bool digit(unsigned c) { return c >= '0' && c <= '9'; }
inline bool alnum(unsigned c) { return alpha(c) || digit(c); }

inline bool hex(unsigned c)
{
  c |= 0x20;
  return digit(c) || (c >= 'a' && c <= 'f');
}

inline bool regName(ZuCSpan in)
{
  const unsigned n = in.length();
  const char *data = in.data();
  for (unsigned i = 0; i < n; ++i) {
    const unsigned c = data[i];
    if (alnum(c) || c == '-' || c == '.' || c == '_' || c == '~' ||
	c == '!' || c == '$' || c == '&' || c == '\'' || c == '(' ||
	c == ')' || c == '*' || c == '+' || c == ',' || c == ';' ||
	c == '=')
      continue;
    if (c == '%' && i + 2 < n &&
	hex(data[i + 1]) && hex(data[i + 2])) {
      i += 2;
      continue;
    }
    return false;
  }
  return true;
}

inline void lower(URIString &out, ZuCSpan in)
{
  const unsigned n = in.length();
  const char *inData = in.data();
  out.length(n);
  char *outData = out.data();
  for (unsigned i = 0; i < n; ++i) {
    const unsigned c = inData[i];
    outData[i] = c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
  }
}

} // namespace URI_

URIString URI::authority() const
{
  URIString out;
  if (ipv6Literal) out << '[' << host << ']';
  else out << host;
  if (explicitPort) out << ':' << port;
  return out;
}

URIString URI::str() const
{
  URIString out;
  out << scheme << "://" << authority() << target;
  return out;
}

struct SchemeIDs { using Keys = ZuStringTL<"ws", "wss">; };
struct AuthEndIDs { using Keys = ZuStringTL<"/", "?", "@">; };

URIError URI::parse(URI &out, ZuCSpan input)
{
  URI parsed;
  const unsigned inputLen = input.length();
  const char *inputData = input.data();

  // Only ws and wss are accepted, so do not search the entire URI for a
  // scheme terminator.  Canonicalize the bounded token once for matching.
  unsigned schemeLen;
  if (inputLen >= 3 && inputData[2] == ':')
    schemeLen = 2;
  else if (inputLen >= 4 && inputData[3] == ':')
    schemeLen = 3;
  else
    return URI_::error(URICode::UnsupportedScheme);
  const ZuCSpan schemeSuffix{
    inputData + schemeLen, inputLen - schemeLen};
  if (!schemeSuffix.match("://"))
    return URI_::error(URICode::UnsupportedScheme);
  URI_::lower(parsed.scheme, {inputData, schemeLen});
  static constexpr auto schemes = ZuMatcher<SchemeIDs>();
  switch (schemes.exact(parsed.scheme)) {
  case 0:
    parsed.port = 80;
    break;
  case 1:
    parsed.port = 443;
    break;
  default:
    return URI_::error(URICode::UnsupportedScheme);
  }

  const int64_t invalid_ = input.find([](char c_) {
    const unsigned c = uint8_t(c_);
    return c <= 0x20 || c == 0x7f || c == '#';
  });
  if (invalid_ >= 0) {
    const unsigned invalid = unsigned(invalid_);
    return URI_::error(
      inputData[invalid] == '#' ?
	URICode::Fragment : URICode::InvalidCharacter,
      invalid);
  }

  const unsigned authStart = schemeLen + 3;
  const ZuCSpan remainder{
    inputData + authStart, inputLen - authStart};
  static constexpr auto authEndMatcher = ZuMatcher<AuthEndIDs>();
  const auto [authOffset, authToken] = authEndMatcher.find(remainder);
  if (authToken == 2)
    return URI_::error(URICode::UserInfo, authStart + authOffset);
  const unsigned authEnd =
    authOffset >= 0 ? authStart + authOffset : inputLen;
  if (authEnd == authStart) return URI_::error(URICode::MissingHost, authStart);

  const ZuCSpan auth{inputData + authStart, authEnd - authStart};
  ZuCSpan host, port;
  const unsigned authLen = auth.length();
  const char *authData = auth.data();
  if (auth[0] == '[') {
    const int64_t close_ =
      ZuCSpan{authData + 1, authLen - 1}.find(
	[](char c) { return c == ']'; });
    if (close_ < 1)
      return URI_::error(URICode::InvalidAuthority, authStart);
    const unsigned close = unsigned(close_) + 1;
    host = {authData + 1, close - 1};
    parsed.ipv6Literal = true;
    if (close + 1 < authLen) {
      if (auth[close + 1] != ':')
	return URI_::error(URICode::InvalidAuthority, authStart + close + 1);
      port = {authData + close + 2, authLen - close - 2};
      parsed.explicitPort = true;
    }
  } else {
    const int64_t portColon = auth.find([](char c) { return c == ':'; });
    if (portColon >= 0) {
      const unsigned portOffset = unsigned(portColon) + 1;
      const ZuCSpan port_{
	authData + portOffset, authLen - portOffset};
      const int64_t duplicate =
	port_.find([](char c) { return c == ':'; });
      if (duplicate >= 0)
	return URI_::error(
	  URICode::InvalidAuthority, authStart + portOffset + duplicate);
      host = {authData, unsigned(portColon)};
      port = {
	authData + portOffset,
	authLen - unsigned(portColon) - 1};
      parsed.explicitPort = true;
    } else
      host = auth;
  }
  if (!host) return URI_::error(URICode::MissingHost, authStart);
  if (!parsed.ipv6Literal && !URI_::regName(host))
    return URI_::error(URICode::InvalidAuthority, authStart);
  URI_::lower(parsed.host, host);
  if (parsed.ipv6Literal) {
    in6_addr addr;
#ifndef _WIN32
    if (::inet_pton(AF_INET6, parsed.host.ndata(), &addr) != 1)
#else
    if (::InetPtonA(AF_INET6, parsed.host.ndata(), &addr) != 1)
#endif
      return URI_::error(URICode::InvalidAuthority, authStart + 1);
  }

  if (parsed.explicitPort) {
    if (!port) return URI_::error(URICode::InvalidPort, authEnd);
    uint32_t value = 0;
    const unsigned portLen = port.length();
    const char *portData = port.data();
    for (unsigned i = 0; i < portLen; ++i) {
      const unsigned c = portData[i];
      if (c < '0' || c > '9')
	return URI_::error(
	  URICode::InvalidPort,
	  authStart + unsigned(portData - authData) + i);
      value = value * 10 + (c - '0');
      if (value > 65535)
	return URI_::error(URICode::InvalidPort, authStart);
    }
    if (!value) return URI_::error(URICode::InvalidPort, authStart);
    parsed.port = value;
  }

  if (authEnd == inputLen)
    parsed.target = "/";
  else if (inputData[authEnd] == '?')
    parsed.target << '/' <<
      ZuCSpan{inputData + authEnd, inputLen - authEnd};
  else
    parsed.target =
      ZuCSpan{inputData + authEnd, inputLen - authEnd};
  out = ZuMv(parsed);
  return {};
}

} // namespace Zws
