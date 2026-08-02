//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// HTTP Alt-Svc parsing, printing, and cache tests

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuStream.hh>

#include <zlib/ZhttpAltSvc.hh>

using namespace ZuTestUtil;

namespace ZhttpAltSvcTest_ {

Zhttp::Origin origin(ZuBSpan host = "example.com")
{
  return Zhttp::Origin{Zhttp::OriginView{
    host, 443, Zhttp::Scheme::https, false}};
}

void cursor()
{
  ZuTestScope(cursor);
  auto key = origin();
  ZuBSpan field =
    "h2=\":443\", h3=\"Example.COM:8443\"; ma=60; persist=1";
  Zhttp::AltSvcCursor cursor{field, key.view(), 4};
  Zhttp::AltSvcValue value;
  ZuCHECK(cursor.next(value) && value.protocolID == "h2" &&
      value.hostOmitted && value.host == "example.com" && value.port == 443,
    "inherited authority");
  ZuCHECK(cursor.next(value) && value.protocolID == "h3" && value.h3 &&
      value.host == "Example.COM" && !value.normalized && value.port == 8443 &&
      value.maxAge == 60 && value.persist,
    "read-only explicit authority");
  ZuCHECK(!cursor.next(value) && cursor.error().ok(), "cursor end");

  Zhttp::URLString mutableField;
  mutableField << "h3=\"Example.COM:443\"";
  Zhttp::AltSvcCursor mutableCursor{
    ZuSpan<uint8_t>{reinterpret_cast<uint8_t *>(mutableField.data()),
	mutableField.length()}, key.view(), 1};
  ZuCHECK(mutableCursor.next(value) && value.normalized &&
      value.host == "example.com", "mutable host normalized in place");

  Zhttp::AltSvcCursor bounded{
    ZuBSpan{"h3=\":443\", h3-29=\":443\""}, key.view(), 1};
  while (bounded.next(value)) { }
  ZuCHECK(bounded.error().code == Zhttp::AltSvcParseCode::TooManyAlternatives,
    "alternative bound");
  Zhttp::AltSvcCursor clear{ZuBSpan{"clear"}, key.view(), 1};
  ZuCHECK(clear.clear() && !clear.next(value) && clear.error().ok(), "clear");
}

void printing()
{
  ZuTestScope(printing);
  auto key = origin();
  ZuBSpan field = "h3=\"[2001:db8::1]:443\"; ma=0; persist=1";
  Zhttp::AltSvcCursor cursor{field, key.view(), 1};
  Zhttp::AltSvcValue value;
  ZuCHECK(cursor.next(value), "parse printable value");
  Zhttp::URLString printed;
  printed << value;
  ZuCHECK(ZuBSpan{printed} == field, "direct value print");
  Zhttp::AltSvcCursor reparsed{ZuBSpan{printed}, key.view(), 1};
  Zhttp::AltSvcValue roundTrip;
  ZuCHECK(reparsed.next(roundTrip) && roundTrip.protocolID == value.protocolID &&
      roundTrip.host == value.host && roundTrip.port == value.port &&
      roundTrip.maxAge == value.maxAge && roundTrip.persist == value.persist,
    "semantic print/parse round trip");
  char buffer[256];
  ZuStream sink{ZuSpan<char>{buffer, sizeof(buffer)}};
  sink << value;
  ZuCSpan sinkValue{buffer, unsigned(sizeof(buffer) - sink.length())};
  ZuCHECK(!sink.overflow() && sinkValue == printed,
    "value streams directly to a bounded sink");
  Zhttp::AltSvcValue values[] = {value, roundTrip};
  Zhttp::URLString list;
  list << Zhttp::AltSvcValuesView{values};
  Zhttp::AltSvcCursor listCursor{ZuBSpan{list}, key.view(), 2};
  unsigned count = 0;
  while (listCursor.next(roundTrip)) ++count;
  ZuCHECK(listCursor.error().ok() && count == 2,
    "value-list print is reparsable");
  Zhttp::AltSvcCursor inherited{ZuBSpan{"h3=\":443\""}, key.view(), 1};
  ZuCHECK(inherited.next(roundTrip), "parse default max-age");
  Zhttp::URLString inheritedPrint;
  inheritedPrint << roundTrip;
  ZuCHECK(inheritedPrint == "h3=\":443\"; ma=86400",
    "default max-age is explicit in canonical output");
  Zhttp::URLString clear;
  clear << Zhttp::AltSvcClear{};
  ZuCHECK(clear == "clear", "clear print");
}

void cache()
{
  ZuTestScope(cache);
  auto a = origin("a.example");
  auto b = origin("b.example");
  Zhttp::AltSvcCache cache{1};
  ZuCHECK(cache.update(a, ZuBSpan{"h3=\":443\"; ma=10"}, 2, ZuTime{100}) &&
      cache.count() == 1, "insert bounded origin");
  unsigned values = 0;
  ZuCHECK(cache.get(a, [&values](const Zhttp::AltSvcValue &value) {
      values += value.h3 && value.host == "a.example";
    }, ZuTime{109}) && values == 1, "borrowed cache iteration");
  ZuCHECK(!cache.update(
      a, ZuBSpan{"h3=\":444\", bad"}, 2, ZuTime{100}) &&
      cache.count() == 1, "malformed update is transactional");
  ZuCHECK(!cache.update(
      b, ZuBSpan{"h3=\":443\"; ma=10"}, 2, ZuTime{100}) &&
      cache.count() == 1, "origin bound rejection");
  values = 0;
  ZuCHECK(!cache.get(a, [&values](const Zhttp::AltSvcValue &) {
      ++values;
    }, ZuTime{110}) && !values && !cache.count(), "expiry releases capacity");
  ZuCHECK(cache.update(b, ZuBSpan{"h3=\":443\""}, 2, ZuTime{110}) &&
      cache.update(b, ZuBSpan{"clear"}, 2, ZuTime{110}) && !cache.count(),
    "validated clear removes origin");
}

} // namespace ZhttpAltSvcTest_

int main(int argc, char **argv)
{
  using namespace ZhttpAltSvcTest_;
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(cursor);
  ZuTestCall(printing);
  ZuTestCall(cache);
  return 0;
}
