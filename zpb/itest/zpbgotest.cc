//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmRBTree.hh>
#include <zlib/ZpbReflect.hh>

#include "ZiTestResidue.hh"
#include "ZpbTestUtil.hh"

using namespace ZuTestUtil;
using namespace ZpbTest;

ZuStructFacet(PrefixedPB);
ZpbConfig(PrefixedPB, Zpb_SizePrefixed<>);

struct Child { uint32_t value; };
ZfStruct(, (Child, PB), (value,, UInt32));
Zpb::AsMessage<"fixture.Child"> Zpb_Fmt(Child *);
struct Other { ZuCSpan value; };
ZfStruct(, (Other, PB), (value,, String));
Zpb::AsMessage<"fixture.Other"> Zpb_Fmt(Other *);
ZuDerive(Choice, (ZuUnion<void, Child, Other>));
Zpb::AsUnion<"fixture.Choice", Zpb::Alt<"first", 10, Child>,
  Zpb::Alt<"second", 11, Other>> Zpb_Fmt(Choice *);
ZuDerive(Labels, (ZmRBTreeKV<ZuCSpan, int32_t,
  ZmRBTreeHeapID<"zpbgotest.Labels">>));
Zpb::AsMap<"fixture.Labels", ZfFieldTC::String, ZfFieldTC::Int32>
  Zpb_Fmt(Labels *);
ZuDerive(Samples, (ZtArray<int32_t, ZtArrayHeapID<"zpbgotest.Samples">>));
ZuDerive(Flags, (ZtArray<uint8_t, ZtArrayHeapID<"zpbgotest.Flags">>));

struct Row {
  uint64_t id;
  int64_t delta;
  double ratio;
  ZuCSpan text;
  ZuBSpan bytes;
  Samples samples;
  Flags flags;
  Child child;
  Labels labels;
  Choice choice;
};
ZfStruct(, (Row, PB),
  (id, (PB::FieldID<1>), UInt64),
  (delta, (PB::FieldID<2>, PB::ZigZag), Int64),
  (ratio, (PB::FieldID<3>), Float),
  (text, (PB::FieldID<4>), String),
  (bytes, (PB::FieldID<5>), Bytes),
  (samples, (PB::FieldID<6>, PB::Packed, PB::ZigZag), Int32Vec),
  (flags, (PB::FieldID<7>, PB::Packed, PB::BoolVec), UInt8Vec),
  (child, (PB::FieldID<8>), UDT),
  (labels, (PB::FieldID<9>), UDT),
  (choice,, UDT));
Zpb::AsMessage<"fixture.Row"> Zpb_Fmt(Row *);
ZuStructReflect(Row, ZuFacet::PrefixedPB, ZuFacet::PB);
ZuStructReflect(Child, ZuFacet::PrefixedPB, ZuFacet::PB);
ZuStructReflect(Other, ZuFacet::PrefixedPB, ZuFacet::PB);

struct Blob { ZuBSpan bytes; };
ZfStruct(, (Blob, PB), (bytes,, Bytes));
Zpb::AsMessage<"fixture.Blob"> Zpb_Fmt(Blob *);
ZuStructReflect(Blob, ZuFacet::PrefixedPB, ZuFacet::PB);

static bool writeBytes(const Zi::Path &path, ZuBSpan bytes)
{
  return write(path, {reinterpret_cast<const char *>(bytes.data()), bytes.length()});
}

static bool schema(const Zi::Path &path)
{
  using namespace Zpb::Reflect;
  DescSet set;
  if (!exportSchema<Row>(set, "row.proto", "fixture", Syntax::Proto3) ||
    !exportSchema<Blob>(set, "blob.proto", "fixture", Syntax::Proto3))
    return false;
  Bytes output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto bytes = Zpb::save(builder, set);
  return !builder.failed() && verifySchema(bytes) && writeBytes(path, bytes);
}

static int peer(const Zi::Path &cwd, ZuCSpan kind, ZuCSpan mode)
{
  auto path = [&cwd](ZuCSpan name) {
    return ZiFile::append(cwd, Zi::Path{name});
  };
  return run(Zi::Path{ZPB_GO_PEER}, cwd, path("peer.log"),
    "-schema", path("schema.pb"), "-kind", kind, "-mode", mode,
    "-input", path("input.pb"), "-output", path("output.pb"));
}

template <typename Facet>
static void row(ZuCSpan mode)
{
  ZuTestScopeRT(row);
  auto cwd = ZiTestResidue::dir(mode);
  auto path = [&cwd](ZuCSpan name) {
    return ZiFile::append(cwd, Zi::Path{name});
  };
  ZuCheckRT(schema(path("schema.pb")));
  Row input;
  input.id = UINT64_MAX - 1;
  input.delta = -12345;
  input.ratio = 1.25;
  input.text = "native";
  input.bytes = ZuBSpan{"\x00\xff", 2};
  input.samples.push(-1);
  input.samples.push(0);
  input.samples.push(12345);
  input.flags.push(1);
  input.flags.push(0);
  input.flags.push(1);
  input.child.value = 42;
  input.labels.add("a", -7);
  input.labels.add("b", 9);
  input.choice.p<1>(Child{99});
  Bytes output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  // The backward builder leaves this unknown field after the typed fields.
  builder.varint(7);
  builder.tag(99, Zpb::Wire::Varint);
  auto wire = Zpb::save<Facet>(builder, input);
  ZuCheckRT(!builder.failed() && writeBytes(path("input.pb"), wire));
  ZuCheckRT(peer(cwd, "row", mode) == 0, return);
  auto encoded = read(path("output.pb"));
  ZuBSpan bytes{encoded.data(), encoded.length()};
  ZuCheckRT((Zpb::verify<Row, Facet>(bytes)), return);
  auto response = Zpb::load<Row, Facet>(bytes);
  ZuCheckRT(response.id == 42 && response.delta == input.delta &&
    response.ratio == input.ratio && response.text == "go" &&
    response.bytes == input.bytes && response.samples == input.samples &&
    response.flags == input.flags && response.child.value == input.child.value);
  ZuCheckRT(response.labels.count_() == 2 &&
    response.labels.findVal("a") == -7 && response.labels.findVal("b") == 9);
  ZuCheckRT(response.choice.type() == 2 &&
    response.choice.template p<2>().value == "go");
}

static void blobs()
{
  ZuTestScopeRT(blobs);
  auto cwd = ZiTestResidue::dir("blobs");
  auto path = [&cwd](ZuCSpan name) {
    return ZiFile::append(cwd, Zi::Path{name});
  };
  ZuCheckRT(schema(path("schema.pb")));
  // Raw protobuf payload sizes: 0, 127, 128, 16383, 16384.
  const unsigned lengths[] = {0, 125, 126, 16380, 16381};
  Bytes sequence;
  for (auto length : lengths) {
    Bytes data;
    data.length(length);
    if (length) memset(data.data(), 0x5a, length);
    Blob input{data};
    Bytes output;
    Zpb::ArrayStorage storage{output};
    Zpb::Builder builder{storage};
    auto frame = Zpb::save<ZuFacet::PrefixedPB>(builder, input);
    ZuCheckRT(!builder.failed());
    sequence.append(frame.data(), frame.length());
  }
  ZuCheckRT(writeBytes(path("input.pb"), sequence));
  ZuCheckRT(peer(cwd, "blob", "delimited") == 0, return);
  auto encoded = read(path("output.pb"));
  ZuBSpan bytes{encoded.data(), encoded.length()};
  unsigned pos = 0;
  for (auto length : lengths) {
    unsigned begin = pos;
    ZuBSpan payload;
    ZuCheckRT(Zpb::Wire::readLength(bytes, pos, payload), return);
    ZuBSpan frame{bytes.data() + begin, pos - begin};
    ZuCheckRT((Zpb::verify<Blob, ZuFacet::PrefixedPB>(frame)), return);
    auto response = Zpb::load<Blob, ZuFacet::PrefixedPB>(frame);
    ZuCheckRT(response.bytes.length() == length);
    bool valid = true;
    for (auto byte : response.bytes) valid &= byte == 0x5a;
    ZuCheckRT(valid);
  }
  ZuCheckRT(pos == bytes.length());
}

static void invalid()
{
  ZuTestScopeRT(invalid);
  auto cwd = ZiTestResidue::dir("invalid");
  auto path = [&cwd](ZuCSpan name) {
    return ZiFile::append(cwd, Zi::Path{name});
  };
  ZuCheckRT(schema(path("schema.pb")));
  const ZuBSpan frames[] = {
    {"\x80", 1}, // truncated prefix
    {"\x02\x08", 2}, // truncated payload
    {"\xff\xff\xff\xff\xff\xff\xff\xff\xff\x02", 10},
    {"\x81\x80\x40", 3} // over Go's configured 1 MiB message limit
  };
  for (auto frame : frames) {
    ZuCheckRT(writeBytes(path("input.pb"), frame));
    ZuCheckRT(peer(cwd, "blob", "delimited") == 1);
    ZuCheckRT(read(path("peer.log")).find("message 0:") >= 0);
  }
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZiTestResidue::init("zpbgotest");
  ZuTestMain();
  ZuTestCall((row<ZuFacet::PB>), "raw");
  ZuTestCall((row<ZuFacet::PrefixedPB>), "delimited");
  ZuTestCall(blobs);
  ZuTestCall(invalid);
}
