//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZtScratch.hh>
#include <zlib/ZfASN1.hh>
#include <zlib/ZfCBOR.hh>
#include <zlib/ZfCf.hh>
#include <zlib/ZfCLI.hh>
#include <zlib/ZfCSV.hh>
#include <zlib/ZfDBUS.hh>
#include <zlib/ZfJSON.hh>
#include <zlib/ZfTOML.hh>
#include <zlib/ZfURI.hh>
#include <zlib/ZfYAML.hh>

using namespace ZuTestUtil;

struct Row {
  int value = 1;
  int tail = 2;

  friend ZfStructPrint ZuPrintType(Row *);
};

ZfStruct(, (Row, JSON, CSV, CBOR, ASN1, DBUS, URI, CLI, Cf, TOML, YAML),
  ((old, Skip, int),		(Ctor<9>, Mutable, (Keys<0, 5>), Deflt<7>),		Int32),
  (value,
    (Ctor<0>, Mutable, Keys<0>, CLI::Long<"value">),				Int32),
  ((text, Skip, ZuCSpan),	(Required),					String),
  (tail,			(Ctor<1>, Mutable, Keys<1>, CLI::Long<"tail">),	Int32));

ZfStructImpl(Row);

struct Empty { };
ZfStruct(, (Empty, JSON), ((old, Skip, int), (Mutable), Int32));
ZfStructImpl(Empty);

struct Raw { int value; };
ZuStruct(Raw,
  ((old, Skip, (ZuTuple<int, int>)), (Keys<9>)),
  (value, (Keys<0>)));

template <typename Field, typename = void>
struct HasGet : public ZuFalse { };
template <typename Field>
struct HasGet<Field, decltype(
  Field::get(ZuDeclVal<const typename Field::O &>()), void())> :
  public ZuTrue { };

using Old = ZfField(Row, old);
ZuAssert(!HasGet<Old>{});
ZuAssert(!ZuFieldActive<Old>{});
ZuAssert(Old::deflt() == 7);
ZuAssert(ZuFields<Row>::N == 4);
ZuAssert(ZuActiveFields<Row>::N == 2);
ZuAssert(ZuActiveFields<Empty>::N == 0);
ZuAssert(ZuStructKeyIDs<Row>::N == 2);
ZuAssert(ZuFields<ZuStructTuple<Row>>::N == 2);
ZuAssert((ZuFields<ZuStructKeyT<Row, 0>>::N == 1));
ZuAssert(ZfFieldIndex(Row, value) == 0);
ZuAssert(ZfFieldIndex(Row, tail) == 1);
ZuAssert(ZuFields<Raw>::N == 2);
ZuAssert(ZuActiveFields<Raw>::N == 1);
ZuAssert(ZuStructKeyIDs<Raw>::N == 1);
ZuAssert(!ZfFieldFilter::All<Old>{});
ZuAssert(!ZfFieldFilter::Ctor<Old>{});
ZuAssert(!ZfFieldFilter::Load<Old>{});
ZuAssert(!ZfFieldFilter::Init<Old>{});
ZuAssert(!ZfFieldFilter::Save<Old>{});
ZuAssert(!ZfFieldFilter::Upd<Old>{});
ZuAssert(!ZfFieldFilter::Del<Old>{});

ZuDerive(Text, (ZtString<ZtStringHeapID<"ZfSkipTest">>));
ZuDerive(Bytes, (ZtArray<uint8_t, ZtArrayHeapID<"ZfSkipTest">>));

ZuStructFacet(SmallTOML);
ZfTOMLConfig(SmallTOML, (
  ZfTOML_ScalarScratchSize<1, ZfTOML_PathScratchSize<1>>));

struct TOMLChild {
  Bytes bytes;
};
struct TOMLRoot {
  TOMLChild child;
};
ZfStruct(, (TOMLChild, TOML, SmallTOML),
  (bytes, (Mutable, TOML::Literal), Bytes));
ZfStruct(, (TOMLRoot, TOML, SmallTOML),
  (child, (Mutable), UDT));

static void tomlConfig()
{
  ZuTestScope(tomlConfig);
  TOMLRoot orig;
  orig.child.bytes = ZuBSpan{"scratch fallback"};
  Text deflt, small;
  ZfTOML::save(deflt, orig);
  ZfTOML::save<ZuFacet::SmallTOML>(small, orig);
  ZuCheck(small == deflt);
  auto parsed = ZfTOML::scan(small);
  ZuCheck(parsed.p<0>() >= 0);
  TOMLRoot copy;
  ZfTOML::handler<TOMLRoot, ZuFacet::SmallTOML>(
    parsed.p<1>().ptr()).load(copy);
  ZuCheck(copy.child.bytes == orig.child.bytes);
}

static void metadata()
{
  ZuTestScope(metadata);
  auto fields = ZfVFields<Row>();
  ZuCheck(fields.length() == 2);
  ZuCheck(fields[0]->id == "value" && fields[1]->id == "tail");
  ZuCheck(ZfVFieldMatcher<Row>()("value") == 0);
  ZuCheck(ZfVFieldMatcher<Row>()("tail") == 1);
  ZuCheck(ZfVFieldMatcher<Row>()("old") < 0);
  ZuCheck(ZfVFieldMatcher<Row>()("text") < 0);
  ZuCheck(!ZfVFields<Empty>().length());
  Row row{11, 12};
  auto tuple = ZuStructExtract<ZuAlwaysTrue>(row);
  ZuCheck(tuple.p<0>() == 11 && tuple.p<1>() == 12);
  ZuCheck(ZuStructKey<0>(row).p<0>() == 11);
  ZuCheck(ZuStructKey<1>(row).p<0>() == 12);
  ZuCheck(ZuStructKey<0>(Raw{13}).p<0>() == 13);
  auto out = ZtScratch(Text, 0);
  out << row;
  ZuCheck(out == "{value:11,tail:12}");
}

static void json()
{
  ZuTestScope(json);
  auto out = ZtScratch(Text, 0);
  ZfJSON::save<ZuFacet::JSON, ZuAlwaysTrue>(out, Row{11, 12});
  ZuCheck(out == "{\"value\":11,\"tail\":12}");
  out.length_(0);
  ZfJSON::save<ZuFacet::JSON, ZfFieldFilter::Del>(out, Row{11, 12});
  ZuCheck(out == "{\"value\":11}");
  char input[] = "{\"old\":99,\"value\":21,\"text\":\"ignored\",\"tail\":22}";
  auto parsed = ZfJSON::scan(input);
  auto h = ZfJSON::handler<Row>((*parsed.p<1>())[0]);
  auto row = h.ctor();
  ZuCheck(row.value == 21 && row.tail == 22);
  Row loaded;
  h.load(loaded);
  ZuCheck(loaded.value == 21 && loaded.tail == 22);
  out.length_(0);
  ZfJSON::save(out, Empty{});
  ZuCheck(out == "{}");
}

static void binary()
{
  ZuTestScope(binary);
  Row row{11, 12};
  auto bytes = ZtScratch(Bytes, 0);
  ZfCBOR::save(bytes, row);
  auto cbor = ZfCBOR::handler<Row>(bytes).ctor();
  ZuCheck(cbor.value == 11 && cbor.tail == 12);
  bytes.length(0);
  ZfASN1::save(bytes, row);
  auto asn = ZfASN1::handler<Row>(bytes).ctor();
  ZuCheck(asn.value == 11 && asn.tail == 12);
  bytes.length(0);
  ZfDBUS::save(bytes, row);
  ZuCheck(ZfDBUS::signature<Row>() == "ii");
  auto dbus = ZfDBUS::handler<Row>({bytes, "ii"}).ctor();
  ZuCheck(dbus.value == 11 && dbus.tail == 12);
}

template <typename Save, typename Scan, typename Handler>
static void text(Save save, Scan scan, Handler handler)
{
  ZuTestScope(text);
  auto out = ZtScratch(Text, 0);
  save(out, Row{11, 12});
  auto parsed = scan(out);
  auto row = handler(parsed.template p<1>().ptr()).ctor();
  ZuCheck(row.value == 11 && row.tail == 12);
  Row loaded;
  handler(parsed.template p<1>().ptr()).load(loaded);
  ZuCheck(loaded.value == 11 && loaded.tail == 12);
}

static void csv()
{
  ZuTestScope(csv);
  auto out = ZtScratch(Text, 0);
  ZfCSV::Writer<Row, ZuFacet::CSV> writer;
  writer.saveHdr(out);
  writer.save(out, Row{11, 12});
  ZuCheck(out == "value,tail\n11,12\n");
  auto reader = ZfCSV::reader<Row>();
  unsigned count = 0;
  reader.read(out.span(), [&count](const auto &h) {
    auto row = h.ctor();
    ZuCheck(row.value == 11 && row.tail == 12);
    ++count;
  });
  ZuCheck(count == 1);
}

static void cli()
{
  ZuTestScope(cli);
  ZfCLI::OutArgv args;
  args.argv.push("skip");
  args.argv.push("--value");
  args.argv.push("11");
  args.argv.push("--tail");
  args.argv.push("12");
  args.finish();
  Row row;
  ZuCheck(ZfCLI::load(row, args.argv) == 1);
  ZuCheck(row.value == 11 && row.tail == 12);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(metadata);
  ZuTestCall(tomlConfig);
  ZuTestCall(json);
  ZuTestCall(binary);
  ZuTestCall(csv);
  ZuTestCall(cli);
  ZuTestCall(text,
    [](auto &s, const Row &row) { ZfCf::save(s, row); },
    [](ZuCSpan s) { return ZfCf::scan(s); },
    [](auto node) { return ZfCf::handler<Row>(node); });
  ZuTestCall(text,
    [](auto &s, const Row &row) { ZfTOML::save(s, row); },
    [](ZuCSpan s) { return ZfTOML::scan(s); },
    [](auto node) { return ZfTOML::handler<Row>(node); });
  ZuTestCall(text,
    [](auto &s, const Row &row) { ZfYAML::save(s, row); },
    [](ZuCSpan s) { return ZfYAML::scan(s); },
    [](auto node) { return ZfYAML::handler<Row>(node); });
  ZuTestCall(text,
    [](auto &s, const Row &row) { ZfURI::save(s, row); },
    [](ZuSpan<char> s) { return ZfURI::scan(s); },
    [](auto node) { return ZfURI::handler<Row>(node); });
}
