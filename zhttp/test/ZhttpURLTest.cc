//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// HTTP URL, redirect, and Alt-Svc tests

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZhttpURL.hh>

using namespace ZuTestUtil;

namespace ZhttpURLTest_ {

void parseURL()
{
  ZuTestScope(parseURL);

  struct Test {
    ZuCSpan	input;
    ZuCSpan	host;
    ZuCSpan	target;
    uint16_t	port;
    bool	ok;
  };
  static const Test tests[] = {
    {"https://host?x", "host", "/?x", 443, true},
    {"http://Example.COM", "example.com", "/", 80, true},
    {"http://example.com:80?x#y", "example.com", "/?x", 80, true},
    {"https://[2001:db8::1]:8443/a#x",
      "2001:db8::1", "/a", 8443, true},
    {"https://[2001:db8::1]?x", "2001:db8::1", "/?x", 443, true},
    {"https://host/#x", "host", "/", 443, true},
    {"https://host:0/", {}, {}, 0, false},
    {"https://host:65536/", {}, {}, 0, false},
    {"https://host:/", {}, {}, 0, false},
    {"https://user@host/", {}, {}, 0, false},
    {"https://2001:db8::1/", {}, {}, 0, false},
    {"https://[2001:db8::1]x/", {}, {}, 0, false},
    {"https://[2001:db8::1/", {}, {}, 0, false},
    {"https://[not-ipv6]/", {}, {}, 0, false},
    {"https://ho[st]/", {}, {}, 0, false},
    {"https://host%2/", {}, {}, 0, false},
    {"https://host\\name/", {}, {}, 0, false},
    {"https://host name/", {}, {}, 0, false},
    {"ftp://host/", {}, {}, 0, false},
    {"https://host:bad/", {}, {}, 0, false},
    {"https:///path", {}, {}, 0, false}
  };
  for (const auto &test : tests) {
    Zhttp::URL url;
    auto e = Zhttp::URL::parse(url, test.input);
    ZuCHECK(e.ok() == test.ok, test.input);
    if (!test.ok) continue;
    ZuCHECK(url.host == test.host, test.input);
    ZuCHECK(url.target == test.target, test.input);
    ZuCHECK(url.port == test.port, test.input);
  }
}

void redirect()
{
  ZuTestScope(redirect);
  Zhttp::URL base;
  ZuCHECK(Zhttp::URL::parse(
    base, "http://a/b/c/d;p?q").ok(), "parse base");
  struct Test {
    ZuCSpan	ref;
    ZuCSpan	result;
  };
  static const Test tests[] = {
    {"g:h", "g:h"},
    {"g", "http://a/b/c/g"},
    {"./g", "http://a/b/c/g"},
    {"g/", "http://a/b/c/g/"},
    {"/g", "http://a/g"},
    {"//g", "http://g/"},
    {"?y", "http://a/b/c/d;p?y"},
    {"g?y", "http://a/b/c/g?y"},
    {"#s", "http://a/b/c/d;p?q"},
    {"g#s", "http://a/b/c/g"},
    {"g?y#s", "http://a/b/c/g?y"},
    {";x", "http://a/b/c/;x"},
    {"g;x", "http://a/b/c/g;x"},
    {"g;x?y#s", "http://a/b/c/g;x?y"},
    {"", "http://a/b/c/d;p?q"},
    {".", "http://a/b/c/"},
    {"./", "http://a/b/c/"},
    {"..", "http://a/b/"},
    {"../", "http://a/b/"},
    {"../g", "http://a/b/g"},
    {"../..", "http://a/"},
    {"../../", "http://a/"},
    {"../../g", "http://a/g"},
    {"../../../g", "http://a/g"},
    {"../../../../g", "http://a/g"},
    {"/./g", "http://a/g"},
    {"/../g", "http://a/g"},
    {"g.", "http://a/b/c/g."},
    {".g", "http://a/b/c/.g"},
    {"g..", "http://a/b/c/g.."},
    {"..g", "http://a/b/c/..g"},
    {"./../g", "http://a/b/g"},
    {"./g/.", "http://a/b/c/g/"},
    {"g/./h", "http://a/b/c/g/h"},
    {"g/../h", "http://a/b/c/h"},
    {"g;x=1/./y", "http://a/b/c/g;x=1/y"},
    {"g;x=1/../y", "http://a/b/c/y"},
    {"g?y/./x", "http://a/b/c/g?y/./x"},
    {"g?y/../x", "http://a/b/c/g?y/../x"},
    {"g#s/./x", "http://a/b/c/g"},
    {"g#s/../x", "http://a/b/c/g"}
  };
  for (const auto &test : tests) {
    Zhttp::URL url;
    auto e = Zhttp::URL::resolve(url, base, test.ref);
    if (test.ref == "g:h") {
      ZuCHECK(!e.ok(), test.ref);
      continue;
    }
    ZuCHECK(e.ok(), test.ref);
    ZuCHECK(url.str() == test.result, test.ref);
  }
}

void altSvc()
{
  ZuTestScope(altSvc);
  Zhttp::Origin origin{"https", "example.com", 443};
  Zhttp::AltSvc value;
  auto e = Zhttp::AltSvc::parse(
    value, "h2=\":443\", h3=\":8443\"; ma=60; persist=1", origin, 4);
  ZuCHECK(e.ok(), "valid alternatives");
  ZuCHECK(value.values.length() == 2, "alternative count");
  ZuCHECK(!value.values[0].h3 && value.values[1].h3,
    "exact ALPN classification");
  ZuCHECK(value.values[1].host == "example.com" &&
      value.values[1].port == 8443 &&
      value.values[1].maxAge == 60 && value.values[1].persist,
    "H3 alternative values");
  ZuCHECK(Zhttp::AltSvc::parse(
      value, "h3=\"[2001:db8::1]:443\"; ma=0; x=y; flag",
      origin, 4).ok() &&
      value.values.length() == 1 &&
      value.values[0].host == "2001:db8::1" &&
      value.values[0].ipv6Literal && !value.values[0].maxAge,
    "IPv6 authority and extension parameters");

  Zhttp::AltSvc clear;
  ZuCHECK(Zhttp::AltSvc::parse(clear, "clear", origin, 4).ok() &&
      clear.clear && !clear.values.length(), "clear");

  Zhttp::AltSvc malformed;
  malformed.clear = true;
  ZuCHECK(!Zhttp::AltSvc::parse(
      malformed, "h3=\":bad\"", origin, 4).ok() &&
      malformed.clear && !malformed.values.length(),
    "failure leaves output unchanged");

  ZuCHECK(!Zhttp::AltSvc::parse(
      malformed, "h3=\":443\", h3-29=\":443\"", origin, 1).ok(),
    "explicit alternative bound");
  ZuCHECK(!Zhttp::AltSvc::parse(
      malformed, "clear, h3=\":443\"", origin, 4).ok(),
    "clear cannot be combined");
  ZuCHECK(!Zhttp::AltSvc::parse(
      malformed, "h3=\":443\",", origin, 4).ok(),
    "trailing comma rejected");
  ZuCHECK(!Zhttp::AltSvc::parse(
      malformed, "h3=\"host:443", origin, 4).ok(),
    "unterminated authority rejected");
  ZuCHECK(Zhttp::AltSvc::parse(
      value, "xh3=\":443\", h3x=\":443\", h3-29=\":443\"", origin, 4).ok() &&
      !value.values[0].h3 && !value.values[1].h3 && value.values[2].h3,
    "no H3 substring matching");
}

void altSvcCache()
{
  ZuTestScope(altSvcCache);
  Zhttp::Origin a{"https", "a.example", 443};
  Zhttp::Origin b{"https", "b.example", 443};
  Zhttp::AltSvc parsed;
  ZuCHECK(Zhttp::AltSvc::parse(
    parsed, "h3=\":443\"; ma=10", a, 2).ok(), "parse cache value");

  Zhttp::AltSvcCache cache{1};
  ZuCHECK(cache.update(a, parsed, ZuTime{100}) && cache.count() == 1,
    "insert bounded origin");
  Zhttp::AltSvcValues values;
  ZuCHECK(cache.get(a, values, ZuTime{109}) &&
      values.length() == 1 && values[0].h3,
    "origin-scoped hit before expiry");
  ZuCHECK(!cache.get(b, values, ZuTime{109}) && !values.length(),
    "different origin misses");
  ZuCHECK(!cache.update(b, parsed, ZuTime{100}) && cache.count() == 1,
    "origin bound has deterministic rejection");
  ZuCHECK(!cache.get(a, values, ZuTime{110}) &&
      !values.length() && !cache.count(),
    "access removes expired entry");
  ZuCHECK(cache.update(b, parsed, ZuTime{110}) && cache.count() == 1,
    "expiry releases bounded capacity");

  Zhttp::AltSvc clear;
  clear.clear = true;
  ZuCHECK(cache.update(b, clear, ZuTime{110}) && !cache.count(),
    "clear removes origin");
}

} // namespace ZhttpURLTest_

int main(int argc, char **argv)
{
  using namespace ZhttpURLTest_;

  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(parseURL);
  ZuTestCall(redirect);
  ZuTestCall(altSvc);
  ZuTestCall(altSvcCache);
  return 0;
}
