//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// HTTP URL, redirect, and request-target tests

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZhttpURL.hh>

using namespace ZuTestUtil;

namespace ZhttpURLTest_ {

void parseURL()
{
  ZuTestScopeRT(parseURL);

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
    Zhttp::URL storage;
    auto e = storage.assign(ZuBSpan{test.input});
    ZuCheckRT(e.ok() == test.ok, log_( test.input));
    if (!test.ok) continue;
    auto url = storage.url();
    ZuCheckRT(url.host == test.host, log_( test.input));
    Zhttp::URLString target;
    url.writeTarget(target);
    ZuCheckRT(target == test.target, log_( test.input));
    ZuCheckRT(url.port == test.port, log_( test.input));
  }

  Zhttp::URLString mutableURL;
  mutableURL << "HTTP://Example.COM/a?#";
  Zhttp::URLView url{mutableURL.span()};
  ZuCheckRT(url.ok() && url.host == "example.com", log_( "in-place host case fold"));
  ZuCheckRT(url.hasQuery && !url.query && url.hasFragment && !url.fragment, log_(
    "empty query and fragment presence"));
  Zhttp::URLString printed;
  printed << url;
  ZuCheckRT(printed == "http://example.com/a?#", log_( "direct URL print"));

  Zhttp::URL empty;
  ZuCheckRT(!empty.ok() && !empty.url().ok(), log_( "default storage is not a URL"));
  Zhttp::URLView nullURL;
  ZuCheckRT(!nullURL.ok(), log_( "default borrowed view is not a URL"));
  Zhttp::URL retained{ZuBSpan{"https://Example.COM:8443/p?"}};
  Zhttp::URL copied = retained;
  Zhttp::URL moved = ZuMv(copied);
  Zhttp::URLString movedText;
  movedText << moved.url();
  ZuCheckRT(moved.ok() && movedText == "https://example.com:8443/p?", log_(
    "owned URL offsets survive copy and move"));
  auto failed = moved.assign(ZuBSpan{"https://bad host/"});
  movedText.length(0);
  movedText << moved.url();
  ZuCheckRT(!failed.ok() && moved.ok() &&
      movedText == "https://example.com:8443/p?", log_(
    "failed assignment preserves retained URL"));
  Zhttp::URLString adoptedText{"HTTP://Adopted.EXAMPLE/a"};
  auto adopted = moved.adopt(ZuMv(adoptedText));
  movedText.length(0);
  movedText << moved.url();
  ZuCheckRT(adopted.ok() && movedText == "http://adopted.example/a", log_(
    "adopt transfers URL storage and normalizes in place"));
  Zhttp::URLString invalidAdopt{"https://bad host/"};
  failed = moved.adopt(ZuMv(invalidAdopt));
  movedText.length(0);
  movedText << moved.url();
  ZuCheckRT(!failed.ok() && moved.ok() &&
      movedText == "http://adopted.example/a", log_(
    "failed adopt preserves retained URL"));
  Zhttp::AuthorityView readOnly;
  ZuCheckRT(Zhttp::parseAuthority(
      readOnly, ZuBSpan{"Example.COM:443"}, 0, 0, true).ok() &&
      readOnly.host == "Example.COM" && !readOnly.normalized, log_(
    "read-only authority validation preserves source bytes"));
  ZuCheckRT(Zhttp::parseAuthority(
      readOnly, ZuBSpan{"127.0.0.1:0"}, 0, 0, false, true).ok() &&
      readOnly.explicitPort && !readOnly.port, log_(
    "socket authority policy permits an explicit ephemeral port"));
}

void redirect()
{
  ZuTestScopeRT(redirect);
  Zhttp::URL baseStorage{ZuBSpan{"http://a/b/c/d;p?q"}};
  ZuCheckRT(baseStorage.ok(), log_( "parse base"));
  auto base = baseStorage.url();
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
    {"//g", "http://g"},
    {"?y", "http://a/b/c/d;p?y"},
    {"g?y", "http://a/b/c/g?y"},
    {"#s", "http://a/b/c/d;p?q#s"},
    {"g#s", "http://a/b/c/g#s"},
    {"g?y#s", "http://a/b/c/g?y#s"},
    {";x", "http://a/b/c/;x"},
    {"g;x", "http://a/b/c/g;x"},
    {"g;x?y#s", "http://a/b/c/g;x?y#s"},
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
    {"//g/a/../b", "http://g/b"},
    {"http://g/a/../b?x#s", "http://g/b?x#s"},
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
    {"g#s/./x", "http://a/b/c/g#s/./x"},
    {"g#s/../x", "http://a/b/c/g#s/../x"}
  };
  for (const auto &test : tests) {
    Zhttp::URL storage;
    auto e = storage.resolve(base, test.ref);
    if (test.ref == "g:h") {
      ZuCheckRT(!e.ok(), log_( test.ref));
      continue;
    }
    ZuCheckRT(e.ok(), log_( test.ref));
    Zhttp::URLString result;
    result << storage.url();
    ZuCheckRT(result == test.result, log_( test.ref));
  }
}

void target()
{
  ZuTestScopeRT(target);

  struct Test {
    ZuCSpan input;
    Zhttp::Method::T method;
    Zhttp::TargetForm::T form;
    ZuCSpan path;
    bool ok;
  };
  static const Test tests[] = {
    {"/a?", Zhttp::Method::GET, Zhttp::TargetForm::Origin, "/a?", true},
    {"http://Example.COM/a?x", Zhttp::Method::GET,
      Zhttp::TargetForm::Absolute, "/a?x", true},
    {"example.com:443", Zhttp::Method::CONNECT,
      Zhttp::TargetForm::Authority, {}, true},
    {"*", Zhttp::Method::OPTIONS, Zhttp::TargetForm::Asterisk, "*", true},
    {"*", Zhttp::Method::GET, Zhttp::TargetForm::Asterisk, {}, false},
    {"/a#x", Zhttp::Method::GET, Zhttp::TargetForm::Origin, {}, false},
    {"http://example.com/a#x", Zhttp::Method::GET,
      Zhttp::TargetForm::Absolute, {}, false},
    {"/a%2", Zhttp::Method::GET, Zhttp::TargetForm::Origin, {}, false},
    {"http://example.com/a b", Zhttp::Method::GET,
      Zhttp::TargetForm::Absolute, {}, false},
    {"example.com", Zhttp::Method::CONNECT,
      Zhttp::TargetForm::Authority, {}, false}
  };
  for (const auto &test : tests) {
    Zhttp::URLString input{test.input};
    Zhttp::Target target;
    auto e = Zhttp::Target::parseH1(target, test.method, input.span());
    ZuCheckRT(e.ok() == test.ok, log_( test.input));
    if (test.ok) {
      ZuCheckRT(target.form == test.form, log_( test.input));
      ZuCheckRT(target.path == test.path, log_( test.input));
      if (target.path)
	ZuCheckRT(target.path.data() >=
	    input.data() &&
	    target.path.data() + target.path.length() <=
	    input.data() + input.length(), log_(
	  "path-query borrows the input target"));
      if (target.authority.host)
	ZuCheckRT(target.authority.normalized, log_( "target authority normalized"));
    }
  }

  Zhttp::URLString authority;
  authority << "Example.COM:443";
  Zhttp::URLString path{"/chat?"};
  Zhttp::URLString protocol{"websocket"};
  Zhttp::Target target;
  auto e = Zhttp::Target::fromPseudo(target, Zhttp::Method::CONNECT,
    Zhttp::Scheme::https, authority.span(), path.span(), protocol.span());
  ZuCheckRT(e.ok() && target.form == Zhttp::TargetForm::ExtendedConnect &&
      target.authority.host == "example.com" &&
      target.path == "/chat?", log_( "extended CONNECT"));
  path = "bad";
  e = Zhttp::Target::fromPseudo(target, Zhttp::Method::GET,
    Zhttp::Scheme::https, authority.span(), path.span(), {});
  ZuCheckRT(e.code == Zhttp::TargetParseCode::InvalidPath &&
      e.field == Zhttp::TargetField::Path && !e.offset, log_(
    "pseudo-header error identifies its source field"));
}

} // namespace ZhttpURLTest_

int main(int argc, char **argv)
{
  using namespace ZhttpURLTest_;

  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(parseURL);
  ZuTestCall(redirect);
  ZuTestCall(target);
  return 0;
}
