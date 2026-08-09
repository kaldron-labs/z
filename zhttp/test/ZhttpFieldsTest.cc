//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z HTTP wire-neutral field semantics test

#include <zlib/ZuTestUtil.hh>

#include <zlib/Zhttp.hh>

using namespace ZuTestUtil;

namespace {

void testFieldSection()
{
  ZuTestScope(testFieldSection);
  ZuCHECK(Zhttp::FieldSection::lookup("Informational") ==
      Zhttp::FieldSection::Informational,
    "informational section lookup");
  ZuCHECK(Zhttp::FieldSection::name(Zhttp::FieldSection::Final) == "Final",
    "final section name");
  ZuCHECK(Zhttp::FieldSection::lookup("invalid") ==
      Zhttp::FieldSection::Invalid,
    "invalid section lookup");
}

struct H3Fields {
  template <typename Semantics, typename Header>
  static bool field(
    Semantics &semantics, ZuCSpan name, ZuCSpan value, Header &&header) {
    return semantics.field(name, value, ZuFwd<Header>(header));
  }
};

struct H2SyntheticFields {
  template <typename Semantics, typename Header>
  static bool field(
    Semantics &semantics, ZuCSpan name, ZuCSpan value, Header &&header) {
    return semantics.field(name, value, ZuFwd<Header>(header));
  }
};

template <typename Source>
bool validRequest(bool extended = false)
{
  Zhttp::Fields::Semantics<true> fields;
  fields.extendedConnect(extended);
  bool host = false;
  auto header = [&host](ZuBSpan name, ZuBSpan value) {
    if (name == "host" && value == "example.com") host = true;
  };
  bool ok =
    Source::field(fields, ":method",
      extended ? "CONNECT" : "GET", header) &&
    Source::field(fields, ":scheme", "https", header) &&
    Source::field(fields, ":authority", "example.com", header) &&
    Source::field(fields, ":path", "/", header);
  if (extended)
    ok = ok && Source::field(fields, ":protocol", "websocket", header);
  bool operation = false;
  auto section = fields.finish(
    [&operation, extended](
	Zhttp::Method::T method, const Zhttp::RequestTarget &target) {
      operation =
	method == (extended ? Zhttp::Method::CONNECT : Zhttp::Method::GET) &&
	target.path == "/" &&
	(!extended || target.protocol == "websocket");
    },
    [](unsigned) { }, header);
  return ok && host && operation && section == Zhttp::FieldSection::Final;
}

template <typename Source>
bool invalidCases()
{
  auto header = [](ZuBSpan, ZuBSpan) { };
  auto finish = [](auto &fields) {
    return fields.finish(
      [](Zhttp::Method::T, const Zhttp::RequestTarget &) { },
      [](unsigned) { }, [](ZuBSpan, ZuBSpan) { });
  };

  Zhttp::Fields::Semantics<true> pseudoAfterRegular;
  bool pseudoOrder =
    Source::field(pseudoAfterRegular, "accept", "*/*", header) &&
    !Source::field(pseudoAfterRegular, ":method", "GET", header);

  Zhttp::Fields::Semantics<true> duplicate;
  bool duplicatePseudo =
    Source::field(duplicate, ":method", "GET", header) &&
    !Source::field(duplicate, ":method", "GET", header);

  Zhttp::Fields::Semantics<true> forbidden;
  bool forbiddenField =
    !Source::field(forbidden, "connection", "close", header) &&
    !Source::field(forbidden, "te", "gzip", header) &&
    !Source::field(forbidden, "Upper", "value", header);

  Zhttp::Fields::Semantics<true> missing;
  bool missingPseudo =
    Source::field(missing, ":method", "GET", header) &&
    finish(missing) == Zhttp::FieldSection::Invalid;

  Zhttp::Fields::Semantics<true> connect;
  bool invalidConnect =
    Source::field(connect, ":method", "CONNECT", header) &&
    Source::field(connect, ":authority", "example.com:443", header) &&
    Source::field(connect, ":scheme", "https", header) &&
    Source::field(connect, ":path", "/", header) &&
    finish(connect) == Zhttp::FieldSection::Invalid;

  Zhttp::Fields::Semantics<true> disabledExtended;
  bool disabledConnect =
    Source::field(disabledExtended, ":method", "CONNECT", header) &&
    Source::field(disabledExtended, ":scheme", "https", header) &&
    Source::field(disabledExtended, ":authority", "example.com", header) &&
    Source::field(disabledExtended, ":path", "/", header) &&
    Source::field(disabledExtended, ":protocol", "opaque", header) &&
    finish(disabledExtended) == Zhttp::FieldSection::Invalid;

  Zhttp::Fields::Semantics<true> trailers;
  trailers.trailers(true);
  bool invalidTrailer =
    !Source::field(trailers, "content-length", "0", header);

  return pseudoOrder && duplicatePseudo && forbiddenField &&
    missingPseudo && invalidConnect && disabledConnect && invalidTrailer;
}

template <typename Source>
bool responses()
{
  auto header = [](ZuBSpan, ZuBSpan) { };
  Zhttp::Fields::Semantics<false> informational;
  bool ok = Source::field(informational, ":status", "103", header);
  unsigned status = 0;
  auto section = informational.finish(
    [](Zhttp::Method::T, const Zhttp::RequestTarget &) { },
    [&status](unsigned value) { status = value; }, header);
  if (!ok || status != 103 ||
      section != Zhttp::FieldSection::Informational)
    return false;

  Zhttp::Fields::Semantics<false> final;
  ok = Source::field(final, ":status", "200", header) &&
    Source::field(final, "content-length", "3", header);
  section = final.finish(
    [](Zhttp::Method::T, const Zhttp::RequestTarget &) { },
    [&status](unsigned value) { status = value; }, header);
  if (!ok || status != 200 || section != Zhttp::FieldSection::Final ||
      !final.bodyAllowed())
    return false;

  Zhttp::Fields::Semantics<false> head;
  head.requestMethod(Zhttp::Method::HEAD);
  ok = Source::field(head, ":status", "200", header);
  section = head.finish(
    [](Zhttp::Method::T, const Zhttp::RequestTarget &) { },
    [&status](unsigned value) { status = value; }, header);
  return ok && section == Zhttp::FieldSection::Final && !head.bodyAllowed();
}

template <typename Source>
void testSource(ZuCSpan name)
{
  ZuTestScope(testSource);
  (void)name;
  ZuCHECK(validRequest<Source>(), "ordinary request semantics");
  ZuCHECK(validRequest<Source>(true),
    "Extended CONNECT request semantics");
  ZuCHECK(invalidCases<Source>(), "invalid field semantics");
  ZuCHECK(responses<Source>(), "response semantics");
}

} // namespace

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testFieldSection);
  ZuTestCall((testSource<H3Fields>), "H3");
  ZuTestCall((testSource<H2SyntheticFields>), "H2 synthetic");
}
