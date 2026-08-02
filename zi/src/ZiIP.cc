//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// IP address

#include <zlib/ZiIP.hh>

#include <string.h>

#include <zlib/Zu_aton.hh>
#include <zlib/ZuUTF.hh>

#include <zlib/ZiResolver.hh>

ZtEnumImplNS(ZiIPType);

static bool pton4_(
  const char *ptr, const char *end, unsigned octet,
  uint32_t value, in_addr &addr)
{
  for (;;) {
    unsigned n;
    unsigned avail = unsigned(end - ptr);
    if (avail > 3) avail = 3;
    unsigned i = Zu_atou(n, ptr, avail);
    if (!i || (i > 1 && *ptr == '0') || n > 255)
      return false;
    ptr += i;
    value = (value << 8) | n;
    if (++octet == 4) break;
    if (ptr == end || *ptr++ != '.') return false;
  }
  if (ptr != end) return false;
  addr.s_addr = htonl(value);
  return true;
}

static bool pton4(ZuCSpan host, in_addr &addr)
{
  if (!host) return false;
  const unsigned hostLen = host.length();
  auto ptr = &host[0];
  return pton4_(ptr, ptr + hostLen, 0, 0, addr);
}

static bool pton6(ZuCSpan host, in6_addr &addr)
{
  if (!host) return false;
  const unsigned hostLen = host.length();
  auto ptr = &host[0];
  const auto end = ptr + hostLen;
  memset(&addr, 0, sizeof(addr));
  unsigned group = 0;
  int compressed = -1;

  if (*ptr == ':') {
    if (hostLen < 2 || ptr[1] != ':') return false;
    compressed = 0;
    ptr += 2;
    if (ptr == end) return true;
  }

  while (ptr < end) {
    if (group >= 8) return false;
    auto begin = ptr;
    unsigned value = 0;
    unsigned avail = unsigned(end - ptr);
    if (avail > 4) avail = 4;
    unsigned digits = Zu_nscan<
	ZuFmt::Left<4, '\0', ZuFmt::Hex<>>>::atou(
	value, ptr, avail);
    if (!digits) return false;
    if (digits > 1 && *ptr == '0' &&
	(ptr[1] == 'x' || ptr[1] == 'X'))
      return false;
    ptr += digits;

    if (ptr < end && *ptr == '.') {
      if (group > 6) return false;
      if (digits > 3 || (digits > 1 && *begin == '0')) return false;
      unsigned n = 0;
      for (unsigned shift = digits << 2; shift;) {
	shift -= 4;
	unsigned digit = (value >> shift) & 0xf;
	if (digit > 9) return false;
	n = n * 10 + digit;
      }
      if (n > 255) return false;
      in_addr v4;
      if (!pton4_(ptr + 1, end, 1, n, v4))
	return false;
      memcpy(&addr.s6_addr[group << 1], &v4.s_addr, sizeof(v4.s_addr));
      group += 2;
      ptr = end;
      break;
    }
    addr.s6_addr[group << 1] = uint8_t(value >> 8);
    addr.s6_addr[(group << 1) + 1] = uint8_t(value);
    ++group;
    if (ptr == end) break;
    if (*ptr++ != ':' || ptr == end) return false;
    if (*ptr != ':') continue;
    if (compressed >= 0) return false;
    compressed = group;
    if (++ptr == end) break;
  }

  if (compressed < 0) return group == 8;
  unsigned missing = 8 - group;
  if (!missing) return false;
  unsigned tail = group - unsigned(compressed);
  memmove(
    &addr.s6_addr[(unsigned(compressed) + missing) << 1],
    &addr.s6_addr[unsigned(compressed) << 1], tail << 1);
  memset(&addr.s6_addr[unsigned(compressed) << 1], 0, missing << 1);
  return true;
}

bool ZiIP::parse(ZiIP &out, ZuCSpan host)
{
  in_addr v4;
  if (pton4(host, v4)) {
    out = v4;
    return true;
  }
  in6_addr v6;
  if (pton6(host, v6)) {
    out = v6;
    return true;
  }
  return false;
}

int ZiIP::resolve_(Zi::Hostname host, ZeError *e)
{
#ifndef _WIN32
  ZuCSpan text{host};
#else
  Zi::Name name;
  name.length(ZuUTF<char, wchar_t>::cvt(name.span(), host));
  name.truncate();
  ZuCSpan text{name};
#endif
  if (parse(*this, text)) return Zi::OK;

  ZeError error;
  bool ok = ZmBlock<bool>{}([this, host = ZuMv(host), &error](auto wake) mutable {
    ZiResolver::resolve(ZuMv(host),
      ZiResolver_::ResolveFn{[this, &error, wake](auto result) mutable {
	if (result.template is<ZiResolver_::Event>()) {
	  error = result.template p<ZiResolver_::Event>();
	  wake(false);
	  return false;
	}
	if (result.template is<void>()) return false;
	auto ip = result.template p<ZiIP>();
	*this = ip;
	wake(true);
	return false;
      }});
  });
  if (ok) return Zi::OK;
  if (e) *e = error;
  return Zi::IOError;
}

ZiIP::Hostname ZiIP::name(ZeError *e)
{
  Hostname name;
  ZeError error;
  bool ok = ZmBlock<bool>{}([this, &name, &error](auto wake) {
    ZiResolver::name(*this,
      ZiResolver_::NameFn{[&name, &error, wake](auto result) mutable {
	if (result.template is<ZiResolver_::Event>()) {
	  error = result.template p<ZiResolver_::Event>();
	  wake(false);
	  return;
	}
	name = ZuMv(result).template p<Hostname>();
	wake(true);
      }});
  });
  if (ok) return name;
  if (e) *e = error;
  return {};
}
