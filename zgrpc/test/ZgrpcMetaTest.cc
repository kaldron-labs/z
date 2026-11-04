//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <limits.h>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZgrpcMeta.hh>

using namespace ZuTestUtil;
using namespace Zgrpc;

static void timeouts()
{
  ZuTestScopeRT(timeouts);
  ZuCheckRT(encodeTimeout(-1) == "0n" && encodeTimeout(0) == "0n");
  ZuCheckRT(encodeTimeout(99999999) == "99999999n");
  ZuCheckRT(encodeTimeout(100000000) == "100000u");
  ZuCheckRT(encodeTimeout(100000001) == "100001u");
  ZuCheckRT(encodeTimeout(INT64_MAX) == "2562048H");
  for (auto input : {"0n", "1u", "1m", "1S", "1M", "1H", "99999999H"}) {
    int64_t nanos = -1, roundTrip = -1;
    ZuCheckRT(decodeTimeout(input, nanos) && nanos >= 0);
    ZuCheckRT(decodeTimeout(encodeTimeout(nanos), roundTrip) && roundTrip >= nanos);
  }
  int64_t nanos = -1;
  ZuCheckRT(decodeTimeout("99999999H", nanos) && nanos == INT64_MAX);
  for (auto input : {"", "n", "1", "1s", "-1n", "+1n", "100000000n", "1 n", "1.5S"}) {
    nanos = -1;
    ZuCheckRT(!decodeTimeout(input, nanos) && nanos == -1);
  }
}

static void status()
{
  ZuTestScopeRT(status);
  for (int i = 0; i < Code::N; i++) {
    String wire;
    wire << ZuBoxed(i);
    Code::T code = -1;
    ZuCheckRT(decodeCode(wire, code) && code == i);
    ZuCheckRT(Code::lookup(Code::name(i)) == i);
  }
  Code::T code = -1;
  for (auto input : {"", "-1", "+0", " 0", "17", "255", "0x0", "1.0"})
    ZuCheckRT(!decodeCode(input, code) && code == -1);
  ZuCheckRT(httpCode(400) == Code::INTERNAL);
  ZuCheckRT(httpCode(401) == Code::UNAUTHENTICATED);
  ZuCheckRT(httpCode(403) == Code::PERMISSION_DENIED);
  ZuCheckRT(httpCode(404) == Code::UNIMPLEMENTED);
  for (unsigned status : {429U, 502U, 503U, 504U})
    ZuCheckRT(httpCode(status) == Code::UNAVAILABLE);
  ZuCheckRT(httpCode(200) == Code::UNKNOWN && httpCode(500) == Code::UNKNOWN);
  ZuCheckRT(contentType("application/grpc") && contentType("application/grpc+proto"));
  ZuCheckRT(!contentType("application/grpc-web") && !contentType("application/json"));
}

static void text()
{
  ZuTestScope(text);
  ZuCheck(encodeText("a %+") == "a %25+");
  ZuCheck(decodeText("a %25+") == "a %+");
  ZuCheck(decodeText("%xx%2%") == "%xx%2%");
  Bytes bytes;
  bytes.length(256);
  for (unsigned i = 0; i < 256; i++) bytes[i] = i;
  auto decoded = decodeText(encodeText(bytes));
  ZuCheck(ZuBSpan{decoded} == ZuBSpan{bytes});
}

static void binary()
{
  ZuTestScopeRT(binary);
  Bytes out;
  ZuCheckRT(decodeBinary("", out, 0) && !out.length());
  for (auto wire : {"YQ", "YQ=="})
    ZuCheckRT(decodeBinary(wire, out, 1) && ZuBSpan{out} == ZuBSpan{"a"});
  for (auto wire : {"YWI", "YWI="})
    ZuCheckRT(decodeBinary(wire, out, 2) && ZuBSpan{out} == ZuBSpan{"ab"});
  ZuCheckRT(decodeBinary("YWJj", out, 3) && ZuBSpan{out} == ZuBSpan{"abc"});
  for (auto wire : {"a", "YQ=", "=YQ=", "====", "YQ===", "YQ==a", "YQ,YQ", "Y Q", "YQ-_"})
    ZuCheckRT(!decodeBinary(wire, out, 100) && ZuBSpan{out} == ZuBSpan{"abc"});
  ZuCheckRT(!decodeBinary("YWJj", out, 2) && ZuBSpan{out} == ZuBSpan{"abc"});
  Bytes bytes;
  bytes.length(256);
  for (unsigned i = 0; i < 256; i++) bytes[i] = i;
  auto wire = encodeBinary(bytes);
  ZuCheckRT(decodeBinary(wire, out, 256) && out == bytes);
}

static void collection()
{
  ZuTestScopeRT(collection);
  Metadata meta;
  uint8_t name[] = {'x', '-', 't', 'a', 'g'};
  uint8_t value[] = {'o', 'n', 'e'};
  ZuCheckRT(meta.receive(name, value));
  memset(name, 'z', sizeof(name));
  memset(value, 'z', sizeof(value));
  ZuCheckRT(meta.receive("x-tag", "two"));
  ZuCheckRT(meta.fields().length() == 2);
  ZuCheckRT(meta.name(meta.fields()[0]) == ZuBSpan{"x-tag"});
  ZuCheckRT(meta.value(meta.fields()[0]) == ZuBSpan{"one"});
  ZuCheckRT(meta.value(meta.fields()[1]) == ZuBSpan{"two"});
  Metadata copy = meta;
  meta.reset(1);
  ZuCheckRT(copy.name(copy.fields()[0]) == ZuBSpan{"x-tag"});
  ZuCheckRT(copy.value(copy.fields()[1]) == ZuBSpan{"two"});
  ZuCheckRT(!meta.add("x", "") && meta.error() == MetaError::Limit);
  meta.reset(34);
  ZuCheckRT(meta.add("x", "a") && meta.size() == 34);
  ZuCheckRT(!meta.add("x", "") && meta.error() == MetaError::Limit);
  ZuCheckRT(meta.fields().length() == 1);
  for (auto reserved : {"grpc-status", "grpc-future", "te", "content-type", "content-length"}) {
    Metadata fields;
    ZuCheckRT(!fields.add(reserved, "x") && fields.error() == MetaError::Reserved);
  }
  for (auto invalid : {"", "X-UPPER", "has space", ":authority", "bad/key"}) {
    Metadata fields;
    ZuCheckRT(!fields.add(invalid, "x") && fields.error() == MetaError::Name);
  }
}

static void binaryFields()
{
  ZuTestScope(binaryFields);
  Metadata meta;
  ZuCheck(meta.receive("x-bin", "YQ==, YWI, ,YWJj"));
  auto fields = meta.fields();
  ZuCheck(fields.length() == 4);
  ZuCheck(meta.value(fields[0]) == ZuBSpan{"a"});
  ZuCheck(meta.value(fields[1]) == ZuBSpan{"ab"});
  ZuCheck(!meta.value(fields[2]).length());
  ZuCheck(meta.value(fields[3]) == ZuBSpan{"abc"});
  ZuCheck(fields[0].namePos == fields[3].namePos && fields[0].binary);
  ZuCheck(meta.name(fields[3]) == ZuBSpan{"x-bin"});
  ZuCheck(!meta.receive("bad-bin", "YQ,broken!") && meta.error() == MetaError::Binary);
  ZuCheck(!meta.add("x", "later"));
  meta.reset(Zhttp::DefltMaxHeaderSection);
  using Key = ZuStringT<"x-selected-bin">;
  ZuCheck((meta.receive<Key, 7>("YQ,YWI")));
  ZuCheck(meta.fields()[0].fixedName == ZuBSpan{Key{}()});
  ZuCheck(meta.fields()[1].key == 7 && meta.value(meta.fields()[1]) == ZuBSpan{"ab"});
  const uint8_t raw[] = {0, ',', 255};
  ZuCheck(meta.add("raw-bin", raw));
  ZuCheck(meta.value(meta.fields()[2]) == ZuBSpan{raw});
  Metadata bounded{37}; // one x-bin entry, no payload
  ZuCheck(bounded.receive("x-bin", ""));
  ZuCheck(!bounded.receive("x-bin", ",") && bounded.error() == MetaError::Limit);
}

using Section = Zhttp::FieldSection::T;
constexpr Section Initial = Zhttp::FieldSection::Final;
constexpr Section Trailing = Zhttp::FieldSection::Trailers;

static void requestFields()
{
  ZuTestScope(requestFields);
  MetaRx rx;
  ZuCheck(rx.header(Initial, "content-type", "application/grpc+proto"));
  ZuCheck(rx.header<ZuStringT<"te">>(Initial, "trailers"));
  ZuCheck(rx.header<ZuStringT<"grpc-timeout">>(Initial, "0n"));
  ZuCheck(rx.header(Initial, "x-dynamic", "a"));
  ZuCheck((rx.header<ZuStringT<"x-selected">, 3>(Initial, "b")));
  ZuCheck(rx.header<ZuStringT<"grpc-extension">>(Initial, "opaque"));
  ZuCheck(rx.initial() && rx.timed() && rx.timeout() == 0);
  ZuCheck(rx.metadata().fields().length() == 2);
  ZuCheck(rx.metadata().fields()[1].fixedName == ZuBSpan{"x-selected"} &&
    rx.metadata().fields()[1].key == 3);
  ZuCheck(rx.end() && rx.finished());
  rx.reset(false, Zhttp::DefltMaxHeaderSection);
  ZuCheck(rx.header(Initial, "content-type", "application/grpc"));
  ZuCheck(!rx.initial() && rx.error() == MetaError::MissingTE);
  rx.reset(false, Zhttp::DefltMaxHeaderSection);
  ZuCheck(!rx.header(Initial, "grpc-timeout", "-1S") && rx.error() == MetaError::Timeout);
  rx.reset(false, Zhttp::DefltMaxHeaderSection);
  ZuCheck(!rx.header(Initial, "grpc-encoding", "gzip"));
  ZuCheck(rx.error() == MetaError::Encoding && rx.status().code == Code::UNIMPLEMENTED);
}

static void responseFields()
{
  ZuTestScope(responseFields);
  MetaRx rx{true};
  rx.httpStatus(200);
  ZuCheck(rx.header(Zhttp::FieldSection::Informational, "grpc-status", "garbage"));
  ZuCheck(rx.header(Initial, "content-type", "application/grpc"));
  ZuCheck(rx.header(Initial, "x-tag", "initial"));
  ZuCheck(rx.initial() && !rx.finished());
  ZuCheck(rx.header(Trailing, "x-tag", "trailing"));
  ZuCheck(rx.header<ZuStringT<"grpc-status">>(Trailing, "7"));
  ZuCheck(rx.header(Trailing, "grpc-message", "access%20denied"));
  ZuCheck(rx.header(Trailing, "grpc-status-details-bin", "AAH/"));
  ZuCheck(!rx.finished() && rx.end() && rx.finished());
  ZuCheck(rx.status().code == Code::PERMISSION_DENIED && rx.status().message == "access denied");
  const uint8_t details[] = {0, 1, 255};
  ZuCheck(ZuBSpan{rx.status().details} == ZuBSpan{details});
  ZuCheck(rx.metadata().value(rx.metadata().fields()[0]) == ZuBSpan{"initial"});
  ZuCheck(rx.trailers().value(rx.trailers().fields()[0]) == ZuBSpan{"trailing"});
  rx.reset(true, Zhttp::DefltMaxHeaderSection);
  ZuCheck(rx.header(Initial, "content-type", "application/grpc+proto"));
  ZuCheck(rx.header(Initial, "grpc-status", "12"));
  ZuCheck(rx.initial() && rx.trailersOnly() && !rx.finished());
  ZuCheck(rx.end() && rx.status().code == Code::UNIMPLEMENTED);
}

static void responseErrors()
{
  ZuTestScopeRT(responseErrors);
  for (auto status : {200U, 400U, 401U, 403U, 404U, 429U, 502U, 503U, 504U}) {
    MetaRx rx{true};
    rx.httpStatus(status);
    ZuCheckRT(rx.header(Initial, "content-type", "application/grpc"));
    ZuCheckRT(rx.initial());
    ZuCheckRT(!rx.end() && rx.error() == MetaError::MissingStatus);
    ZuCheckRT(rx.status().code == httpCode(status));
  }
  for (auto code : {"", "17", "1,2", "garbage"}) {
    MetaRx rx{true};
    ZuCheckRT(!rx.header(Initial, "grpc-status", code) && rx.error() == MetaError::Status);
    ZuCheckRT(!rx.header(Initial, "content-type", "application/grpc"));
  }
  MetaRx rx{true};
  ZuCheckRT(rx.header(Initial, "content-type", "application/grpc"));
  ZuCheckRT(rx.initial());
  ZuCheckRT(rx.header(Trailing, "grpc-status", "0"));
  ZuCheckRT(!rx.header(Trailing, "grpc-status", "0") && rx.error() == MetaError::Duplicate);
  rx.reset(true, Zhttp::DefltMaxHeaderSection);
  ZuCheckRT(!rx.header(Initial, "content-type", "application/json"));
  ZuCheckRT(rx.error() == MetaError::ContentType);
  rx.reset(true, Zhttp::DefltMaxHeaderSection);
  ZuCheckRT(!rx.header(Initial, "bad-bin", "YWJj,invalid!") && rx.error() == MetaError::Binary);
}

static void sectionErrors()
{
  ZuTestScope(sectionErrors);
  MetaRx rx{true};
  ZuCheck(!rx.header(Trailing, "grpc-status", "0") && rx.error() == MetaError::Section);
  rx.reset(true, Zhttp::DefltMaxHeaderSection);
  ZuCheck(rx.header(Initial, "content-type", "application/grpc"));
  ZuCheck(rx.header(Initial, "grpc-message", "early"));
  ZuCheck(rx.initial());
  ZuCheck(!rx.header(Trailing, "grpc-status", "0") && rx.error() == MetaError::Section);
  rx.reset(true, Zhttp::DefltMaxHeaderSection);
  ZuCheck(rx.header(Initial, "content-type", "application/grpc"));
  ZuCheck(rx.header(Initial, "grpc-status", "0"));
  ZuCheck(rx.initial());
  ZuCheck(!rx.header(Trailing, "x-extra", "late") && rx.error() == MetaError::Section);
  MetaRx tiny{true, 32};
  ZuCheck(!tiny.header(Initial, "content-type", "application/grpc"));
  ZuCheck(tiny.error() == MetaError::Limit && tiny.status().code == Code::RESOURCE_EXHAUSTED);
}

using Secret = ZuStringT<"x-secret">;
using SeedList = ZuTypeList<Secret, ZuTypeList<ZuStringT<"public">>>;
ZhttpHdrCatalogDerive(SeedHeaders, SeedList);
ZhttpHdrCatalogImpl(SeedHeaders);
struct SecretPolicy {
  using Keys = ZuStringTL<"x-secret", "authorization">;
  bool neverIndex(ZuBSpan name) const {
    static constexpr auto matcher = ZuMatcher<SecretPolicy>();
    return matcher.exact(name) >= 0;
  }
};

static void production()
{
  ZuTestScope(production);
  Metadata meta;
  ZuCheck(meta.add("x-secret", "token") && meta.add("x-secret", "again"));
  const uint8_t bytes[] = {0, 1, 255};
  ZuCheck(meta.add("x-bin", bytes));
  SecretPolicy policy;
  MetaFields<SeedHeaders, SecretPolicy> fields{meta, &policy};
  unsigned fixed = 0, dynamic = 0, sensitive = 0;
  fields.header<Secret, ZuStringT<"public">>([&fixed]() { ++fixed; });
  String wire;
  fields.header([&fields, &wire, &dynamic, &sensitive](auto name, const auto &value) {
    ++dynamic;
    if (fields.neverIndex(name)) ++sensitive;
    wire << name << ':' << value << ';';
  });
  ZuCheck(fixed == 1 && dynamic == 3 && sensitive == 2);
  ZuCheck(wire == "x-secret:token;x-secret:again;x-bin:AAH/;");
  // The same policy applies to the matching fixed seed and trailer provider.
  ZuCheck(fields.neverIndex(Secret{}()) && !fields.neverIndex("x-bin"));
  String text;
  text << TextValue{{"message %+"}};
  ZuCheck(text == "message %25+");
  String binary;
  binary << BinaryValue{bytes};
  ZuCheck(binary == "AAH/");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(timeouts);
  ZuTestCall(status);
  ZuTestCall(text);
  ZuTestCall(binary);
  ZuTestCall(collection);
  ZuTestCall(binaryFields);
  ZuTestCall(requestFields);
  ZuTestCall(responseFields);
  ZuTestCall(responseErrors);
  ZuTestCall(sectionErrors);
  ZuTestCall(production);
}
