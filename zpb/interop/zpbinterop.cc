//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <google/protobuf/compiler/parser.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/dynamic_message.h>
#include <google/protobuf/io/tokenizer.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
#include <google/protobuf/message_lite.h>
#include <google/protobuf/util/delimited_message_util.h>
#include <google/protobuf/util/message_differencer.h>

#include <iostream>
#include <string>

#include <zlib/ZuBox.hh>
#include <zlib/ZuMatcher.hh>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmRBTree.hh>
#include <zlib/ZmTime.hh>
#include <zlib/ZiFile.hh>
#include <zlib/ZpbReflect.hh>

#include <interop.hh>
#include <zprotoc_values_row.hh>

using namespace ZuTestUtil;
using namespace Zpb::Reflect;

ZuDerive(Output, (ZtArray<uint8_t, ZtArrayHeapID<"zpbinterop.Output">>));

ZuStructFacet(PrefixedPB);
ZpbConfig(PrefixedPB, Zpb_SizePrefixed<>);

struct DelimitedRow { ZuCSpan name; };
ZfStruct(, (DelimitedRow, PB), (name,, String));
Zpb::AsMessage<"google.protobuf.FileDescriptorProto">
  Zpb_Fmt(DelimitedRow *);
ZuStructReflect(DelimitedRow, ZuFacet::PrefixedPB, ZuFacet::PB);

static void testDelimited()
{
  ZuTestScope(testDelimited);
  // Field 1 matches FileDescriptorProto::name. Exercise empty messages and
  // both sides of the first two varint-length boundaries.
  const unsigned lengths[] = {0, 125, 126, 16380, 16381};
  google::protobuf::DynamicMessageFactory factory;
  auto desc = google::protobuf::FileDescriptorProto::descriptor();
  auto name = desc->FindFieldByNumber(1);
  for (auto length : lengths) {
    ProtoText text;
    text.length(length);
    if (length) memset(text.data(), 'x', length);
    DelimitedRow native{length ? ZuCSpan{text} : ZuCSpan{}};
    Output output;
    Zpb::ArrayStorage storage{output};
    Zpb::Builder builder{storage};
    auto frame = Zpb::save<ZuFacet::PrefixedPB>(builder, native);
    ZuCheck(!builder.failed());
    if (!length) ZuCheck(frame.length() == 1 && !frame[0]);

    auto canonical = factory.GetPrototype(desc)->New();
    auto reflection = canonical->GetReflection();
    google::protobuf::io::ArrayInputStream input{frame.data(), int(frame.length())};
    bool eof = false;
    ZuCheck(google::protobuf::util::ParseDelimitedFromZeroCopyStream(
      canonical, &input, &eof) && !eof);
    auto loaded = reflection->GetString(*canonical, name);
    ZuCheck(loaded.size() == length &&
      (!length || !memcmp(loaded.data(), text.data(), length)));

    canonical->Clear();
    if (length) reflection->SetString(canonical, name,
      std::string{text.data(), length});
    std::string encoded;
    {
      google::protobuf::io::StringOutputStream stream{&encoded};
      ZuCheck(google::protobuf::util::SerializeDelimitedToZeroCopyStream(
	*canonical, &stream));
    }
    ZuBSpan bytes{encoded.data(), unsigned(encoded.size())};
    ZuCheck(bytes == frame);
    ZuCheck((Zpb::verify<DelimitedRow, ZuFacet::PrefixedPB>(bytes)));
    ZuCheck((Zpb::load<DelimitedRow, ZuFacet::PrefixedPB>(bytes).name == text));
    delete canonical;
  }
}

struct NativeItem { uint32_t id = 0; };
ZfStruct(, (NativeItem, PB), (id,, UInt32));
Zpb::AsMessage<"demo.NativeItem"> Zpb_Fmt(NativeItem *);
ZuDerive(NativeMap, (ZmRBTreeKV<int32_t, int32_t,
  ZmRBTreeHeapID<"zpbinterop.NativeMap">>));
Zpb::AsMap<"demo.NativeMap", ZfFieldTC::Int32, ZfFieldTC::Int32>
  Zpb_Fmt(NativeMap *);
ZuDerive(NativeBlob, (ZtArray<uint8_t,
  ZtArrayHeapID<"zpbinterop.NativeBlob">>));
ZuDerive(NativeBlobMap, (ZmRBTreeKV<uint32_t, NativeBlob,
  ZmRBTreeHeapID<"zpbinterop.NativeBlobMap">>));
Zpb::AsMap<"demo.NativeBlobMap", ZfFieldTC::UInt32, ZfFieldTC::Bytes>
  Zpb_Fmt(NativeBlobMap *);
ZuDerive(NativeChoice, (ZuUnion<void, NativeItem>));
Zpb::AsUnion<"demo.NativeChoice",
  Zpb::Alt<"selected", 5, NativeItem>> Zpb_Fmt(NativeChoice *);
struct NativeRow {
  uint32_t id = 0;
  NativeMap entries;
  NativeChoice choice;
  NativeBlobMap blobs;
};
ZfStruct(, (NativeRow, PB),
  (id,, UInt32), (entries,, UDT), (choice,, UDT), (blobs,, UDT));
Zpb::AsMessage<"demo.NativeRow"> Zpb_Fmt(NativeRow *);
struct RequiredChoiceItem { uint32_t id = 0; };
ZfStruct(, (RequiredChoiceItem, PB), (id, (Required), UInt32));
Zpb::AsMessage<"demo.RequiredChoiceItem"> Zpb_Fmt(RequiredChoiceItem *);
struct FinalChoiceItem { uint32_t id = 0; };
ZfStruct(, (FinalChoiceItem, PB), (id,, UInt32));
Zpb::AsMessage<"demo.FinalChoiceItem"> Zpb_Fmt(FinalChoiceItem *);
ZuDerive(RequiredChoice,
  (ZuUnion<void, RequiredChoiceItem, FinalChoiceItem>));
Zpb::AsUnion<"demo.RequiredChoice",
  Zpb::Alt<"first", 4, RequiredChoiceItem>,
  Zpb::Alt<"final", 9, FinalChoiceItem>> Zpb_Fmt(RequiredChoice *);
struct RequiredChoiceRow { RequiredChoice choice; };
ZfStruct(, (RequiredChoiceRow, PB), (choice,, UDT));
Zpb::AsMessage<"demo.RequiredChoiceRow"> Zpb_Fmt(RequiredChoiceRow *);
struct NativeMappedRow { ZuFixed amount; };
ZfStruct(, (NativeMappedRow, PB), (amount,, Fixed));
Zpb::AsMessage<"demo.NativeMappedRow"> Zpb_Fmt(NativeMappedRow *);
struct NativeDefaults {
  int32_t number;
  ZuCSpan text;
  ZuBSpan bytes;
};
ZfStruct(, (NativeDefaults, PB),
  (number, (Deflt<-12>),		Int32),
  (text, (Deflt<"a\n\"b"_z>),		String),
  (bytes, (Deflt<"\001\377"_z>),	Bytes),
  ((former, Skip, int),,		Int32));
Zpb::AsMessage<"demo.NativeDefaults"> Zpb_Fmt(NativeDefaults *);
ZtEnumNS(, NativeColor, int32_t, Red, Blue);
ZtEnumImplNS(NativeColor);
struct DepColorMap : NativeColor::Map {
  static constexpr const auto &id() { return "dep.NativeColor"; }
};
struct DepColorRow { int32_t color = NativeColor::Red; };
ZfStruct(, (DepColorRow, PB), (color, (Enum<DepColorMap>), Int32));
Zpb::AsMessage<"dep.DepColorRow"> Zpb_Fmt(DepColorRow *);
struct ImportedColorRow { int32_t color = NativeColor::Red; };
ZfStruct(, (ImportedColorRow, PB),
  (color, (Enum<DepColorMap>), Int32));
Zpb::AsMessage<"demo.ImportedColorRow"> Zpb_Fmt(ImportedColorRow *);
struct NativeEnumRow { int32_t color = NativeColor::Blue; };
ZfStruct(, (NativeEnumRow, PB),
  (color, (Enum<NativeColor::Map>, Deflt<NativeColor::Blue>), Int32));
Zpb::AsMessage<"demo.NativeEnumRow"> Zpb_Fmt(NativeEnumRow *);

struct CompatOld { uint32_t id = 0; };
ZfStruct(, (CompatOld, PB), (id,, UInt32));
Zpb::AsMessage<"demo.Compat"> Zpb_Fmt(CompatOld *);
struct CompatNew { uint32_t id = 0; ZuCSpan label; uint32_t revision = 0; };
ZfStruct(, (CompatNew, PB),
  (id,, UInt32), (label,, String), (revision,, UInt32));
Zpb::AsMessage<"demo.Compat"> Zpb_Fmt(CompatNew *);

struct NativeNode {
  uint32_t value = 0;
  ZuPtr<NativeNode> next;
};
ZfStruct(, (NativeNode, PB), (value,, UInt32), (next,, UDT));
Zpb::AsMessage<"demo.NativeNode"> Zpb_Fmt(NativeNode *);

struct NativeRequired { uint32_t id = 0; };
ZfStruct(, (NativeRequired, PB), (id, (Required), UInt32));
Zpb::AsMessage<"demo.NativeRequired"> Zpb_Fmt(NativeRequired *);

ZuStructFacet(InteropFacet);
struct FacetRow {
  uint32_t first = 0;
  uint32_t last = 37;
  uint32_t hidden = 0;
};
ZfStruct(, (FacetRow, PB),
  (first,,			UInt32),
  ((retired, Skip, int),,	Int32),
  (last,,			UInt32),
  (hidden,,			UInt32));
Zpb::AsMessage<"demo.FacetRow"> Zpb_Fmt(FacetRow *);
ZfStructRender(, FacetRow, InteropFacet,
  (last, PB::ID<"renamed">, Deflt<37>), first);

struct BenchSmall { uint32_t id = 0; bool enabled = false; };
ZfStruct(, (BenchSmall, PB), (id,, UInt32), (enabled,, Bool));
Zpb::AsMessage<"bench.Small"> Zpb_Fmt(BenchSmall *);
struct BenchNested { BenchSmall child; uint32_t sequence = 0; };
ZfStruct(, (BenchNested, PB), (child,, UDT), (sequence,, UInt32));
Zpb::AsMessage<"bench.Nested"> Zpb_Fmt(BenchNested *);
ZuDerive(BenchNumbers,
  (ZtArray<int32_t, ZtArrayHeapID<"zpbinterop.BenchNumbers">>));
ZuDerive(BenchMap,
  (ZmRBTreeKV<int32_t, int32_t,
    ZmRBTreeHeapID<"zpbinterop.BenchMap">>));
Zpb::AsMap<"bench.Map", ZfFieldTC::Int32, ZfFieldTC::Int32>
  Zpb_Fmt(BenchMap *);
struct BenchRepeated { BenchNumbers numbers; BenchMap entries; };
ZfStruct(, (BenchRepeated, PB),
  (numbers,, Int32Vec), (entries,, UDT));
Zpb::AsMessage<"bench.Repeated"> Zpb_Fmt(BenchRepeated *);
struct BenchLarge { ZuBSpan bytes; ZuCSpan text; };
ZfStruct(, (BenchLarge, PB), (bytes,, Bytes), (text,, String));
Zpb::AsMessage<"bench.Large"> Zpb_Fmt(BenchLarge *);

ZuDerive(NativeSmallValues,
  (ZtArray<int16_t, ZtArrayHeapID<"zpbinterop.SmallValues">>));
ZuDerive(NativeTextValues,
  (ZtArray<ZuCSpan, ZtArrayHeapID<"zpbinterop.TextValues">>));
ZuDerive(NativeTinyValues,
  (ZtArray<int8_t, ZtArrayHeapID<"zpbinterop.TinyValues">>));
ZuDerive(NativeCountValues,
  (ZtArray<uint8_t, ZtArrayHeapID<"zpbinterop.CountValues">>));
ZuDerive(NativeWideValues,
  (ZtArray<uint16_t, ZtArrayHeapID<"zpbinterop.WideValues">>));
ZuDerive(NativeBlobValues,
  (ZtArray<ZuBSpan, ZtArrayHeapID<"zpbinterop.BlobValues">>));
ZuDerive(NativeRatios,
  (ZtArray<float, ZtArrayHeapID<"zpbinterop.Ratios">>));
ZuDerive(NativePrecisions,
  (ZtArray<double, ZtArrayHeapID<"zpbinterop.Precisions">>));
struct NativeScalarRow {
  int8_t small = 0;
  uint8_t count = 0;
  int16_t signed16 = 0;
  uint16_t unsigned16 = 0;
  float ratio = 0;
  double precise = 0;
  ZuCSpan text;
  ZuBSpan bytes;
  NativeSmallValues values;
  NativeTextValues labels;
  NativeTinyValues smalls;
  NativeCountValues counts;
  NativeWideValues unsignedValues;
  NativeBlobValues blobs;
  NativeRatios ratios;
  NativePrecisions precisions;
};
ZfStruct(, (NativeScalarRow, PB),
  (small,, Int8), (count,, UInt8), (ratio,,	Float),
  (precise,, Float), (text,, String), (bytes,,	Bytes),
  (values,, Int16Vec), (labels,,		StringVec),
  (signed16,, Int16), (unsigned16,,		UInt16),
  (smalls,, Int8Vec), (counts,,			UInt8Vec),
  (unsignedValues,, UInt16Vec), (blobs,,	BytesVec),
  (ratios,, FloatVec), (precisions,,		FloatVec));
Zpb::AsMessage<"demo.NativeScalarRow"> Zpb_Fmt(NativeScalarRow *);
ZuDerive(NativeBoolValues,
  (ZtArray<uint8_t, ZtArrayHeapID<"zpbinterop.BoolValues">>));
struct NativeBoolRow { NativeBoolValues values; };
ZfStruct(, (NativeBoolRow, PB),
  (values, (PB::BoolVec), UInt8Vec));
Zpb::AsMessage<"demo.NativeBoolRow"> Zpb_Fmt(NativeBoolRow *);
struct NativePackedBoolRow { NativeBoolValues values; };
ZfStruct(, (NativePackedBoolRow, PB),
  (values, (PB::BoolVec, PB::Packed), UInt8Vec));
Zpb::AsMessage<"demo.NativePackedBoolRow">
  Zpb_Fmt(NativePackedBoolRow *);
ZuDerive(NativeCStringValues,
  (ZtArray<char *, ZtArrayHeapID<"zpbinterop.CStringValues">>));
struct NativeCStringRow {
  char *text = nullptr;
  NativeCStringValues values;
  NativeCStringRow() = default;
  NativeCStringRow(NativeCStringRow &&o) :
    text{o.text}, values{ZuMv(o.values)} { o.text = nullptr; }
  ~NativeCStringRow() {
    Zpb::CStringArg::free(text);
    for (auto value : values) Zpb::CStringArg::free(value);
  }
};
ZfStruct(, (NativeCStringRow, PB),
  (text,, CString), (values,, CStringVec));
Zpb::AsMessage<"demo.NativeCStringRow"> Zpb_Fmt(NativeCStringRow *);
ZuDerive(NativeWire32,
  (ZtArray<int32_t, ZtArrayHeapID<"zpbinterop.Wire32">>));
ZuDerive(NativeUWire32,
  (ZtArray<uint32_t, ZtArrayHeapID<"zpbinterop.UWire32">>));
ZuDerive(NativeWire64,
  (ZtArray<int64_t, ZtArrayHeapID<"zpbinterop.Wire64">>));
ZuDerive(NativeUWire64,
  (ZtArray<uint64_t, ZtArrayHeapID<"zpbinterop.UWire64">>));
struct NativeWireRow {
  int32_t signed32 = 0;
  int64_t signed64 = 0;
  uint32_t unsigned32 = 0;
  uint64_t unsigned64 = 0;
  int32_t fixedSigned32 = 0;
  int64_t fixedSigned64 = 0;
  NativeWire32 zig32;
  NativeWire64 zig64;
  NativeWire32 fixed32;
  NativeUWire64 fixed64;
  NativeUWire32 fixedUnsigned32;
  NativeWire64 fixedSigned64s;
  int32_t plainSigned32 = 0;
  int64_t plainSigned64 = 0;
  uint32_t plainUnsigned32 = 0;
  uint64_t plainUnsigned64 = 0;
  NativeWire32 plain32s;
  NativeWire64 plain64s;
  NativeUWire32 plainU32s;
  NativeUWire64 plainU64s;
};
ZfStruct(, (NativeWireRow, PB),
  (signed32, (PB::ZigZag),				Int32),
  (signed64, (PB::ZigZag),				Int64),
  (unsigned32, (PB::Fixed),				UInt32),
  (unsigned64, (PB::Fixed),				UInt64),
  (fixedSigned32, (PB::Fixed),				Int32),
  (fixedSigned64, (PB::Fixed),				Int64),
  (zig32, (PB::ZigZag),					Int32Vec),
  (zig64, (PB::ZigZag),					Int64Vec),
  (fixed32, (PB::Fixed),				Int32Vec),
  (fixed64, (PB::Fixed),				UInt64Vec),
  (fixedUnsigned32, (PB::Fixed),			UInt32Vec),
  (fixedSigned64s, (PB::Fixed),				Int64Vec),
  (plainSigned32,, Int32), (plainSigned64,,		Int64),
  (plainUnsigned32,, UInt32), (plainUnsigned64,,	UInt64),
  (plain32s,, Int32Vec), (plain64s,,			Int64Vec),
  (plainU32s,, UInt32Vec), (plainU64s,,			UInt64Vec));
Zpb::AsMessage<"demo.NativeWireRow"> Zpb_Fmt(NativeWireRow *);

static bool parse(const ProtoText &text, ZuCSpan name,
  google::protobuf::FileDescriptorProto &out)
{
  google::protobuf::io::ArrayInputStream input{text.data(),
    int(text.length())};
  google::protobuf::io::Tokenizer tokenizer{&input, nullptr};
  google::protobuf::compiler::Parser parser;
  if (!parser.Parse(&tokenizer, &out)) return false;
  out.set_name(name.data(), name.length());
  return true;
}

static bool readProto(ProtoText &out, ZuCSpan path)
{
  ZiFile file{Zi::Path{path}, ZiFile::ReadOnly | ZiFile::GC};
  if (!file) return false;
  auto stat = file.fstat();
  if (!stat || stat.size() < 0 || uint64_t(stat.size()) > INT_MAX)
    return false;
  out.length(stat.size());
  return !stat.size() || file.read(out.data(), stat.size()) == stat.size();
}

static bool readProto(ProtoText &out)
{
  return readProto(out, ZPB_INTEROP_PROTO);
}

template <typename T>
static bool nativeProto(ProtoText &out, ZuCSpan fileName, Syntax::T syntax)
{
  DescSet descriptor;
  if (!exportSchema<T>(descriptor, fileName, "demo", syntax))
    return false;
  Output output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto bytes = Zpb::save(builder, descriptor);
  if (builder.failed() || !verifySchema(bytes)) return false;
  Schema schema{bytes};
  auto file = schema.file(fileName);
  return file && emitProto(out, schema, *file, syntax);
}

static void testProto3Optional()
{
  ZuTestScope(testProto3Optional);
  ProtoText source;
  source << "syntax = \"proto3\"; package demo; "
    "message OptRow { message Payload { uint32 id = 1; } "
    "optional uint32 value = 1; optional bool active = 2; "
    "oneof pick { Payload chosen = 3; } "
    "optional string label = 4; }";
  google::protobuf::FileDescriptorProto proto;
  ZuCheck(parse(source, "optional.proto", proto));
  if (proto.message_type_size() != 1) return;
  const auto &decl = proto.message_type(0);
  ZuCheck(decl.field_size() == 4 && decl.oneof_decl_size() == 4 &&
    decl.oneof_decl(0).name() == "pick" &&
    decl.field(0).proto3_optional() && decl.field(0).oneof_index() == 1 &&
    decl.field(1).proto3_optional() && decl.field(1).oneof_index() == 2 &&
    !decl.field(2).proto3_optional() && decl.field(2).oneof_index() == 0 &&
    decl.field(3).proto3_optional() && decl.field(3).oneof_index() == 3);
  google::protobuf::DescriptorPool pool;
  auto file = pool.BuildFile(proto);
  ZuCheck(file);
  if (!file) return;
  google::protobuf::FileDescriptorSet set;
  file->CopyTo(set.add_file());
  std::string descriptorBytes;
  bool imported = set.SerializeToString(&descriptorBytes) &&
    verifySchema({reinterpret_cast<const uint8_t *>(descriptorBytes.data()),
      unsigned(descriptorBytes.length())});
  ZuCheck(imported);
  if (!imported) return;
  Schema schema{{reinterpret_cast<const uint8_t *>(descriptorBytes.data()),
    unsigned(descriptorBytes.length())}};
  auto reflected = schema.message(".demo.OptRow");
  ZuCheck(reflected && reflected->oneofs.length() == 4 &&
    reflected->fields.length() == 4 &&
    reflected->oneofs[0].name == "pick" &&
    reflected->fields[0].proto3Optional &&
    reflected->fields[1].proto3Optional &&
    !reflected->fields[2].proto3Optional &&
    reflected->fields[3].proto3Optional);
  if (!reflected) return;
  ProtoText emitted;
  auto importedFile = schema.file("optional.proto");
  ZuCheck(importedFile && emitProto(emitted, schema, *importedFile,
      fileSyntax(*importedFile)) &&
    strstr(emitted.data(), "optional uint32 value = 1;") &&
    strstr(emitted.data(), "optional bool active = 2;") &&
    strstr(emitted.data(), "optional string label = 4;") &&
    strstr(emitted.data(), "oneof pick") &&
    !strstr(emitted.data(), "oneof _"));
  google::protobuf::FileDescriptorProto roundTrip;
  bool parsed = parse(emitted, "optional-roundtrip.proto", roundTrip);
  ZuCheck(parsed);
  if (parsed) {
    const auto &again = roundTrip.message_type(0);
    ZuCheck(again.oneof_decl_size() == 4 && again.field_size() == 4);
    bool fieldsMatch = again.field_size() == 4;
    for (const auto &field : again.field())
      fieldsMatch &= field.proto3_optional() ==
	(field.name() != "chosen");
    ZuCheck(fieldsMatch);
    google::protobuf::DescriptorPool roundTripPool;
    ZuCheck(roundTripPool.BuildFile(roundTrip));
  }

  auto type = file->FindMessageTypeByName("OptRow");
  google::protobuf::DynamicMessageFactory factory;
  auto message = factory.GetPrototype(type)->New();
  auto value = type->FindFieldByName("value");
  auto active = type->FindFieldByName("active");
  auto label = type->FindFieldByName("label");
  auto fields = message->GetReflection();
  ZuCheck(!fields->HasField(*message, value) &&
    !fields->HasField(*message, active) &&
    !fields->HasField(*message, label) &&
    verify(schema, *reflected, {}));
  fields->SetUInt32(message, value, 0);
  fields->SetBool(message, active, false);
  fields->SetString(message, label, "");
  std::string wire;
  bool present = fields->HasField(*message, value) &&
    fields->HasField(*message, active) &&
    fields->HasField(*message, label) &&
    message->SerializeToString(&wire);
  ZuCheck(present);
  if (present) {
    ZuBSpan bytes{reinterpret_cast<const uint8_t *>(wire.data()),
      unsigned(wire.length())};
    ZuCheck(verify(schema, *reflected, bytes) && wire.length() == 6);
  }
  delete message;
}

static void testRecursive()
{
  ZuTestScope(testRecursive);
  ProtoText text;
  ZuCheck(nativeProto<NativeNode>(text, "recursive.proto", Syntax::Proto3));
  if (!text.length()) return;
  google::protobuf::FileDescriptorProto proto;
  ZuCheck(parse(text, "recursive.proto", proto));
  google::protobuf::DescriptorPool pool;
  auto file = pool.BuildFile(proto);
  ZuCheck(file);
  if (!file) return;
  auto type = file->FindMessageTypeByName("NativeNode");
  ZuCheck(type && type->field_count() == 2 &&
    type->field(1)->message_type() == type);
  if (!type || type->field_count() != 2) return;
  google::protobuf::FileDescriptorSet set;
  file->CopyTo(set.add_file());
  std::string descriptorBytes;
  bool imported = set.SerializeToString(&descriptorBytes) &&
    verifySchema({reinterpret_cast<const uint8_t *>(descriptorBytes.data()),
      unsigned(descriptorBytes.length())});
  ZuCheck(imported);
  if (!imported) return;
  Schema schema{{reinterpret_cast<const uint8_t *>(descriptorBytes.data()),
    unsigned(descriptorBytes.length())}};
  auto reflected = schema.message(".demo.NativeNode");
  ZuCheck(reflected && schema.field(*reflected, "next") &&
    schema.field(*reflected, "next")->typeName == ".demo.NativeNode");
  if (!reflected) return;

  NativeNode native;
  native.value = 1;
  native.next = new NativeNode;
  native.next->value = 2;
  Output output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto wire = Zpb::save(builder, native);
  ZuCheck(!builder.failed() && Zpb::verify<NativeNode>(wire) &&
    verify(schema, *reflected, wire));
  google::protobuf::DynamicMessageFactory factory;
  auto message = factory.GetPrototype(type)->New();
  bool canonicalRead = message->ParseFromArray(wire.data(), wire.length());
  ZuCheck(canonicalRead);
  if (canonicalRead) {
    auto fields = message->GetReflection();
    const auto &child = fields->GetMessage(*message, type->field(1));
    ZuCheck(fields->GetUInt32(*message, type->field(0)) == 1 &&
      child.GetReflection()->GetUInt32(child, type->field(0)) == 2 &&
      !child.GetReflection()->HasField(child, type->field(1)));
  }

  auto valueField = schema.field(*reflected, "value");
  auto nextField = schema.field(*reflected, "next");
  ZuCheck(valueField && nextField);
  if (valueField && nextField) {
    Value childValue{valueField, 6, 0, {}};
    Value parentValue{valueField, 5, 0, {}};
    Output reflectedOutput;
    Zpb::ArrayStorage reflectedStorage{reflectedOutput};
    Zpb::Builder reflectedBuilder{reflectedStorage};
    bool written = writeNested(reflectedBuilder, schema, *reflected, *nextField,
      ZuSpan<const Value>{&childValue, 1}) &&
      write(reflectedBuilder, *reflected,
	ZuSpan<const Value>{&parentValue, 1});
    ZuCheck(written && verify(schema, *reflected, reflectedBuilder.bytes()));
    message->Clear();
    bool parsed = written && message->ParseFromArray(
      reflectedBuilder.bytes().data(), reflectedBuilder.bytes().length());
    ZuCheck(parsed);
    if (parsed) {
      auto fields = message->GetReflection();
      const auto &child = fields->GetMessage(*message, type->field(1));
      ZuCheck(fields->GetUInt32(*message, type->field(0)) == 5 &&
	child.GetReflection()->GetUInt32(child, type->field(0)) == 6);
    }
  }

  message->Clear();
  auto fields = message->GetReflection();
  fields->SetUInt32(message, type->field(0), 3);
  auto child = fields->MutableMessage(message, type->field(1));
  child->GetReflection()->SetUInt32(child, type->field(0), 4);
  std::string canonicalWire;
  bool canonicalWrite = message->SerializeToString(&canonicalWire);
  ZuCheck(canonicalWrite);
  if (canonicalWrite) {
    ZuBSpan bytes{reinterpret_cast<const uint8_t *>(canonicalWire.data()),
      unsigned(canonicalWire.length())};
    ZuCheck(Zpb::verify<NativeNode>(bytes, {1}) &&
      verify(schema, *reflected, bytes, {1}) &&
      !Zpb::verify<NativeNode>(bytes, {0}) &&
      !verify(schema, *reflected, bytes, {0}));
    auto loaded = Zpb::load<NativeNode>(bytes);
    ZuCheck(loaded.value == 3 && loaded.next &&
      loaded.next->value == 4 && !loaded.next->next);
  }
  delete message;
}

static void testRequiredSyntax()
{
  ZuTestScope(testRequiredSyntax);
  ProtoText proto2Text, proto3Text;
  bool exported = nativeProto<NativeRequired>(proto2Text, "required2.proto",
    Syntax::Proto2) && nativeProto<NativeRequired>(proto3Text,
      "required3.proto", Syntax::Proto3);
  ZuCheck(exported);
  if (!exported) return;
  google::protobuf::FileDescriptorProto proto2, proto3;
  bool parsed = parse(proto2Text, "required2.proto", proto2) &&
    parse(proto3Text, "required3.proto", proto3);
  ZuCheck(parsed);
  if (!parsed) return;
  google::protobuf::DescriptorPool pool2, pool3;
  auto file2 = pool2.BuildFile(proto2);
  auto file3 = pool3.BuildFile(proto3);
  ZuCheck(file2 && file3);
  if (!file2 || !file3) return;
  auto type2 = file2->FindMessageTypeByName("NativeRequired");
  auto type3 = file3->FindMessageTypeByName("NativeRequired");
  bool labels = type2 && type3 && type2->field_count() == 1 &&
    type3->field_count() == 1 && type2->field(0)->is_required() &&
    !type3->field(0)->is_required();
  ZuCheck(labels);
  if (!labels) return;
  google::protobuf::FileDescriptorSet set2, set3;
  file2->CopyTo(set2.add_file());
  file3->CopyTo(set3.add_file());
  std::string bytes2, bytes3;
  bool imported = set2.SerializeToString(&bytes2) &&
    set3.SerializeToString(&bytes3) &&
    verifySchema({reinterpret_cast<const uint8_t *>(bytes2.data()),
      unsigned(bytes2.length())}) &&
    verifySchema({reinterpret_cast<const uint8_t *>(bytes3.data()),
      unsigned(bytes3.length())});
  ZuCheck(imported);
  if (!imported) return;
  Schema schema2{{reinterpret_cast<const uint8_t *>(bytes2.data()),
    unsigned(bytes2.length())}};
  Schema schema3{{reinterpret_cast<const uint8_t *>(bytes3.data()),
    unsigned(bytes3.length())}};
  auto reflected2 = schema2.message(".demo.NativeRequired");
  auto reflected3 = schema3.message(".demo.NativeRequired");
  ZuCheck(reflected2 && reflected3 &&
    reflected2->fields[0].label == 2 && reflected3->fields[0].label == 1);
  if (!reflected2 || !reflected3) return;
  google::protobuf::DynamicMessageFactory factory;
  auto message2 = factory.GetPrototype(type2)->New();
  auto message3 = factory.GetPrototype(type3)->New();
  ZuCheck(!message2->IsInitialized() && message3->IsInitialized() &&
    !Zpb::verify<NativeRequired>({}) &&
    !verify(schema2, *reflected2, {}) &&
    verify(schema3, *reflected3, {}));
  message2->GetReflection()->SetUInt32(message2, type2->field(0), 7);
  std::string wire;
  bool written = message2->SerializeToString(&wire);
  ZuCheck(written);
  if (written) {
    ZuBSpan bytes{reinterpret_cast<const uint8_t *>(wire.data()),
      unsigned(wire.length())};
    ZuCheck(message3->ParseFromString(wire) &&
      message3->GetReflection()->GetUInt32(*message3,
	type3->field(0)) == 7 &&
      Zpb::verify<NativeRequired>(bytes) &&
      verify(schema2, *reflected2, bytes) &&
      verify(schema3, *reflected3, bytes) &&
      Zpb::load<NativeRequired>(bytes).id == 7);
  }
  delete message2;
  delete message3;
}

static void testCompatibility()
{
  ZuTestScope(testCompatibility);
  ProtoText oldText, newText;
  bool exported = nativeProto<CompatOld>(oldText, "compat-old.proto",
    Syntax::Proto3) && nativeProto<CompatNew>(newText, "compat-new.proto",
      Syntax::Proto3);
  ZuCheck(exported);
  if (!exported) return;
  google::protobuf::FileDescriptorProto oldProto, newProto;
  bool parsed = parse(oldText, "compat-old.proto", oldProto) &&
    parse(newText, "compat-new.proto", newProto);
  ZuCheck(parsed);
  if (!parsed) return;
  google::protobuf::DescriptorPool oldPool, newPool;
  auto oldFile = oldPool.BuildFile(oldProto);
  auto newFile = newPool.BuildFile(newProto);
  ZuCheck(oldFile && newFile);
  if (!oldFile || !newFile) return;
  auto oldType = oldFile->FindMessageTypeByName("Compat");
  auto newType = newFile->FindMessageTypeByName("Compat");
  bool shapes = oldType && newType && oldType->field_count() == 1 &&
    newType->field_count() == 3 &&
    oldType->field(0)->number() == newType->field(0)->number();
  ZuCheck(shapes);
  if (!shapes) return;
  google::protobuf::FileDescriptorSet oldSet, newSet;
  oldFile->CopyTo(oldSet.add_file());
  newFile->CopyTo(newSet.add_file());
  std::string oldDescBytes, newDescBytes;
  bool imported = oldSet.SerializeToString(&oldDescBytes) &&
    newSet.SerializeToString(&newDescBytes) &&
    verifySchema({reinterpret_cast<const uint8_t *>(oldDescBytes.data()),
      unsigned(oldDescBytes.length())}) &&
    verifySchema({reinterpret_cast<const uint8_t *>(newDescBytes.data()),
      unsigned(newDescBytes.length())});
  ZuCheck(imported);
  if (!imported) return;
  Schema oldSchema{{reinterpret_cast<const uint8_t *>(oldDescBytes.data()),
    unsigned(oldDescBytes.length())}};
  Schema newSchema{{reinterpret_cast<const uint8_t *>(newDescBytes.data()),
    unsigned(newDescBytes.length())}};
  auto oldRef = oldSchema.message(".demo.Compat");
  auto newRef = newSchema.message(".demo.Compat");
  ZuCheck(oldRef && newRef);
  if (!oldRef || !newRef) return;
  google::protobuf::DynamicMessageFactory factory;
  auto oldMessage = factory.GetPrototype(oldType)->New();
  auto newMessage = factory.GetPrototype(newType)->New();

  CompatOld oldNative{17};
  Output oldOutput;
  Zpb::ArrayStorage oldStorage{oldOutput};
  Zpb::Builder oldBuilder{oldStorage};
  auto oldWire = Zpb::save(oldBuilder, oldNative);
  bool oldRead = !oldBuilder.failed() &&
    newMessage->ParseFromArray(oldWire.data(), oldWire.length());
  ZuCheck(oldRead);
  ZuCheck(verify(oldSchema, *oldRef, oldWire) &&
    verify(newSchema, *newRef, oldWire));
  if (oldRead) {
    auto fields = newMessage->GetReflection();
    ZuCheck(fields->GetUInt32(*newMessage, newType->field(0)) == 17 &&
      fields->GetString(*newMessage, newType->field(1)).empty() &&
      fields->GetUInt32(*newMessage, newType->field(2)) == 0);
  }

  newMessage->Clear();
  auto newFields = newMessage->GetReflection();
  newFields->SetUInt32(newMessage, newType->field(0), 29);
  newFields->SetString(newMessage, newType->field(1), "new");
  newFields->SetUInt32(newMessage, newType->field(2), 4);
  std::string newWire;
  bool newWritten = newMessage->SerializeToString(&newWire);
  ZuCheck(newWritten);
  if (newWritten) {
    ZuBSpan bytes{reinterpret_cast<const uint8_t *>(newWire.data()),
      unsigned(newWire.length())};
    ZuCheck(Zpb::verify<CompatOld>(bytes) &&
      Zpb::verify<CompatNew>(bytes) &&
      verify(oldSchema, *oldRef, bytes) &&
      verify(newSchema, *newRef, bytes));
    auto old = Zpb::load<CompatOld>(bytes);
    auto current = Zpb::load<CompatNew>(bytes);
    ZuCheck(old.id == 29 && current.id == 29 &&
      current.label == "new" && current.revision == 4);
    Output droppedOutput;
    Zpb::ArrayStorage droppedStorage{droppedOutput};
    Zpb::Builder droppedBuilder{droppedStorage};
    auto dropped = Zpb::save(droppedBuilder, old);
    oldMessage->Clear();
    ZuCheck(!droppedBuilder.failed() &&
      oldMessage->ParseFromArray(dropped.data(), dropped.length()) &&
      oldMessage->GetReflection()->GetUInt32(*oldMessage,
	oldType->field(0)) == 29 &&
      oldMessage->GetReflection()->GetUnknownFields(*oldMessage)
	.field_count() == 0);
    newMessage->Clear();
    ZuCheck(newMessage->ParseFromArray(dropped.data(), dropped.length()) &&
      newMessage->GetReflection()->GetString(*newMessage,
	newType->field(1)).empty() &&
      newMessage->GetReflection()->GetUInt32(*newMessage,
	newType->field(2)) == 0);
  }

  CompatNew current{41, "native", 5};
  Output newOutput;
  Zpb::ArrayStorage newStorage{newOutput};
  Zpb::Builder newBuilder{newStorage};
  auto nativeWire = Zpb::save(newBuilder, current);
  oldMessage->Clear();
  ZuCheck(!newBuilder.failed() &&
    verify(oldSchema, *oldRef, nativeWire) &&
    verify(newSchema, *newRef, nativeWire) &&
    oldMessage->ParseFromArray(nativeWire.data(), nativeWire.length()) &&
    oldMessage->GetReflection()->GetUInt32(*oldMessage,
      oldType->field(0)) == 41 &&
    oldMessage->GetReflection()->GetUnknownFields(*oldMessage)
      .field_count() == 2);
  delete oldMessage;
  delete newMessage;
}

static void testInterop()
{
  ZuTestScope(testInterop);
  FileDesc base;
  base.name = "base.proto";
  base.package = "demo";
  base.syntax = "proto3";
  MsgDesc item;
  item.name = "Item";
  item.fields.push(FieldDesc{"id", {}, {}, 1, 1, Type::UInt32});
  base.messages.push(ZuMv(item));

  FileDesc user;
  user.name = "user.proto";
  user.package = "demo";
  user.syntax = "proto3";
  user.dependencies.push("base.proto");
  MsgDesc row;
  row.name = "Row";
  row.fields.push(FieldDesc{"entries", ".demo.Row.EntriesEntry", {},
    1, 3, Type::Message});
  ZuPtr<MsgDesc> entry{new MsgDesc{}};
  entry->name = "EntriesEntry";
  entry->options = new MsgOpts{true};
  entry->fields.push(FieldDesc{"key", {}, {}, 1, 1, Type::UInt32});
  entry->fields.push(FieldDesc{"value", {}, {}, 2, 1, Type::String});
  row.nested.push(ZuMv(entry));
  row.oneofs.push(OneofDesc{"choice"});
  FieldDesc first{"first", ".demo.Item", {}, 2, 1, Type::Message};
  first.oneofIndex = 0;
  FieldDesc second{"second", ".demo.Item", {}, 3, 1, Type::Message};
  second.oneofIndex = 0;
  row.fields.push(first);
  row.fields.push(second);
  user.messages.push(ZuMv(row));

  DescSet descriptor;
  descriptor.files.push(ZuMv(base));
  descriptor.files.push(ZuMv(user));
  Output output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto bytes = Zpb::save(builder, descriptor);
  bool valid = !builder.failed() && verifySchema(bytes);
  ZuCheck(valid);
  if (!valid) return;
  Schema schema{bytes};
  ProtoText baseText, userText;
  auto baseFile = schema.file("base.proto");
  auto userFile = schema.file("user.proto");
  bool emitted = baseFile && userFile &&
    emitProto(baseText, schema, *baseFile,
      fileSyntax(*baseFile)) &&
    emitProto(userText, schema, *userFile,
      fileSyntax(*userFile));
  ZuCheck(emitted);
  if (!emitted) return;

  google::protobuf::FileDescriptorProto baseProto, userProto;
  ZuCheck(parse(baseText, baseFile->name, baseProto) &&
    parse(userText, userFile->name, userProto));
  google::protobuf::DescriptorPool pool;
  auto baseCanonical = pool.BuildFile(baseProto);
  auto userCanonical = pool.BuildFile(userProto);
  ZuCheck(baseCanonical && userCanonical);
  if (!baseCanonical || !userCanonical) return;
  google::protobuf::FileDescriptorSet canonicalSet;
  baseCanonical->CopyTo(canonicalSet.add_file());
  userCanonical->CopyTo(canonicalSet.add_file());
  std::string canonicalBytes;
  ZuCheck(canonicalSet.SerializeToString(&canonicalBytes) &&
    verifySchema({reinterpret_cast<const uint8_t *>(canonicalBytes.data()),
      unsigned(canonicalBytes.length())}));
  Schema imported{{reinterpret_cast<const uint8_t *>(canonicalBytes.data()),
    unsigned(canonicalBytes.length())}};
  auto canonicalRow = userCanonical->FindMessageTypeByName("Row");
  ZuCheck(canonicalRow && canonicalRow->field_count() == 3 &&
    canonicalRow->field(0)->is_map() &&
    canonicalRow->oneof_decl_count() == 1 &&
    canonicalRow->field(2)->containing_oneof() ==
      canonicalRow->oneof_decl(0));
  if (!canonicalRow) return;

  const uint8_t payload[] = {
    0x0a, 5, 0x08, 7, 0x12, 1, 'a',
    0x1a, 2, 0x08, 42};
  auto nativeRow = schema.message(".demo.Row");
  ZuCheck(nativeRow && verify(schema, *nativeRow,
    {payload, sizeof(payload)}));
  auto importedRow = imported.message(".demo.Row");
  ZuCheck(importedRow && verify(imported, *importedRow,
    {payload, sizeof(payload)}));
  if (!nativeRow || !importedRow) return;
  google::protobuf::DynamicMessageFactory factory;
  auto prototype = factory.GetPrototype(canonicalRow);
  auto message = prototype->New();
  ZuCheck(message->ParseFromArray(payload, sizeof(payload)));
  auto reflection = message->GetReflection();
  auto mapField = canonicalRow->field(0);
  const auto &mapEntry = reflection->GetRepeatedMessage(*message, mapField, 0);
  auto mapReflection = mapEntry.GetReflection();
  auto mapType = mapEntry.GetDescriptor();
  ZuCheck(mapReflection->GetUInt32(mapEntry,
      mapType->FindFieldByName("key")) == 7 &&
    mapReflection->GetString(mapEntry,
      mapType->FindFieldByName("value")) == "a" &&
    reflection->GetOneofFieldDescriptor(*message,
      canonicalRow->oneof_decl(0)) == canonicalRow->field(2));
  ZuCheck(reflection->FieldSize(*message, canonicalRow->field(0)) == 1 &&
    reflection->GetMessage(*message, canonicalRow->field(2))
      .GetReflection()->GetUInt32(
	reflection->GetMessage(*message, canonicalRow->field(2)),
	baseCanonical->FindMessageTypeByName("Item")->field(0)) == 42);
  std::string canonicalWire;
  ZuCheck(message->SerializeToString(&canonicalWire) &&
    verify(schema, *nativeRow,
      {reinterpret_cast<const uint8_t *>(canonicalWire.data()),
	unsigned(canonicalWire.length())}));
  delete message;
}

static void testNativeExport()
{
  ZuTestScope(testNativeExport);
  DescSet descriptor;
  ZuCheck(exportSchema<NativeRow>(descriptor, "native.proto", "demo",
    Syntax::Proto3));
  Output output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto bytes = Zpb::save(builder, descriptor);
  ZuCheck(!builder.failed() && verifySchema(bytes));
  if (builder.failed()) return;
  Schema schema{bytes};
  auto file = schema.file("native.proto");
  ProtoText proto;
  ZuCheck(file && emitProto(proto, schema, *file,
      fileSyntax(*file)));
  if (!file) return;
  google::protobuf::FileDescriptorProto canonical;
  ZuCheck(parse(proto, file->name, canonical));
  google::protobuf::DescriptorPool pool;
  auto built = pool.BuildFile(canonical);
  ZuCheck(built);
  if (!built) return;
  auto row = built->FindMessageTypeByName("NativeRow");
  auto idField = row ? row->FindFieldByName("id") : nullptr;
  auto entryField = row ? row->FindFieldByName("entries") : nullptr;
  auto choiceField = row ? row->FindFieldByName("selected") : nullptr;
  auto blobField = row ? row->FindFieldByName("blobs") : nullptr;
  ZuCheck(row && row->field_count() == 4 &&
    idField && idField->number() == 1 &&
    entryField && entryField->number() == 2 && entryField->is_map() &&
    choiceField && choiceField->number() == 5 &&
    choiceField->containing_oneof() &&
    blobField && blobField->number() == 4 && blobField->is_map());
  if (!row || !idField || !entryField || !choiceField || !blobField) return;
  NativeRow native;
  native.id = 17;
  native.entries.add(7, 33);
  NativeItem selected{42};
  native.choice.p<1>(ZuMv(selected));
  NativeBlob nativeBlob;
  const uint8_t nativeBlobBytes[] = {'b', 0, 'y'};
  for (auto byte : nativeBlobBytes) nativeBlob.push(byte);
  native.blobs.add(9, ZuMv(nativeBlob));
  Output nativeOutput;
  Zpb::ArrayStorage nativeStorage{nativeOutput};
  Zpb::Builder nativeBuilder{nativeStorage};
  auto nativeWire = Zpb::save(nativeBuilder, native);
  ZuCheck(!nativeBuilder.failed() && Zpb::verify<NativeRow>(nativeWire));
  google::protobuf::DynamicMessageFactory factory;
  auto canonicalMsg = factory.GetPrototype(row)->New();
  bool parsed = canonicalMsg->ParseFromArray(nativeWire.data(),
    nativeWire.length());
  ZuCheck(parsed);
  if (!parsed) { delete canonicalMsg; return; }
  auto fields = canonicalMsg->GetReflection();
  int entries = fields->FieldSize(*canonicalMsg, entryField);
  int blobs = fields->FieldSize(*canonicalMsg, blobField);
  ZuCheck(entries == 1 && blobs == 1);
  if (entries != 1 || blobs != 1) {
    delete canonicalMsg;
    return;
  }
  const auto &entry = fields->GetRepeatedMessage(*canonicalMsg, entryField, 0);
  auto entryType = entry.GetDescriptor();
  auto entryFields = entry.GetReflection();
  auto itemType = built->FindMessageTypeByName("NativeItem");
  ZuCheck(itemType);
  if (!itemType) { delete canonicalMsg; return; }
  const auto &choice = fields->GetMessage(*canonicalMsg, choiceField);
  const auto &blobEntry = fields->GetRepeatedMessage(*canonicalMsg,
    blobField, 0);
  auto blobType = blobEntry.GetDescriptor();
  auto blobFields = blobEntry.GetReflection();
  ZuCheck(fields->GetUInt32(*canonicalMsg, idField) == 17 &&
    entryFields->GetInt32(entry, entryType->FindFieldByName("key")) == 7 &&
    entryFields->GetInt32(entry, entryType->FindFieldByName("value")) == 33 &&
    fields->GetOneofFieldDescriptor(*canonicalMsg, row->oneof_decl(0)) ==
      choiceField &&
    choice.GetReflection()->GetUInt32(choice, itemType->field(0)) == 42 &&
    blobFields->GetUInt32(blobEntry,
      blobType->FindFieldByName("key")) == 9 &&
    blobFields->GetString(blobEntry,
      blobType->FindFieldByName("value")) == std::string("b\0y", 3));
  canonicalMsg->Clear();
  fields->SetUInt32(canonicalMsg, idField, 29);
  auto added = fields->AddMessage(canonicalMsg, entryField);
  auto addedFields = added->GetReflection();
  addedFields->SetInt32(added, entryType->FindFieldByName("key"), 7);
  addedFields->SetInt32(added, entryType->FindFieldByName("value"), 44);
  auto replacement = fields->AddMessage(canonicalMsg, entryField);
  auto replacementFields = replacement->GetReflection();
  replacementFields->SetInt32(replacement,
    entryType->FindFieldByName("key"), 7);
  replacementFields->SetInt32(replacement,
    entryType->FindFieldByName("value"), 55);
  ZuCheck(fields->FieldSize(*canonicalMsg, entryField) == 2);
  auto addedBlob = fields->AddMessage(canonicalMsg, blobField);
  auto addedBlobType = addedBlob->GetDescriptor();
  auto addedBlobFields = addedBlob->GetReflection();
  addedBlobFields->SetUInt32(addedBlob,
    addedBlobType->FindFieldByName("key"), 10);
  addedBlobFields->SetString(addedBlob,
    addedBlobType->FindFieldByName("value"), std::string("x\0z", 3));
  auto selectedItem = fields->MutableMessage(canonicalMsg, choiceField);
  selectedItem->GetReflection()->SetUInt32(selectedItem, itemType->field(0), 53);
  std::string canonicalWire;
  bool validWire = canonicalMsg->SerializeToString(&canonicalWire) &&
    Zpb::verify<NativeRow>({
      reinterpret_cast<const uint8_t *>(canonicalWire.data()),
      unsigned(canonicalWire.length())});
  ZuCheck(validWire);
  if (!validWire) { delete canonicalMsg; return; }
  auto reflectedRow = schema.message(".demo.NativeRow");
  ZuCheck(reflectedRow && verify(schema, *reflectedRow,
    {reinterpret_cast<const uint8_t *>(canonicalWire.data()),
      unsigned(canonicalWire.length())}));
  canonicalWire.append("\x98\x06\x07", 3); // unknown field 99
  ZuCheck(Zpb::verify<NativeRow>({
    reinterpret_cast<const uint8_t *>(canonicalWire.data()),
    unsigned(canonicalWire.length())}));
  auto loaded = Zpb::load<NativeRow>({
    reinterpret_cast<const uint8_t *>(canonicalWire.data()),
    unsigned(canonicalWire.length())});
  auto loadedBlob = loaded.blobs.find(10);
  ZuCheck(loaded.id == 29 && loaded.entries.count_() == 1 &&
    loaded.entries.findVal(7) == 55 && loaded.choice.type() == 1 &&
    loaded.choice.p<1>().id == 53 && loaded.blobs.count_() == 1 &&
    loadedBlob && loadedBlob->val().length() == 3 &&
    !memcmp(loadedBlob->val().data(), "x\0z", 3));
  Output droppedOutput;
  Zpb::ArrayStorage droppedStorage{droppedOutput};
  Zpb::Builder droppedBuilder{droppedStorage};
  auto droppedWire = Zpb::save(droppedBuilder, loaded);
  canonicalMsg->Clear();
  ZuCheck(!droppedBuilder.failed() &&
    canonicalMsg->ParseFromArray(droppedWire.data(), droppedWire.length()) &&
    canonicalMsg->GetReflection()->GetUnknownFields(*canonicalMsg)
      .field_count() == 0);
  delete canonicalMsg;
}

static void testFacetInterop()
{
  ZuTestScope(testFacetInterop);
  DescSet descriptor;
  ZuCheck((exportSchema<FacetRow, ZuFacet::InteropFacet>(descriptor,
    "facet.proto", "demo", Syntax::Proto2)));
  Output output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto bytes = Zpb::save(builder, descriptor);
  ZuCheck(!builder.failed() && verifySchema(bytes));
  if (builder.failed()) return;
  Schema schema{bytes};
  auto file = schema.file("facet.proto", Syntax::Proto2);
  ProtoText proto;
  ZuCheck(file && emitProto(proto, schema, *file, Syntax::Proto2));
  if (!file) return;
  google::protobuf::FileDescriptorProto canonical;
  ZuCheck(parse(proto, file->name, canonical));
  google::protobuf::DescriptorPool pool;
  auto built = pool.BuildFile(canonical);
  ZuCheck(built);
  if (!built) return;
  auto row = built->FindMessageTypeByName("FacetRow");
  auto first = row ? row->FindFieldByName("first") : nullptr;
  auto renamed = row ? row->FindFieldByName("renamed") : nullptr;
  auto hidden = row ? row->FindFieldByName("hidden") : nullptr;
  ZuCheck(row && row->field_count() == 3 &&
    row->reserved_range_count() == 1 &&
    row->reserved_range(0)->start == 2 &&
    first && first->number() == 1 &&
    renamed && renamed->number() == 3 &&
    hidden && hidden->number() == 4);
  if (!first || !renamed || !hidden) return;
  FacetRow native;
  native.first = 11;
  native.last = 23;
  native.hidden = 47;
  Output nativeOutput;
  Zpb::ArrayStorage nativeStorage{nativeOutput};
  Zpb::Builder nativeBuilder{nativeStorage};
  auto nativeWire = Zpb::save<ZuFacet::InteropFacet>(nativeBuilder, native);
  ZuCheck((!nativeBuilder.failed() &&
    Zpb::verify<FacetRow, ZuFacet::InteropFacet>(nativeWire)));
  google::protobuf::DynamicMessageFactory factory;
  auto message = factory.GetPrototype(row)->New();
  auto reflection = message->GetReflection();
  ZuCheck(renamed->has_default_value() &&
    renamed->default_value_uint32() == 37 &&
    !reflection->HasField(*message, renamed) &&
    reflection->GetUInt32(*message, renamed) == 37);
  auto empty = Zpb::load<FacetRow, ZuFacet::InteropFacet>({});
  ZuCheck(empty.last == 37 && !empty.first && !empty.hidden);
  bool parsed = message->ParseFromArray(nativeWire.data(),
    nativeWire.length());
  ZuCheck(parsed);
  if (parsed) {
    ZuCheck(reflection->GetUInt32(*message, first) == 11 &&
      reflection->GetUInt32(*message, renamed) == 23 &&
      !reflection->HasField(*message, hidden));
    reflection->SetUInt32(message, first, 31);
    reflection->SetUInt32(message, renamed, 37);
    reflection->SetUInt32(message, hidden, 41);
    std::string canonicalWire;
    bool encoded = message->SerializeToString(&canonicalWire);
    ZuBSpan input{reinterpret_cast<const uint8_t *>(canonicalWire.data()),
      unsigned(canonicalWire.length())};
    auto reflected = schema.message(".demo.FacetRow");
    ZuCheck((encoded && reflected && verify(schema, *reflected, input) &&
      Zpb::verify<FacetRow, ZuFacet::InteropFacet>(input)));
    if (encoded) {
      auto loaded = Zpb::load<FacetRow, ZuFacet::InteropFacet>(input);
      ZuCheck(loaded.first == 31 && loaded.last == 37 && !loaded.hidden);
    }
  }
  delete message;
}

static void testDiscardedChoice()
{
  ZuTestScope(testDiscardedChoice);
  DescSet descriptor;
  ZuCheck(exportSchema<RequiredChoiceRow>(descriptor,
    "required-choice.proto", "demo", Syntax::Proto2));
  if (!descriptor.files.length()) return;
  Output output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto bytes = Zpb::save(builder, descriptor);
  ZuCheck(!builder.failed() && verifySchema(bytes));
  if (builder.failed()) return;
  Schema schema{bytes};
  auto file = schema.file("required-choice.proto");
  ProtoText proto;
  ZuCheck(file && emitProto(proto, schema, *file,
      fileSyntax(*file)));
  if (!file) return;
  google::protobuf::FileDescriptorProto canonical;
  ZuCheck(parse(proto, file->name, canonical));
  google::protobuf::DescriptorPool pool;
  auto built = pool.BuildFile(canonical);
  ZuCheck(built);
  if (!built) return;
  auto row = built->FindMessageTypeByName("RequiredChoiceRow");
  ZuCheck(row && row->oneof_decl_count() == 1);
  if (!row) return;
  const uint8_t malformed[] = {
    0x22, 0x02, 0x08, 0x80, 0x4a, 0x02, 0x08, 0x09};
  const uint8_t absentRequired[] = {
    0x22, 0x00, 0x4a, 0x02, 0x08, 0x09};
  auto reflected = schema.message(".demo.RequiredChoiceRow");
  ZuCheck(reflected &&
    !verify(schema, *reflected, {malformed, sizeof(malformed)}) &&
    verify(schema, *reflected, {absentRequired, sizeof(absentRequired)}));
  google::protobuf::DynamicMessageFactory factory;
  auto message = factory.GetPrototype(row)->New();
  ZuCheck(!message->ParseFromArray(malformed, sizeof(malformed)) &&
    !Zpb::verify<RequiredChoiceRow>({malformed, sizeof(malformed)}));
  bool accepted = message->ParseFromArray(absentRequired,
    sizeof(absentRequired));
  ZuCheck(accepted && Zpb::verify<RequiredChoiceRow>(
    {absentRequired, sizeof(absentRequired)}));
  if (accepted) {
    auto fields = message->GetReflection();
    auto final = row->FindFieldByName("final");
    ZuCheck(final &&
      fields->GetOneofFieldDescriptor(*message, row->oneof_decl(0)) == final &&
      fields->GetMessage(*message, final).GetReflection()->GetUInt32(
	fields->GetMessage(*message, final),
	final->message_type()->FindFieldByName("id")) == 9);
  }
  delete message;
}

static void testNativeMapped()
{
  ZuTestScope(testNativeMapped);
  DescSet descriptor;
  ZuCheck(!exportSchema<NativeMappedRow>(descriptor,
    "native_mapped.proto", "demo", Syntax::Proto3));
  ZuCheck(!descriptor.files.length());
  ZuCheck(exportSchema<ZuFixed>(descriptor,
    "fixed_dep.proto", "Zf", Syntax::Proto3) &&
    exportSchema<NativeMappedRow>(descriptor,
      "native_mapped.proto", "demo", Syntax::Proto3));
  if (descriptor.files.length() != 2) return;
  ZuCheck(descriptor.files[1].dependencies.length() == 1 &&
    descriptor.files[1].dependencies[0] == "fixed_dep.proto");
  Output descriptorOutput;
  Zpb::ArrayStorage descriptorStorage{descriptorOutput};
  Zpb::Builder descriptorBuilder{descriptorStorage};
  auto descriptorWire = Zpb::save(descriptorBuilder, descriptor);
  ZuCheck(!descriptorBuilder.failed() && verifySchema(descriptorWire));
  if (descriptorBuilder.failed() || !verifySchema(descriptorWire)) return;
  Schema schema{descriptorWire};
  auto fixedFile = schema.file("fixed_dep.proto");
  auto rowFile = schema.file("native_mapped.proto");
  ProtoText fixedText, rowText;
  ZuCheck(fixedFile && rowFile &&
    emitProto(fixedText, schema, *fixedFile,
      fileSyntax(*fixedFile)) &&
    emitProto(rowText, schema, *rowFile,
      fileSyntax(*rowFile)) &&
    strstr(rowText.data(), "import \"fixed_dep.proto\";"));
  if (!fixedFile || !rowFile) return;
  google::protobuf::FileDescriptorProto fixedProto, rowProto;
  ZuCheck(parse(fixedText, "fixed_dep.proto", fixedProto) &&
    parse(rowText, "native_mapped.proto", rowProto));
  google::protobuf::DescriptorPool pool;
  auto fixed = pool.BuildFile(fixedProto);
  auto file = fixed ? pool.BuildFile(rowProto) : nullptr;
  ZuCheck(fixed && file);
  if (!file) return;
  auto type = file->FindMessageTypeByName("NativeMappedRow");
  auto amount = type ? type->FindFieldByName("amount") : nullptr;
  ZuCheck(amount && amount->message_type() ==
    fixed->FindMessageTypeByName("Fixed"));
  if (!amount) return;
  NativeMappedRow native{ZuFixed{-12345, 2}};
  Output output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto wire = Zpb::save(builder, native);
  google::protobuf::DynamicMessageFactory factory;
  auto canonical = factory.GetPrototype(type)->New();
  bool parsed = !builder.failed() &&
    canonical->ParseFromArray(wire.data(), wire.length());
  ZuCheck(parsed);
  if (parsed) {
    const auto &child = canonical->GetReflection()->GetMessage(
      *canonical, amount);
    auto mantissa = amount->message_type()->FindFieldByName("mantissa");
    auto ndp = amount->message_type()->FindFieldByName("ndp");
    ZuCheck(child.GetReflection()->GetInt64(child, mantissa) == -12345 &&
      child.GetReflection()->GetUInt32(child, ndp) == 2);
  }
  delete canonical;
}

static void testNativeEnumImport()
{
  ZuTestScope(testNativeEnumImport);
  DescSet descriptor;
  ZuCheck(exportSchema<DepColorRow>(descriptor,
    "color_dep.proto", "dep", Syntax::Proto2) &&
    exportSchema<ImportedColorRow>(descriptor,
      "color_user.proto", "demo", Syntax::Proto2));
  if (descriptor.files.length() != 2) return;
  ZuCheck(descriptor.files[1].dependencies.length() == 1 &&
    descriptor.files[1].dependencies[0] == "color_dep.proto");
  Output descriptorOutput;
  Zpb::ArrayStorage descriptorStorage{descriptorOutput};
  Zpb::Builder descriptorBuilder{descriptorStorage};
  auto descriptorWire = Zpb::save(descriptorBuilder, descriptor);
  ZuCheck(!descriptorBuilder.failed() && verifySchema(descriptorWire));
  if (descriptorBuilder.failed() || !verifySchema(descriptorWire)) return;
  Schema schema{descriptorWire};
  auto dep = schema.file("color_dep.proto");
  auto user = schema.file("color_user.proto");
  ProtoText depText, userText;
  ZuCheck(dep && user && emitProto(depText, schema, *dep,
      fileSyntax(*dep)) &&
    emitProto(userText, schema, *user,
      fileSyntax(*user)) &&
    strstr(userText.data(), "import \"color_dep.proto\";"));
  if (!dep || !user) return;
  google::protobuf::FileDescriptorProto depProto, userProto;
  ZuCheck(parse(depText, "color_dep.proto", depProto) &&
    parse(userText, "color_user.proto", userProto));
  google::protobuf::DescriptorPool pool;
  auto depFile = pool.BuildFile(depProto);
  auto userFile = depFile ? pool.BuildFile(userProto) : nullptr;
  ZuCheck(depFile && userFile);
  if (!userFile) return;
  auto type = userFile->FindMessageTypeByName("ImportedColorRow");
  auto field = type ? type->FindFieldByName("color") : nullptr;
  ZuCheck(field && field->enum_type() ==
    depFile->FindEnumTypeByName("NativeColor"));
  if (!field) return;
  ImportedColorRow native{NativeColor::Blue};
  Output output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto wire = Zpb::save(builder, native);
  google::protobuf::DynamicMessageFactory factory;
  auto canonical = factory.GetPrototype(type)->New();
  ZuCheck(!builder.failed() &&
    canonical->ParseFromArray(wire.data(), wire.length()) &&
    canonical->GetReflection()->GetEnumValue(*canonical, field) ==
      NativeColor::Blue);
  canonical->GetReflection()->SetEnumValue(canonical, field, NativeColor::Red);
  std::string reverse;
  bool serialized = canonical->SerializeToString(&reverse);
  ZuBSpan bytes{reinterpret_cast<const uint8_t *>(reverse.data()),
    unsigned(reverse.length())};
  ZuCheck(serialized && Zpb::verify<ImportedColorRow>(bytes) &&
    Zpb::load<ImportedColorRow>(bytes).color == NativeColor::Red);
  delete canonical;
}

static void testScalarVocabulary()
{
  ZuTestScope(testScalarVocabulary);
  DescSet descriptor;
  ZuCheck(exportSchema<NativeScalarRow>(descriptor,
    "scalar.proto", "demo", Syntax::Proto3));
  Output descriptorOutput;
  Zpb::ArrayStorage descriptorStorage{descriptorOutput};
  Zpb::Builder descriptorBuilder{descriptorStorage};
  auto descriptorWire = Zpb::save(descriptorBuilder, descriptor);
  ZuCheck(!descriptorBuilder.failed() && verifySchema(descriptorWire));
  if (descriptorBuilder.failed() || !verifySchema(descriptorWire)) return;
  Schema schema{descriptorWire};
  auto reflected = schema.message(".demo.NativeScalarRow");
  auto file = schema.file("scalar.proto");
  ProtoText source;
  ZuCheck(reflected && file && emitProto(source, schema, *file,
      fileSyntax(*file)));
  if (!reflected || !file) return;
  google::protobuf::FileDescriptorProto proto;
  ZuCheck(parse(source, "scalar.proto", proto));
  google::protobuf::DescriptorPool pool;
  auto built = pool.BuildFile(proto);
  ZuCheck(built);
  if (!built) return;
  auto type = built->FindMessageTypeByName("NativeScalarRow");
  auto ratioType = type ? type->FindFieldByName("ratio") : nullptr;
  auto preciseType = type ? type->FindFieldByName("precise") : nullptr;
  auto ratiosType = type ? type->FindFieldByName("ratios") : nullptr;
  auto precisionsType = type ? type->FindFieldByName("precisions") : nullptr;
  bool types = type && type->field_count() == 16 &&
    ratioType && ratioType->type() ==
      google::protobuf::FieldDescriptor::TYPE_FLOAT &&
    preciseType && preciseType->type() ==
      google::protobuf::FieldDescriptor::TYPE_DOUBLE &&
    ratiosType && ratiosType->type() ==
      google::protobuf::FieldDescriptor::TYPE_FLOAT &&
    precisionsType && precisionsType->type() ==
      google::protobuf::FieldDescriptor::TYPE_DOUBLE;
  ZuCheck(types);
  if (!types) return;
  auto small = type->FindFieldByName("small");
  auto count = type->FindFieldByName("count");
  auto ratio = type->FindFieldByName("ratio");
  auto precise = type->FindFieldByName("precise");
  auto textField = type->FindFieldByName("text");
  auto byteField = type->FindFieldByName("bytes");
  auto values = type->FindFieldByName("values");
  auto labels = type->FindFieldByName("labels");
  auto signed16 = type->FindFieldByName("signed16");
  auto unsigned16 = type->FindFieldByName("unsigned16");
  auto smalls = type->FindFieldByName("smalls");
  auto counts = type->FindFieldByName("counts");
  auto unsignedValues = type->FindFieldByName("unsignedValues");
  auto blobs = type->FindFieldByName("blobs");
  auto ratios = type->FindFieldByName("ratios");
  auto precisions = type->FindFieldByName("precisions");

  const uint8_t blob[] = {'x', 0, 'y'};
  const uint8_t repeatedBlob[] = {'q', 0};
  NativeScalarRow native;
  native.small = -7;
  native.count = 200;
  native.signed16 = -30000;
  native.unsigned16 = 60000;
  native.ratio = 1.5f;
  native.precise = 2.25;
  native.text = "native";
  native.bytes = {blob, sizeof(blob)};
  native.values.push(-300);
  native.values.push(1234);
  native.labels.push("first");
  native.labels.push("second");
  native.smalls.push(-5);
  native.counts.push(220);
  native.unsignedValues.push(60001);
  native.blobs.push(ZuBSpan{repeatedBlob, sizeof(repeatedBlob)});
  native.ratios.push(5.5f);
  native.precisions.push(6.25);
  Output output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto wire = Zpb::save(builder, native);
  ZuCheck(!builder.failed() && Zpb::verify<NativeScalarRow>(wire) &&
    verify(schema, *reflected, wire));
  google::protobuf::DynamicMessageFactory factory;
  auto canonical = factory.GetPrototype(type)->New();
  bool parsed = canonical->ParseFromArray(wire.data(), wire.length());
  ZuCheck(parsed);
  auto fields = canonical->GetReflection();
  if (parsed)
    ZuCheck(fields->GetInt32(*canonical, small) == -7 &&
      fields->GetUInt32(*canonical, count) == 200 &&
      fields->GetFloat(*canonical, ratio) == 1.5f &&
      fields->GetDouble(*canonical, precise) == 2.25 &&
      fields->GetString(*canonical, textField) == "native" &&
      fields->GetString(*canonical, byteField) == std::string("x\0y", 3) &&
      fields->GetInt32(*canonical, signed16) == -30000 &&
      fields->GetUInt32(*canonical, unsigned16) == 60000 &&
      fields->FieldSize(*canonical, values) == 2 &&
      fields->GetRepeatedInt32(*canonical, values, 0) == -300 &&
      fields->GetRepeatedString(*canonical, labels, 1) == "second" &&
      fields->GetRepeatedInt32(*canonical, smalls, 0) == -5 &&
      fields->GetRepeatedUInt32(*canonical, counts, 0) == 220 &&
      fields->GetRepeatedUInt32(*canonical, unsignedValues, 0) == 60001 &&
      fields->GetRepeatedString(*canonical, blobs, 0) ==
	std::string("q\0", 2) &&
      fields->GetRepeatedFloat(*canonical, ratios, 0) == 5.5f &&
      fields->GetRepeatedDouble(*canonical, precisions, 0) == 6.25);

  canonical->Clear();
  fields->SetInt32(canonical, small, -8);
  fields->SetUInt32(canonical, count, 201);
  fields->SetInt32(canonical, signed16, -30001);
  fields->SetUInt32(canonical, unsigned16, 60002);
  fields->SetFloat(canonical, ratio, 3.5f);
  fields->SetDouble(canonical, precise, 4.25);
  fields->SetString(canonical, textField, "reverse");
  fields->SetString(canonical, byteField, std::string("a\0b", 3));
  fields->AddInt32(canonical, values, -301);
  fields->AddString(canonical, labels, "third");
  fields->AddInt32(canonical, smalls, -6);
  fields->AddUInt32(canonical, counts, 221);
  fields->AddUInt32(canonical, unsignedValues, 60003);
  fields->AddString(canonical, blobs, std::string("r\0", 2));
  fields->AddFloat(canonical, ratios, 7.5f);
  fields->AddDouble(canonical, precisions, 8.25);
  std::string reverse;
  bool serialized = canonical->SerializeToString(&reverse);
  ZuBSpan bytes{reinterpret_cast<const uint8_t *>(reverse.data()),
    unsigned(reverse.length())};
  ZuCheck(serialized && verify(schema, *reflected, bytes) &&
    Zpb::verify<NativeScalarRow>(bytes));
  if (serialized) {
    auto loaded = Zpb::load<NativeScalarRow>(bytes);
    ZuCheck(loaded.small == -8 && loaded.count == 201 &&
      loaded.signed16 == -30001 && loaded.unsigned16 == 60002 &&
      loaded.ratio == 3.5f && loaded.precise == 4.25 &&
      loaded.text == "reverse" && loaded.bytes.length() == 3 &&
      !memcmp(loaded.bytes.data(), "a\0b", 3) &&
      loaded.values.length() == 1 && loaded.values[0] == -301 &&
      loaded.labels.length() == 1 && loaded.labels[0] == "third" &&
      loaded.smalls.length() == 1 && loaded.smalls[0] == -6 &&
      loaded.counts.length() == 1 && loaded.counts[0] == 221 &&
      loaded.unsignedValues.length() == 1 &&
      loaded.unsignedValues[0] == 60003 &&
      loaded.blobs.length() == 1 && loaded.blobs[0].length() == 2 &&
      !memcmp(loaded.blobs[0].data(), "r\0", 2) &&
      loaded.ratios.length() == 1 && loaded.ratios[0] == 7.5f &&
      loaded.precisions.length() == 1 && loaded.precisions[0] == 8.25);
  }
  fields->SetInt32(canonical, small, 128);
  std::string tooWide;
  serialized = canonical->SerializeToString(&tooWide);
  ZuBSpan invalid{reinterpret_cast<const uint8_t *>(tooWide.data()),
    unsigned(tooWide.length())};
  ZuCheck(serialized && verify(schema, *reflected, invalid) &&
    !Zpb::verify<NativeScalarRow>(invalid));
  delete canonical;
}

static void testIPMapping()
{
  ZuTestScope(testIPMapping);
  DescSet descriptor;
  ZuCheck(exportSchema<ZiIP>(descriptor,
    "ip.proto", "Zi", Syntax::Proto3));
  Output descriptorOutput;
  Zpb::ArrayStorage descriptorStorage{descriptorOutput};
  Zpb::Builder descriptorBuilder{descriptorStorage};
  auto descriptorWire = Zpb::save(descriptorBuilder, descriptor);
  ZuCheck(!descriptorBuilder.failed() && verifySchema(descriptorWire));
  if (descriptorBuilder.failed() || !verifySchema(descriptorWire)) return;
  Schema schema{descriptorWire};
  auto reflected = schema.message(".Zi.IP");
  ProtoText source;
  auto importedFile = schema.file("ip.proto");
  ZuCheck(reflected && importedFile &&
    emitProto(source, schema, *importedFile,
      fileSyntax(*importedFile)));
  if (!reflected || !importedFile) return;
  google::protobuf::FileDescriptorProto proto;
  ZuCheck(parse(source, "ip.proto", proto));
  google::protobuf::DescriptorPool pool;
  auto file = pool.BuildFile(proto);
  ZuCheck(file);
  if (!file) return;
  auto type = file->FindMessageTypeByName("IP");
  auto addr = type ? type->FindFieldByName("addr") : nullptr;
  ZuCheck(type && addr && addr->type() ==
    google::protobuf::FieldDescriptor::TYPE_BYTES);
  if (!addr) return;
  google::protobuf::DynamicMessageFactory factory;
  auto exchange = [&](const ZiIP &ip, const std::string &raw) {
    Output output;
    Zpb::ArrayStorage storage{output};
    Zpb::Builder builder{storage};
    auto wire = Zpb::save(builder, ip);
    if (builder.failed() || !Zpb::verify<ZiIP>(wire) ||
      !verify(schema, *reflected, wire)) return false;
    auto canonical = factory.GetPrototype(type)->New();
    bool ok = canonical->ParseFromArray(wire.data(), wire.length()) &&
      canonical->GetReflection()->GetString(*canonical, addr) == raw;
    canonical->Clear();
    canonical->GetReflection()->SetString(canonical, addr, raw);
    std::string reverse;
    ok &= canonical->SerializeToString(&reverse);
    ZuBSpan bytes{reinterpret_cast<const uint8_t *>(reverse.data()),
      unsigned(reverse.length())};
    ok &= verify(schema, *reflected, bytes) &&
      Zpb::verify<ZiIP>(bytes) && Zpb::load<ZiIP>(bytes) == ip;
    delete canonical;
    return ok;
  };
  ZuCheck(exchange(ZiIP{"127.0.0.1"}, std::string("\x7f\0\0\x01", 4)));
  const char v6[] = {0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 1};
  ZuCheck(exchange(ZiIP{"::1"}, std::string(v6, sizeof(v6))));

  auto invalid = factory.GetPrototype(type)->New();
  invalid->GetReflection()->SetString(invalid, addr, std::string("\x7f\0\x01", 3));
  std::string wrong;
  bool serialized = invalid->SerializeToString(&wrong);
  ZuBSpan bytes{reinterpret_cast<const uint8_t *>(wrong.data()),
    unsigned(wrong.length())};
  ZuCheck(serialized && verify(schema, *reflected, bytes) &&
    Zpb::verify<ZiIP>(bytes));
  delete invalid;
}

static void testFixedMapping()
{
  ZuTestScope(testFixedMapping);
  DescSet descriptor;
  ZuCheck(exportSchema<ZuFixed>(descriptor,
    "fixed.proto", "Zf", Syntax::Proto3));
  Output descriptorOutput;
  Zpb::ArrayStorage descriptorStorage{descriptorOutput};
  Zpb::Builder descriptorBuilder{descriptorStorage};
  auto descriptorWire = Zpb::save(descriptorBuilder, descriptor);
  ZuCheck(!descriptorBuilder.failed() && verifySchema(descriptorWire));
  if (descriptorBuilder.failed() || !verifySchema(descriptorWire)) return;
  Schema schema{descriptorWire};
  auto reflected = schema.message(".Zf.Fixed");
  auto importedFile = schema.file("fixed.proto");
  ProtoText source;
  ZuCheck(reflected && importedFile &&
    emitProto(source, schema, *importedFile,
      fileSyntax(*importedFile)));
  if (!reflected || !importedFile) return;
  google::protobuf::FileDescriptorProto proto;
  ZuCheck(parse(source, "fixed.proto", proto));
  google::protobuf::DescriptorPool pool;
  auto file = pool.BuildFile(proto);
  ZuCheck(file);
  if (!file) return;
  auto type = file->FindMessageTypeByName("Fixed");
  auto mantissa = type ? type->FindFieldByName("mantissa") : nullptr;
  auto ndp = type ? type->FindFieldByName("ndp") : nullptr;
  ZuCheck(type && mantissa && ndp &&
    mantissa->type() == google::protobuf::FieldDescriptor::TYPE_INT64 &&
    ndp->type() == google::protobuf::FieldDescriptor::TYPE_UINT32);
  if (!mantissa || !ndp) return;

  ZuFixed native{-12345, 2};
  Output output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto wire = Zpb::save(builder, native);
  ZuCheck(!builder.failed() && Zpb::verify<ZuFixed>(wire) &&
    verify(schema, *reflected, wire));
  google::protobuf::DynamicMessageFactory factory;
  auto canonical = factory.GetPrototype(type)->New();
  ZuCheck(canonical->ParseFromArray(wire.data(), wire.length()) &&
    canonical->GetReflection()->GetInt64(*canonical, mantissa) == -12345 &&
    canonical->GetReflection()->GetUInt32(*canonical, ndp) == 2);
  canonical->Clear();
  auto fields = canonical->GetReflection();
  fields->SetInt64(canonical, mantissa, -12345);
  fields->SetUInt32(canonical, ndp, 2);
  std::string reverse;
  bool serialized = canonical->SerializeToString(&reverse);
  ZuBSpan bytes{reinterpret_cast<const uint8_t *>(reverse.data()),
    unsigned(reverse.length())};
  ZuCheck(serialized && verify(schema, *reflected, bytes) &&
    Zpb::verify<ZuFixed>(bytes) && Zpb::load<ZuFixed>(bytes) == native);
  fields->SetUInt32(canonical, ndp, 19);
  std::string invalid;
  serialized = canonical->SerializeToString(&invalid);
  ZuBSpan invalidBytes{reinterpret_cast<const uint8_t *>(invalid.data()),
    unsigned(invalid.length())};
  ZuCheck(serialized && verify(schema, *reflected, invalidBytes) &&
    Zpb::verify<ZuFixed>(invalidBytes));
  delete canonical;
}

template <typename T>
static bool exchangeWords(const T &native, ZuCSpan fileName, ZuCSpan name,
  uint64_t high, uint64_t low)
{
  DescSet descriptor;
  if (!exportSchema<T>(descriptor, fileName, "Zf", Syntax::Proto3))
    return false;
  Output descriptorOutput;
  Zpb::ArrayStorage descriptorStorage{descriptorOutput};
  Zpb::Builder descriptorBuilder{descriptorStorage};
  auto descriptorWire = Zpb::save(descriptorBuilder, descriptor);
  if (descriptorBuilder.failed() || !verifySchema(descriptorWire)) return false;
  Schema schema{descriptorWire};
  ProtoText qualified;
  qualified << ".Zf." << name;
  auto reflected = schema.message(qualified);
  auto importedFile = schema.file(fileName);
  if (!reflected || !importedFile) return false;
  ProtoText source;
  if (!emitProto(source, schema, *importedFile,
      fileSyntax(*importedFile))) return false;
  google::protobuf::FileDescriptorProto proto;
  if (!parse(source, fileName, proto)) return false;
  google::protobuf::DescriptorPool pool;
  auto file = pool.BuildFile(proto);
  if (!file) return false;
  auto type = file->FindMessageTypeByName(
    std::string(name.data(), name.length()));
  if (!type) return false;
  auto h = type->FindFieldByName("h"), l = type->FindFieldByName("l");
  if (!h || !l || h->type() != google::protobuf::FieldDescriptor::TYPE_UINT64 ||
    l->type() != google::protobuf::FieldDescriptor::TYPE_UINT64) return false;

  Output output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto wire = Zpb::save(builder, native);
  if (builder.failed() || !Zpb::verify<T>(wire) ||
    !verify(schema, *reflected, wire)) return false;
  google::protobuf::DynamicMessageFactory factory;
  auto canonical = factory.GetPrototype(type)->New();
  bool ok = canonical->ParseFromArray(wire.data(), wire.length()) &&
    canonical->GetReflection()->GetUInt64(*canonical, h) == high &&
    canonical->GetReflection()->GetUInt64(*canonical, l) == low;
  canonical->Clear();
  auto fields = canonical->GetReflection();
  fields->SetUInt64(canonical, h, high);
  fields->SetUInt64(canonical, l, low);
  std::string reverse;
  ok &= canonical->SerializeToString(&reverse);
  ZuBSpan bytes{reinterpret_cast<const uint8_t *>(reverse.data()),
    unsigned(reverse.length())};
  ok &= verify(schema, *reflected, bytes) &&
    Zpb::verify<T>(bytes) && Zpb::load<T>(bytes) == native;
  if constexpr (ZuIsSame<T, ZuDecimal>{}) {
    uint128_t invalid = uint128_t(ZuDecimal::maximum() + 1);
    fields->SetUInt64(canonical, h, uint64_t(invalid >> 64));
    fields->SetUInt64(canonical, l, uint64_t(invalid));
    std::string wrong;
    bool serialized = canonical->SerializeToString(&wrong);
    ZuBSpan invalidBytes{reinterpret_cast<const uint8_t *>(wrong.data()),
      unsigned(wrong.length())};
    ok &= serialized && verify(schema, *reflected, invalidBytes) &&
      Zpb::verify<T>(invalidBytes);
  }
  delete canonical;
  return ok;
}

static void testWordMappings()
{
  ZuTestScope(testWordMappings);
  ZuCheck(exchangeWords(int128_t(-42), "int128.proto", "Int128",
    UINT64_MAX, uint64_t(-42)));
  ZuCheck(exchangeWords((uint128_t(UINT64_MAX) << 64) | 2,
    "uint128.proto", "UInt128", UINT64_MAX, 2));
  ZuDecimal decimal{"-123.456"};
  int128_t unscaled = -int128_t(123456) * int128_t(1000000000000000LL);
  uint128_t bits = uint128_t(unscaled);
  ZuCheck(decimal.value == unscaled &&
    exchangeWords(decimal, "decimal.proto", "Decimal",
      uint64_t(bits >> 64), uint64_t(bits)));
}

template <typename T, typename Fn>
static bool mappedSchema(ZuCSpan fileName, ZuCSpan name, Fn &&fn,
  ZuCSpan package = "Zf", Syntax::T syntax = Syntax::Proto3)
{
  DescSet descriptor;
  if (!exportSchema<T>(descriptor, fileName, package, syntax))
    return false;
  Output descriptorOutput;
  Zpb::ArrayStorage descriptorStorage{descriptorOutput};
  Zpb::Builder descriptorBuilder{descriptorStorage};
  auto descriptorWire = Zpb::save(descriptorBuilder, descriptor);
  if (descriptorBuilder.failed() || !verifySchema(descriptorWire)) return false;
  Schema schema{descriptorWire};
  ProtoText qualified;
  qualified << '.' << package << '.' << name;
  auto reflected = schema.message(qualified);
  auto importedFile = schema.file(fileName);
  if (!reflected || !importedFile) return false;
  ProtoText source;
  if (!emitProto(source, schema, *importedFile,
      fileSyntax(*importedFile))) return false;
  google::protobuf::FileDescriptorProto proto;
  if (!parse(source, fileName, proto)) return false;
  google::protobuf::DescriptorPool pool;
  auto file = pool.BuildFile(proto);
  if (!file) return false;
  auto type = file->FindMessageTypeByName(
    std::string(name.data(), name.length()));
  return type && fn(schema, *reflected, *type);
}

static void testBoolVecInterop()
{
  ZuTestScope(testBoolVecInterop);
  ZuCheck(mappedSchema<NativeBoolRow>("boolvec.proto", "NativeBoolRow",
    [](const Schema &schema, const MsgDesc &reflected,
      const google::protobuf::Descriptor &type) {
      auto value = type.FindFieldByName("values");
      if (type.field_count() != 1 || !value ||
	value->type() != google::protobuf::FieldDescriptor::TYPE_BOOL ||
	!value->is_repeated() || value->is_packed()) return false;
      NativeBoolRow native;
      native.values.push(0);
      native.values.push(2);
      Output output;
      Zpb::ArrayStorage storage{output};
      Zpb::Builder builder{storage};
      auto wire = Zpb::save(builder, native);
      if (builder.failed() || !Zpb::verify<NativeBoolRow>(wire) ||
	!verify(schema, reflected, wire)) return false;
      google::protobuf::DynamicMessageFactory factory;
      auto canonical = factory.GetPrototype(&type)->New();
      auto fields = canonical->GetReflection();
      bool ok = canonical->ParseFromArray(wire.data(), wire.length()) &&
	fields->FieldSize(*canonical, value) == 2 &&
	!fields->GetRepeatedBool(*canonical, value, 0) &&
	fields->GetRepeatedBool(*canonical, value, 1);
      canonical->Clear();
      fields->AddBool(canonical, value, true);
      fields->AddBool(canonical, value, false);
      std::string reverse;
      bool serialized = canonical->SerializeToString(&reverse);
      ZuBSpan bytes{reinterpret_cast<const uint8_t *>(reverse.data()),
	unsigned(reverse.length())};
      ok &= serialized && Zpb::verify<NativeBoolRow>(bytes) &&
	verify(schema, reflected, bytes);
      if (ok) {
	auto loaded = Zpb::load<NativeBoolRow>(bytes);
	ok = loaded.values.length() == 2 && loaded.values[0] == 1 &&
	  loaded.values[1] == 0;
      }
      delete canonical;
      return ok;
    }, "demo"));
  ZuCheck(mappedSchema<NativePackedBoolRow>("packed-bool.proto",
    "NativePackedBoolRow",
    [](const Schema &schema, const MsgDesc &reflected,
      const google::protobuf::Descriptor &type) {
      auto value = type.FindFieldByName("values");
      if (!value || !value->is_repeated() || !value->is_packed()) return false;
      google::protobuf::FileDescriptorSet canonicalSet;
      type.file()->CopyTo(canonicalSet.add_file());
      std::string descriptorBytes;
      if (!canonicalSet.SerializeToString(&descriptorBytes)) return false;
      ZuBSpan descriptorWire{
	reinterpret_cast<const uint8_t *>(descriptorBytes.data()),
	unsigned(descriptorBytes.length())};
      if (!verifySchema(descriptorWire)) return false;
      Schema imported{descriptorWire};
      auto importedType = imported.message(".demo.NativePackedBoolRow");
      if (!importedType || packedOption(importedType->fields[0]) != 1)
	return false;
      NativePackedBoolRow native;
      native.values.push(0);
      native.values.push(2);
      Output output;
      Zpb::ArrayStorage storage{output};
      Zpb::Builder builder{storage};
      auto wire = Zpb::save(builder, native);
      const uint8_t expected[] = {0x0a, 0x02, 0x00, 0x01};
      if (builder.failed() || wire.length() != sizeof(expected) ||
	memcmp(wire.data(), expected, sizeof(expected)) ||
	!Zpb::verify<NativePackedBoolRow>(wire) ||
	!verify(schema, reflected, wire)) return false;
      google::protobuf::DynamicMessageFactory factory;
      auto canonical = factory.GetPrototype(&type)->New();
      auto fields = canonical->GetReflection();
      bool ok = canonical->ParseFromArray(wire.data(), wire.length()) &&
	fields->FieldSize(*canonical, value) == 2 &&
	!fields->GetRepeatedBool(*canonical, value, 0) &&
	fields->GetRepeatedBool(*canonical, value, 1);
      delete canonical;
      return ok;
    }, "demo"));
}

static void testCStringInterop()
{
  ZuTestScope(testCStringInterop);
  const uint8_t input[] = {
    0x0a, 6, 'n', 'a', 't', 'i', 'v', 'e',
    0x12, 3, 'o', 'n', 'e', 0x12, 3, 't', 'w', 'o'};
  ZuBSpan bytes{input, sizeof(input)};
  ZuCheck(Zpb::verify<NativeCStringRow>(bytes));
  auto native = Zpb::load<NativeCStringRow>(bytes);
  ZuCheck(native.text && !strcmp(native.text, "native") &&
    native.values.length() == 2 && !strcmp(native.values[0], "one") &&
    !strcmp(native.values[1], "two"));
  ZuCheck(mappedSchema<NativeCStringRow>("cstring.proto", "NativeCStringRow",
    [&](const Schema &schema, const MsgDesc &reflected,
      const google::protobuf::Descriptor &type) {
      auto textField = type.FindFieldByName("text");
      auto valuesField = type.FindFieldByName("values");
      if (!textField || !valuesField ||
	textField->type() != google::protobuf::FieldDescriptor::TYPE_STRING ||
	valuesField->type() != google::protobuf::FieldDescriptor::TYPE_STRING ||
	!valuesField->is_repeated()) return false;
      Output output;
      Zpb::ArrayStorage storage{output};
      Zpb::Builder builder{storage};
      auto wire = Zpb::save(builder, native);
      if (builder.failed() || !verify(schema, reflected, wire)) return false;
      google::protobuf::DynamicMessageFactory factory;
      auto canonical = factory.GetPrototype(&type)->New();
      auto fields = canonical->GetReflection();
      bool ok = canonical->ParseFromArray(wire.data(), wire.length()) &&
	fields->GetString(*canonical, textField) == "native" &&
	fields->FieldSize(*canonical, valuesField) == 2 &&
	fields->GetRepeatedString(*canonical, valuesField, 0) == "one" &&
	fields->GetRepeatedString(*canonical, valuesField, 1) == "two";
      canonical->Clear();
      fields->SetString(canonical, textField, "reply");
      fields->AddString(canonical, valuesField, "three");
      std::string reverse;
      ok &= canonical->SerializeToString(&reverse);
      ZuBSpan result{reinterpret_cast<const uint8_t *>(reverse.data()),
	unsigned(reverse.length())};
      ok &= verify(schema, reflected, result) &&
	Zpb::verify<NativeCStringRow>(result);
      if (ok) {
	auto loaded = Zpb::load<NativeCStringRow>(result);
	ok = loaded.text && !strcmp(loaded.text, "reply") &&
	  loaded.text[5] == 0 && loaded.values.length() == 1 &&
	  !strcmp(loaded.values[0], "three") && loaded.values[0][5] == 0;
      }
      fields->SetString(canonical, textField, "");
      std::string empty;
      ok &= canonical->SerializeToString(&empty);
      ZuBSpan invalid{reinterpret_cast<const uint8_t *>(empty.data()),
	unsigned(empty.length())};
      ok &= verify(schema, reflected, invalid) &&
	Zpb::verify<NativeCStringRow>(invalid);
      delete canonical;
      return ok;
    }, "demo", Syntax::Proto2));
}

static void testWireNumbers()
{
  ZuTestScope(testWireNumbers);
  NativeWireRow native;
  native.signed32 = -5;
  native.signed64 = -7;
  native.unsigned32 = 0x12345678;
  native.unsigned64 = 0x0102030405060708;
  native.fixedSigned32 = -11;
  native.fixedSigned64 = -13;
  native.zig32.push(-2);
  native.zig64.push(-3);
  native.fixed32.push(-4);
  native.fixed64.push(0x1122334455667788);
  native.fixedUnsigned32.push(0xabcdef01);
  native.fixedSigned64s.push(-9);
  native.plainSigned32 = -25;
  native.plainSigned64 = -27;
  native.plainUnsigned32 = 0xfedcba98;
  native.plainUnsigned64 = 0x8877665544332211;
  native.plain32s.push(-31);
  native.plain64s.push(-33);
  native.plainU32s.push(0x12345678);
  native.plainU64s.push(0x0102030405060708);
  ZuCheck(mappedSchema<NativeWireRow>("wire-numbers.proto", "NativeWireRow",
    [&](const Schema &schema, const MsgDesc &reflected,
      const google::protobuf::Descriptor &type) {
      auto s32 = type.FindFieldByName("signed32");
      auto s64 = type.FindFieldByName("signed64");
      auto u32 = type.FindFieldByName("unsigned32");
      auto u64 = type.FindFieldByName("unsigned64");
      auto f32 = type.FindFieldByName("fixedSigned32");
      auto f64 = type.FindFieldByName("fixedSigned64");
      auto z32 = type.FindFieldByName("zig32");
      auto z64 = type.FindFieldByName("zig64");
      auto v32 = type.FindFieldByName("fixed32");
      auto v64 = type.FindFieldByName("fixed64");
      auto uv32 = type.FindFieldByName("fixedUnsigned32");
      auto sv64 = type.FindFieldByName("fixedSigned64s");
      auto ps32 = type.FindFieldByName("plainSigned32");
      auto ps64 = type.FindFieldByName("plainSigned64");
      auto pu32 = type.FindFieldByName("plainUnsigned32");
      auto pu64 = type.FindFieldByName("plainUnsigned64");
      auto p32s = type.FindFieldByName("plain32s");
      auto p64s = type.FindFieldByName("plain64s");
      auto pu32s = type.FindFieldByName("plainU32s");
      auto pu64s = type.FindFieldByName("plainU64s");
      if (type.field_count() != 20 || !s32 || !s64 || !u32 || !u64 ||
	!f32 || !f64 || !z32 || !z64 || !v32 || !v64 || !uv32 || !sv64 ||
	!ps32 || !ps64 || !pu32 || !pu64 ||
	!p32s || !p64s || !pu32s || !pu64s ||
	s32->type() != google::protobuf::FieldDescriptor::TYPE_SINT32 ||
	s64->type() != google::protobuf::FieldDescriptor::TYPE_SINT64 ||
	u32->type() != google::protobuf::FieldDescriptor::TYPE_FIXED32 ||
	u64->type() != google::protobuf::FieldDescriptor::TYPE_FIXED64 ||
	f32->type() != google::protobuf::FieldDescriptor::TYPE_SFIXED32 ||
	f64->type() != google::protobuf::FieldDescriptor::TYPE_SFIXED64 ||
	uv32->type() != google::protobuf::FieldDescriptor::TYPE_FIXED32 ||
	sv64->type() != google::protobuf::FieldDescriptor::TYPE_SFIXED64 ||
	ps32->type() != google::protobuf::FieldDescriptor::TYPE_INT32 ||
	ps64->type() != google::protobuf::FieldDescriptor::TYPE_INT64 ||
	pu32->type() != google::protobuf::FieldDescriptor::TYPE_UINT32 ||
	pu64->type() != google::protobuf::FieldDescriptor::TYPE_UINT64 ||
	p32s->type() != google::protobuf::FieldDescriptor::TYPE_INT32 ||
	p64s->type() != google::protobuf::FieldDescriptor::TYPE_INT64 ||
	pu32s->type() != google::protobuf::FieldDescriptor::TYPE_UINT32 ||
	pu64s->type() != google::protobuf::FieldDescriptor::TYPE_UINT64 ||
	z32->is_packed() || z64->is_packed() ||
	v32->is_packed() || v64->is_packed() ||
	uv32->is_packed() || sv64->is_packed() ||
	p32s->is_packed() || p64s->is_packed() ||
	pu32s->is_packed() || pu64s->is_packed()) return false;
      Output output;
      Zpb::ArrayStorage storage{output};
      Zpb::Builder builder{storage};
      auto wire = Zpb::save(builder, native);
      if (builder.failed() || !Zpb::verify<NativeWireRow>(wire) ||
	!verify(schema, reflected, wire)) return false;
      google::protobuf::DynamicMessageFactory factory;
      auto canonical = factory.GetPrototype(&type)->New();
      auto fields = canonical->GetReflection();
      bool ok = canonical->ParseFromArray(wire.data(), wire.length()) &&
	fields->GetInt32(*canonical, s32) == -5 &&
	fields->GetInt64(*canonical, s64) == -7 &&
	fields->GetUInt32(*canonical, u32) == 0x12345678 &&
	fields->GetUInt64(*canonical, u64) == 0x0102030405060708 &&
	fields->GetInt32(*canonical, f32) == -11 &&
	fields->GetInt64(*canonical, f64) == -13 &&
	fields->FieldSize(*canonical, z32) == 1 &&
	fields->FieldSize(*canonical, z64) == 1 &&
	fields->FieldSize(*canonical, v32) == 1 &&
	fields->FieldSize(*canonical, v64) == 1 &&
	fields->FieldSize(*canonical, uv32) == 1 &&
	fields->FieldSize(*canonical, sv64) == 1 &&
	fields->GetRepeatedInt32(*canonical, z32, 0) == -2 &&
	fields->GetRepeatedInt64(*canonical, z64, 0) == -3 &&
	fields->GetRepeatedInt32(*canonical, v32, 0) == -4 &&
	fields->GetRepeatedUInt64(*canonical, v64, 0) ==
	  0x1122334455667788 &&
	fields->GetRepeatedUInt32(*canonical, uv32, 0) == 0xabcdef01 &&
	fields->GetRepeatedInt64(*canonical, sv64, 0) == -9 &&
	fields->GetInt32(*canonical, ps32) == -25 &&
	fields->GetInt64(*canonical, ps64) == -27 &&
	fields->GetUInt32(*canonical, pu32) == 0xfedcba98 &&
	fields->GetUInt64(*canonical, pu64) == 0x8877665544332211 &&
	fields->FieldSize(*canonical, p32s) == 1 &&
	fields->FieldSize(*canonical, p64s) == 1 &&
	fields->FieldSize(*canonical, pu32s) == 1 &&
	fields->FieldSize(*canonical, pu64s) == 1 &&
	fields->GetRepeatedInt32(*canonical, p32s, 0) == -31 &&
	fields->GetRepeatedInt64(*canonical, p64s, 0) == -33 &&
	fields->GetRepeatedUInt32(*canonical, pu32s, 0) == 0x12345678 &&
	fields->GetRepeatedUInt64(*canonical, pu64s, 0) ==
	  0x0102030405060708;
      canonical->Clear();
      fields->SetInt32(canonical, s32, -15);
      fields->SetInt64(canonical, s64, -17);
      fields->SetUInt32(canonical, u32, 0x87654321);
      fields->SetUInt64(canonical, u64, 0x8877665544332211);
      fields->SetInt32(canonical, f32, -21);
      fields->SetInt64(canonical, f64, -23);
      fields->AddInt32(canonical, z32, -6);
      fields->AddInt64(canonical, z64, -7);
      fields->AddInt32(canonical, v32, -8);
      fields->AddUInt64(canonical, v64, 0x123456789abcdef0);
      fields->AddUInt32(canonical, uv32, 0xfedcba98);
      fields->AddInt64(canonical, sv64, -19);
      fields->SetInt32(canonical, ps32, -35);
      fields->SetInt64(canonical, ps64, -37);
      fields->SetUInt32(canonical, pu32, 0xabcdef01);
      fields->SetUInt64(canonical, pu64, 0x1122334455667788);
      fields->AddInt32(canonical, p32s, -41);
      fields->AddInt64(canonical, p64s, -43);
      fields->AddUInt32(canonical, pu32s, 0x87654321);
      fields->AddUInt64(canonical, pu64s, 0x8877665544332211);
      std::string reverse;
      ok &= canonical->SerializeToString(&reverse);
      reverse.append("\x38\x11", 2); // unpacked zig32 field 7 = -9
      ZuBSpan bytes{reinterpret_cast<const uint8_t *>(reverse.data()),
	unsigned(reverse.length())};
      ok &= verify(schema, reflected, bytes) &&
	Zpb::verify<NativeWireRow>(bytes);
      if (ok) {
	auto loaded = Zpb::load<NativeWireRow>(bytes);
	ok = loaded.signed32 == -15 && loaded.signed64 == -17 &&
	  loaded.unsigned32 == 0x87654321 &&
	  loaded.unsigned64 == 0x8877665544332211 &&
	  loaded.fixedSigned32 == -21 && loaded.fixedSigned64 == -23 &&
	  loaded.zig32.length() == 2 && loaded.zig32[0] == -6 &&
	  loaded.zig32[1] == -9 && loaded.zig64.length() == 1 &&
	  loaded.zig64[0] == -7 && loaded.fixed32.length() == 1 &&
	  loaded.fixed32[0] == -8 && loaded.fixed64.length() == 1 &&
	  loaded.fixed64[0] == 0x123456789abcdef0 &&
	  loaded.fixedUnsigned32.length() == 1 &&
	  loaded.fixedUnsigned32[0] == 0xfedcba98 &&
	  loaded.fixedSigned64s.length() == 1 &&
	  loaded.fixedSigned64s[0] == -19 &&
	  loaded.plainSigned32 == -35 && loaded.plainSigned64 == -37 &&
	  loaded.plainUnsigned32 == 0xabcdef01 &&
	  loaded.plainUnsigned64 == 0x1122334455667788 &&
	  loaded.plain32s.length() == 1 && loaded.plain32s[0] == -41 &&
	  loaded.plain64s.length() == 1 && loaded.plain64s[0] == -43 &&
	  loaded.plainU32s.length() == 1 &&
	  loaded.plainU32s[0] == 0x87654321 &&
	  loaded.plainU64s.length() == 1 &&
	  loaded.plainU64s[0] == 0x8877665544332211;
      }
      delete canonical;
      return ok;
    }, "demo"));
}

template <typename T, typename Read, typename Write, typename Check>
static bool benchExchange(ZuCSpan fileName, ZuCSpan name, const T &native,
  Read read, Write write, Check check)
{
  return mappedSchema<T>(fileName, name,
    [&](const Schema &schema, const MsgDesc &reflected,
      const google::protobuf::Descriptor &type) {
      Output output;
      Zpb::ArrayStorage storage{output};
      Zpb::Builder builder{storage};
      auto wire = Zpb::save(builder, native);
      if (builder.failed() || !Zpb::verify<T>(wire) ||
	!verify(schema, reflected, wire)) return false;
      google::protobuf::DynamicMessageFactory factory;
      auto canonical = factory.GetPrototype(&type)->New();
      bool ok = canonical->ParseFromArray(wire.data(), wire.length()) &&
	read(*canonical);
      canonical->Clear();
      if (ok) {
	write(*canonical);
	std::string reverse;
	ok = canonical->SerializeToString(&reverse);
	ZuBSpan bytes{reinterpret_cast<const uint8_t *>(reverse.data()),
	  unsigned(reverse.length())};
	ok &= verify(schema, reflected, bytes) &&
	  Zpb::verify<T>(bytes) && check(Zpb::load<T>(bytes));
      }
      delete canonical;
      return ok;
    }, "bench");
}

static void testBenchReadiness()
{
  ZuTestScope(testBenchReadiness);
  auto smallRead = [](const google::protobuf::Message &message) {
    auto type = message.GetDescriptor();
    auto fields = message.GetReflection();
    return fields->GetUInt32(message, type->FindFieldByName("id")) == 7 &&
      fields->GetBool(message, type->FindFieldByName("enabled"));
  };
  auto smallWrite = [](google::protobuf::Message &message) {
    auto type = message.GetDescriptor();
    auto fields = message.GetReflection();
    fields->SetUInt32(&message, type->FindFieldByName("id"), 7);
    fields->SetBool(&message, type->FindFieldByName("enabled"), true);
  };
  ZuCheck(benchExchange("bench-small.proto", "Small", BenchSmall{7, true},
    smallRead, smallWrite, [](const BenchSmall &value) {
      return value.id == 7 && value.enabled;
    }));

  auto nestedRead = [&](const google::protobuf::Message &message) {
    auto type = message.GetDescriptor();
    auto fields = message.GetReflection();
    const auto &child = fields->GetMessage(message,
      type->FindFieldByName("child"));
    return smallRead(child) &&
      fields->GetUInt32(message, type->FindFieldByName("sequence")) == 43;
  };
  auto nestedWrite = [&](google::protobuf::Message &message) {
    auto type = message.GetDescriptor();
    auto fields = message.GetReflection();
    smallWrite(*fields->MutableMessage(&message,
      type->FindFieldByName("child")));
    fields->SetUInt32(&message, type->FindFieldByName("sequence"), 43);
  };
  ZuCheck(benchExchange("bench-nested.proto", "Nested",
    BenchNested{{7, true}, 43}, nestedRead, nestedWrite,
    [](const BenchNested &value) {
      return value.child.id == 7 && value.child.enabled &&
	value.sequence == 43;
    }));

  BenchRepeated repeated;
  for (int i = 1; i <= 32; ++i) {
    repeated.numbers.push(i);
    repeated.entries.add(i, i << 2);
  }
  auto repeatedRead = [](const google::protobuf::Message &message) {
    auto type = message.GetDescriptor();
    auto fields = message.GetReflection();
    auto numbers = type->FindFieldByName("numbers");
    auto entries = type->FindFieldByName("entries");
    if (!entries->is_map() || fields->FieldSize(message, numbers) != 32 ||
	fields->FieldSize(message, entries) != 32) return false;
    uint32_t seen = 0;
    for (int i = 0; i < 32; ++i) {
      if (fields->GetRepeatedInt32(message, numbers, i) != i + 1)
	return false;
      const auto &entry = fields->GetRepeatedMessage(message, entries, i);
      auto entryType = entry.GetDescriptor();
      auto entryFields = entry.GetReflection();
      int key = entryFields->GetInt32(entry,
	entryType->FindFieldByName("key"));
      int value = entryFields->GetInt32(entry,
	entryType->FindFieldByName("value"));
      if (key < 1 || key > 32 || value != key << 2 ||
	(seen & (1U << (key - 1)))) return false;
      seen |= 1U << (key - 1);
    }
    return seen == UINT32_MAX;
  };
  auto repeatedWrite = [](google::protobuf::Message &message) {
    auto type = message.GetDescriptor();
    auto fields = message.GetReflection();
    auto numbers = type->FindFieldByName("numbers");
    auto entries = type->FindFieldByName("entries");
    for (int i = 1; i <= 32; ++i) {
      fields->AddInt32(&message, numbers, i);
      auto entry = fields->AddMessage(&message, entries);
      auto entryType = entry->GetDescriptor();
      auto entryFields = entry->GetReflection();
      entryFields->SetInt32(entry, entryType->FindFieldByName("key"), i);
      entryFields->SetInt32(entry, entryType->FindFieldByName("value"),
	i << 2);
    }
  };
  ZuCheck(benchExchange("bench-repeated.proto", "Repeated", repeated,
    repeatedRead, repeatedWrite, [](const BenchRepeated &value) {
      if (value.numbers.length() != 32 || value.entries.citer().count() != 32)
	return false;
      for (int i = 1; i <= 32; ++i) {
	auto entry = value.entries.find(i);
	if (value.numbers[i - 1] != i || !entry || entry->val() != i << 2)
	  return false;
      }
      return true;
    }));

  constexpr unsigned PayloadSize = 1U << 16;
  Output bytes;
  bytes.length(PayloadSize);
  ProtoText text;
  text.length(PayloadSize);
  for (unsigned i = 0; i < PayloadSize; ++i) {
    bytes[i] = uint8_t(i ^ (i >> 8));
    text[i] = char('a' + i % 26);
  }
  std::string rawBytes{reinterpret_cast<const char *>(bytes.data()),
    bytes.length()};
  std::string rawText{text.data(), text.length()};
  auto largeRead = [&](const google::protobuf::Message &message) {
    auto type = message.GetDescriptor();
    auto fields = message.GetReflection();
    return fields->GetString(message, type->FindFieldByName("bytes")) ==
      rawBytes && fields->GetString(message,
	type->FindFieldByName("text")) == rawText;
  };
  auto largeWrite = [&](google::protobuf::Message &message) {
    auto type = message.GetDescriptor();
    auto fields = message.GetReflection();
    fields->SetString(&message, type->FindFieldByName("bytes"), rawBytes);
    fields->SetString(&message, type->FindFieldByName("text"), rawText);
  };
  ZuCheck(benchExchange("bench-large.proto", "Large",
    BenchLarge{bytes, text}, largeRead, largeWrite,
    [&](const BenchLarge &value) {
      return value.bytes == bytes && value.text == text;
    }));
}

template <typename T>
static bool benchCanonical(ZuCSpan fileName, ZuCSpan name, const T &native,
  unsigned count, int operation)
{
  return mappedSchema<T>(fileName, name,
    [&](const Schema &schema, const MsgDesc &reflected,
      const google::protobuf::Descriptor &type) {
      Output output;
      Zpb::ArrayStorage storage{output};
      Zpb::Builder builder{storage};
      auto wire = Zpb::save(builder, native);
      if (builder.failed() || !Zpb::verify<T>(wire) ||
	!verify(schema, reflected, wire)) return false;

      google::protobuf::DynamicMessageFactory factory;
      auto canonical = factory.GetPrototype(&type)->New();
      bool ok = canonical->ParseFromArray(wire.data(), wire.length()) &&
	canonical->IsInitialized();
      std::string encoded;
      if (ok) ok = canonical->SerializeToString(&encoded);
      ZuBSpan canonicalWire{
	reinterpret_cast<const uint8_t *>(encoded.data()),
	unsigned(encoded.length())};
      if (ok) ok = Zpb::verify<T>(canonicalWire) &&
	verify(schema, reflected, canonicalWire);
      if (!ok) { delete canonical; return false; }
      {
	auto decoded = Zpb::load<T>(canonicalWire);
	Output nativeOutput;
	Zpb::ArrayStorage nativeStorage{nativeOutput};
	Zpb::Builder nativeBuilder{nativeStorage};
	auto nativeWire = Zpb::save(nativeBuilder, decoded);
	auto copy = factory.GetPrototype(&type)->New();
	ok = !nativeBuilder.failed() &&
	  copy->ParseFromArray(nativeWire.data(), nativeWire.length()) &&
	  google::protobuf::util::MessageDifferencer::Equals(*canonical, *copy);
	delete copy;
	if (!ok) { delete canonical; return false; }
      }

      uint64_t checksum = 0;
      auto begin = Zm::now();
      for (unsigned i = 0; i < count; ++i) {
	__asm__ __volatile__("" : : : "memory");
	if (operation == 0) {
	  canonical->Clear();
	  if (!canonical->ParseFromArray(wire.data(), wire.length())) {
	    delete canonical;
	    return false;
	  }
	  __asm__ __volatile__("" : : "g"(canonical) : "memory");
	  checksum += wire.length();
	} else {
	  if (!canonical->SerializeToString(&encoded)) {
	    delete canonical;
	    return false;
	  }
	  checksum += encoded.length();
	}
      }
      auto elapsed = Zm::now() - begin;
      delete canonical;
      std::cout << name << ',' << (operation ? "serialize" : "parse") <<
	',' << wire.length() << ',' << count << ',' <<
	elapsed.as_decimal() << ',' << checksum << '\n';
      return true;
    }, "bench");
}

static bool benchmark(unsigned count, int workload, int operation)
{
  switch (workload) {
    case 0:
      return benchCanonical("bench-small.proto", "Small",
	BenchSmall{7, true}, count, operation);
    case 1:
      return benchCanonical("bench-nested.proto", "Nested",
	BenchNested{{7, true}, 43}, count, operation);
    case 2: {
      BenchRepeated repeated;
      for (int i = 1; i <= 32; ++i) {
	repeated.numbers.push(i);
	repeated.entries.add(i, i << 2);
      }
      return benchCanonical("bench-repeated.proto", "Repeated", repeated,
	count, operation);
    }
    case 3: {
      constexpr unsigned PayloadSize = 1U << 16;
      Output bytes;
      bytes.length(PayloadSize);
      ProtoText text;
      text.length(PayloadSize);
      for (unsigned i = 0; i < PayloadSize; ++i) {
	bytes[i] = uint8_t(i ^ (i >> 8));
	text[i] = char('a' + i % 26);
      }
      return benchCanonical("bench-large.proto", "Large",
	BenchLarge{bytes, text}, count, operation);
    }
  }
  return false;
}

static void testTemporalMappings()
{
  ZuTestScope(testTemporalMappings);
  ZuTime time{123, 456};
  ZuCheck(mappedSchema<ZuTime>("time.proto", "Time",
    [&](const Schema &schema, const MsgDesc &reflected,
      const google::protobuf::Descriptor &type) {
      auto sec = type.FindFieldByName("sec");
      auto nsec = type.FindFieldByName("nsec");
      if (!sec || !nsec ||
	sec->type() != google::protobuf::FieldDescriptor::TYPE_INT64 ||
	nsec->type() != google::protobuf::FieldDescriptor::TYPE_INT32)
	return false;
      Output output;
      Zpb::ArrayStorage storage{output};
      Zpb::Builder builder{storage};
      auto wire = Zpb::save(builder, time);
      if (builder.failed() || !Zpb::verify<ZuTime>(wire) ||
	!verify(schema, reflected, wire)) return false;
      google::protobuf::DynamicMessageFactory factory;
      auto canonical = factory.GetPrototype(&type)->New();
      bool ok = canonical->ParseFromArray(wire.data(), wire.length()) &&
	canonical->GetReflection()->GetInt64(*canonical, sec) == 123 &&
	canonical->GetReflection()->GetInt32(*canonical, nsec) == 456;
      canonical->Clear();
      auto fields = canonical->GetReflection();
      fields->SetInt64(canonical, sec, 123);
      fields->SetInt32(canonical, nsec, 456);
      std::string reverse;
      ok &= canonical->SerializeToString(&reverse);
      ZuBSpan bytes{reinterpret_cast<const uint8_t *>(reverse.data()),
	unsigned(reverse.length())};
      ok &= verify(schema, reflected, bytes) &&
	Zpb::verify<ZuTime>(bytes) && Zpb::load<ZuTime>(bytes) == time;
      fields->SetInt64(canonical, sec, ZuCmp<int64_t>::null());
      std::string invalid;
      bool serialized = canonical->SerializeToString(&invalid);
      ZuBSpan invalidBytes{reinterpret_cast<const uint8_t *>(invalid.data()),
	unsigned(invalid.length())};
      ok &= serialized && verify(schema, reflected, invalidBytes) &&
	Zpb::verify<ZuTime>(invalidBytes);
      delete canonical;
      return ok;
    }));

  ZuDateTime dateTime{2024, 2, 29, 12, 34, 56, 789};
  ZuCheck(mappedSchema<ZuDateTime>("datetime.proto", "DateTime",
    [&](const Schema &schema, const MsgDesc &reflected,
      const google::protobuf::Descriptor &type) {
      auto julian = type.FindFieldByName("julian");
      auto sec = type.FindFieldByName("sec");
      auto nsec = type.FindFieldByName("nsec");
      if (!julian || !sec || !nsec ||
	julian->type() != google::protobuf::FieldDescriptor::TYPE_INT32 ||
	sec->type() != google::protobuf::FieldDescriptor::TYPE_INT32 ||
	nsec->type() != google::protobuf::FieldDescriptor::TYPE_INT32)
	return false;
      Output output;
      Zpb::ArrayStorage storage{output};
      Zpb::Builder builder{storage};
      auto wire = Zpb::save(builder, dateTime);
      if (builder.failed() || !Zpb::verify<ZuDateTime>(wire) ||
	!verify(schema, reflected, wire)) return false;
      google::protobuf::DynamicMessageFactory factory;
      auto canonical = factory.GetPrototype(&type)->New();
      bool ok = canonical->ParseFromArray(wire.data(), wire.length()) &&
	canonical->GetReflection()->GetInt32(*canonical, julian) ==
	  dateTime.julian() &&
	canonical->GetReflection()->GetInt32(*canonical, sec) ==
	  dateTime.sec() &&
	canonical->GetReflection()->GetInt32(*canonical, nsec) ==
	  dateTime.nsec();
      canonical->Clear();
      auto fields = canonical->GetReflection();
      fields->SetInt32(canonical, julian, dateTime.julian());
      fields->SetInt32(canonical, sec, dateTime.sec());
      fields->SetInt32(canonical, nsec, dateTime.nsec());
      std::string reverse;
      ok &= canonical->SerializeToString(&reverse);
      ZuBSpan bytes{reinterpret_cast<const uint8_t *>(reverse.data()),
	unsigned(reverse.length())};
      ok &= verify(schema, reflected, bytes) &&
	Zpb::verify<ZuDateTime>(bytes) &&
	Zpb::load<ZuDateTime>(bytes) == dateTime;
      delete canonical;
      return ok;
    }));
}

static void testBitmapMappings()
{
  ZuTestScope(testBitmapMappings);
  ZuCheck(mappedSchema<ZuBitmap<128>>("bitmap.proto", "Bitmap",
    [&](const Schema &schema, const MsgDesc &reflected,
      const google::protobuf::Descriptor &type) {
      auto words = type.FindFieldByName("words");
      if (!words ||
	words->type() != google::protobuf::FieldDescriptor::TYPE_BYTES)
	return false;
      google::protobuf::DynamicMessageFactory factory;
      auto exchange = [&]<typename B>(const B &native,
	const std::string &raw) {
	Output output;
	Zpb::ArrayStorage storage{output};
	Zpb::Builder builder{storage};
	auto wire = Zpb::save(builder, native);
	if (builder.failed() || !Zpb::verify<B>(wire) ||
	  !verify(schema, reflected, wire)) return false;
	auto canonical = factory.GetPrototype(&type)->New();
	bool ok = canonical->ParseFromArray(wire.data(), wire.length()) &&
	  canonical->GetReflection()->GetString(*canonical, words) == raw;
	canonical->Clear();
	canonical->GetReflection()->SetString(canonical, words, raw);
	std::string reverse;
	ok &= canonical->SerializeToString(&reverse);
	ZuBSpan bytes{reinterpret_cast<const uint8_t *>(reverse.data()),
	  unsigned(reverse.length())};
	ok &= verify(schema, reflected, bytes) &&
	  Zpb::verify<B>(bytes) && Zpb::load<B>(bytes) == native;
	delete canonical;
	return ok;
      };
      ZuBitmap<128> fixed;
      fixed.set(0).set(65);
      ZtBitmap dynamic{128};
      dynamic.set(0).set(65);
      const char raw[] = {1, 0, 0, 0, 0, 0, 0, 0,
	2, 0, 0, 0, 0, 0, 0, 0};
      bool ok = exchange(fixed, std::string(raw, sizeof(raw))) &&
	exchange(dynamic, std::string(raw, sizeof(raw))) &&
	exchange(ZtBitmap{}, std::string{});
      auto invalid = factory.GetPrototype(&type)->New();
      invalid->GetReflection()->SetString(invalid, words, std::string(1, '\1'));
      std::string wrong;
      bool serialized = invalid->SerializeToString(&wrong);
      ZuBSpan bytes{reinterpret_cast<const uint8_t *>(wrong.data()),
	unsigned(wrong.length())};
      ok &= serialized && verify(schema, reflected, bytes) &&
	Zpb::verify<ZuBitmap<128>>(bytes) &&
	Zpb::verify<ZtBitmap>(bytes);
      delete invalid;
      return ok;
    }));

  ZuCheck(mappedSchema<ZmBitmap>("hwbitmap.proto", "HwBitmap",
    [&](const Schema &schema, const MsgDesc &reflected,
      const google::protobuf::Descriptor &type) {
      auto ranges = type.FindFieldByName("ranges");
      if (!ranges ||
	ranges->type() != google::protobuf::FieldDescriptor::TYPE_STRING)
	return false;
      ZmBitmap native{"3-5,7,9-"};
      Output output;
      Zpb::ArrayStorage storage{output};
      Zpb::Builder builder{storage};
      auto wire = Zpb::save(builder, native);
      if (builder.failed() || !Zpb::verify<ZmBitmap>(wire) ||
	!verify(schema, reflected, wire)) return false;
      google::protobuf::DynamicMessageFactory factory;
      auto canonical = factory.GetPrototype(&type)->New();
      bool ok = canonical->ParseFromArray(wire.data(), wire.length()) &&
	canonical->GetReflection()->GetString(*canonical, ranges) == "3-5,7,9-";
      canonical->Clear();
      auto fields = canonical->GetReflection();
      fields->SetString(canonical, ranges, "3-5,7,9-");
      std::string reverse;
      ok &= canonical->SerializeToString(&reverse);
      ZuBSpan bytes{reinterpret_cast<const uint8_t *>(reverse.data()),
	unsigned(reverse.length())};
      ok &= verify(schema, reflected, bytes) &&
	Zpb::verify<ZmBitmap>(bytes) &&
	Zpb::load<ZmBitmap>(bytes) == native;
      fields->SetString(canonical, ranges, "1-2,garbage");
      std::string invalid;
      bool serialized = canonical->SerializeToString(&invalid);
      ZuBSpan invalidBytes{reinterpret_cast<const uint8_t *>(invalid.data()),
	unsigned(invalid.length())};
      ok &= serialized && verify(schema, reflected, invalidBytes) &&
	Zpb::verify<ZmBitmap>(invalidBytes);
      delete canonical;
      return ok;
    }));
}

static void testGenerated()
{
  ZuTestScope(testGenerated);
  ProtoText text;
  ZuCheck(readProto(text));
  if (!text.length()) return;
  google::protobuf::FileDescriptorProto canonical;
  ZuCheck(parse(text, "generated.proto", canonical));
  google::protobuf::DescriptorPool pool;
  auto file = pool.BuildFile(canonical);
  ZuCheck(file);
  if (!file) return;
  auto row = file->FindMessageTypeByName("GenRow");
  auto item = file->FindMessageTypeByName("GenItem");
  ZuCheck(row && item && row->field_count() == 7 &&
    row->FindFieldByName("items")->is_map() &&
    row->FindFieldByName("blobs")->is_map() &&
    row->oneof_decl_count() == 1);
  if (!row || !item) return;
  google::protobuf::FileDescriptorSet canonicalSet;
  file->CopyTo(canonicalSet.add_file());
  std::string descriptorBytes;
  bool validDesc = canonicalSet.SerializeToString(&descriptorBytes) &&
    verifySchema({reinterpret_cast<const uint8_t *>(descriptorBytes.data()),
      unsigned(descriptorBytes.length())});
  ZuCheck(validDesc);
  if (!validDesc) return;
  Schema imported{{reinterpret_cast<const uint8_t *>(descriptorBytes.data()),
    unsigned(descriptorBytes.length())}};
  auto importedRow = imported.message(".demo.GenRow");
  ZuCheck(importedRow && importedRow->oneofs.length() == 1 &&
    imported.message(".demo.GenRow.ItemsEntry"));
  demo::GenRow native;
  native.id = 17;
  demo::GenItem mapped;
  mapped.value = 61;
  native.items.add(7, ZuMv(mapped));
  demo::GenOther selected;
  selected.text = "alt";
  native.choice.p<2>(ZuMv(selected));
  native.deltas.push(-3);
  native.flags.push(0);
  native.flags.push(2);
  const uint8_t blob[] = {'b', 0, 'y'};
  decltype(native.blobs)::Val blobValue;
  for (auto byte : blob) blobValue.push(byte);
  native.blobs.add(9, ZuMv(blobValue));
  Output nativeOutput;
  Zpb::ArrayStorage nativeStorage{nativeOutput};
  Zpb::Builder nativeBuilder{nativeStorage};
  auto nativeWire = Zpb::save(nativeBuilder, native);
  ZuCheck(!nativeBuilder.failed() && Zpb::verify<demo::GenRow>(nativeWire));
  google::protobuf::DynamicMessageFactory factory;
  auto message = factory.GetPrototype(row)->New();
  bool parsed = message->ParseFromArray(nativeWire.data(), nativeWire.length());
  ZuCheck(parsed);
  if (!parsed) { delete message; return; }
  auto fields = message->GetReflection();
  auto idField = row->FindFieldByName("id");
  auto itemsField = row->FindFieldByName("items");
  auto secondField = row->FindFieldByName("second");
  auto deltasField = row->FindFieldByName("deltas");
  auto flagsField = row->FindFieldByName("flags");
  auto blobsField = row->FindFieldByName("blobs");
  int count = fields->FieldSize(*message, itemsField);
  int blobCount = fields->FieldSize(*message, blobsField);
  ZuCheck(count == 1 && blobCount == 1);
  if (count != 1 || blobCount != 1) { delete message; return; }
  const auto &entry = fields->GetRepeatedMessage(*message, itemsField, 0);
  auto entryType = entry.GetDescriptor();
  auto entryFields = entry.GetReflection();
  const auto &entryValue = entryFields->GetMessage(entry,
    entryType->FindFieldByName("value"));
  const auto &chosen = fields->GetMessage(*message, secondField);
  const auto &blobEntry = fields->GetRepeatedMessage(*message, blobsField, 0);
  auto blobType = blobEntry.GetDescriptor();
  auto blobFields = blobEntry.GetReflection();
  ZuCheck(fields->GetUInt32(*message, idField) == 17 &&
    entryFields->GetInt32(entry, entryType->FindFieldByName("key")) == 7 &&
    entryValue.GetReflection()->GetUInt32(entryValue, item->field(0)) == 61 &&
    fields->GetOneofFieldDescriptor(*message, row->oneof_decl(0)) ==
      secondField &&
    chosen.GetReflection()->GetString(chosen,
      chosen.GetDescriptor()->FindFieldByName("text")) == "alt" &&
    fields->GetRepeatedInt32(*message, deltasField, 0) == -3 &&
    fields->FieldSize(*message, flagsField) == 2 &&
    !fields->GetRepeatedBool(*message, flagsField, 0) &&
    fields->GetRepeatedBool(*message, flagsField, 1) &&
    blobFields->GetUInt32(blobEntry,
      blobType->FindFieldByName("key")) == 9 &&
    blobFields->GetString(blobEntry,
      blobType->FindFieldByName("value")) == std::string("b\0y", 3));
  message->Clear();
  fields->SetUInt32(message, idField, 29);
  auto added = fields->AddMessage(message, itemsField);
  auto addedType = added->GetDescriptor();
  auto addedFields = added->GetReflection();
  addedFields->SetInt32(added, addedType->FindFieldByName("key"), 8);
  auto addedValue = addedFields->MutableMessage(added,
    addedType->FindFieldByName("value"));
  addedValue->GetReflection()->SetUInt32(addedValue, item->field(0), 71);
  auto firstField = row->FindFieldByName("first");
  auto first = fields->MutableMessage(message, firstField);
  first->GetReflection()->SetUInt32(first, item->field(0), 81);
  fields->AddInt32(message, deltasField, -9);
  fields->AddBool(message, flagsField, false);
  fields->AddBool(message, flagsField, true);
  auto addedBlob = fields->AddMessage(message, blobsField);
  auto addedBlobType = addedBlob->GetDescriptor();
  auto addedBlobFields = addedBlob->GetReflection();
  addedBlobFields->SetUInt32(addedBlob,
    addedBlobType->FindFieldByName("key"), 10);
  addedBlobFields->SetString(addedBlob,
    addedBlobType->FindFieldByName("value"), std::string("x\0z", 3));
  std::string canonicalWire;
  bool validWire = message->SerializeToString(&canonicalWire) &&
    Zpb::verify<demo::GenRow>({
      reinterpret_cast<const uint8_t *>(canonicalWire.data()),
      unsigned(canonicalWire.length())});
  ZuCheck(validWire);
  if (!validWire) { delete message; return; }
  auto loaded = Zpb::load<demo::GenRow>({
    reinterpret_cast<const uint8_t *>(canonicalWire.data()),
    unsigned(canonicalWire.length())});
  auto loadedBlob = loaded.blobs.find(10);
  ZuCheck(loaded.id == 29 && loaded.items.count_() == 1 &&
    loaded.items.findVal(8).value == 71 && loaded.choice.type() == 1 &&
    loaded.choice.p<1>().value == 81 && loaded.deltas.length() == 1 &&
    loaded.deltas[0] == -9 && loaded.flags.length() == 2 &&
    loaded.flags[0] == 0 && loaded.flags[1] == 1 &&
    loaded.blobs.count_() == 1 && loadedBlob &&
    loadedBlob->val().length() == 3 &&
    !memcmp(loadedBlob->val().data(), "x\0z", 3));
  ZuCheck(deltasField->is_packed());
  message->Clear();
  auto second = fields->MutableMessage(message, secondField);
  second->GetReflection()->SetString(second,
    second->GetDescriptor()->FindFieldByName("text"), "last");
  std::string switchWire;
  bool switched = message->SerializeToString(&switchWire);
  ZuCheck(switched);
  if (switched) {
    std::string mixedWire = canonicalWire + switchWire;
    mixedWire.append("\x28\x11", 2); // unpacked sint32 field 5 = -9
    message->Clear();
    bool canonicalMixed = message->ParseFromString(mixedWire) &&
      fields->GetOneofFieldDescriptor(*message, row->oneof_decl(0)) ==
	secondField &&
      fields->FieldSize(*message, deltasField) == 2 &&
      fields->GetRepeatedInt32(*message, deltasField, 0) == -9 &&
      fields->GetRepeatedInt32(*message, deltasField, 1) == -9;
    ZuCheck(canonicalMixed);
    ZuBSpan mixed{reinterpret_cast<const uint8_t *>(mixedWire.data()),
      unsigned(mixedWire.length())};
    ZuCheck(Zpb::verify<demo::GenRow>(mixed) &&
      verify(imported, *importedRow, mixed));
    if (canonicalMixed) {
      auto merged = Zpb::load<demo::GenRow>(mixed);
      ZuCheck(merged.choice.type() == 2 &&
	merged.choice.p<2>().text == "last" &&
	merged.deltas.length() == 2 && merged.deltas[0] == -9 &&
	merged.deltas[1] == -9);
    }
  }
  message->Clear();
  fields->SetUInt32(message, idField, ZuCmp<uint32_t>::null());
  std::string sentinelWire;
  bool canonicalSentinel = message->SerializeToString(&sentinelWire);
  message->Clear();
  canonicalSentinel &= message->ParseFromString(sentinelWire) &&
    fields->GetUInt32(*message, idField) == ZuCmp<uint32_t>::null();
  ZuCheck(canonicalSentinel);
  if (canonicalSentinel) {
    ZuBSpan bytes{reinterpret_cast<const uint8_t *>(sentinelWire.data()),
      unsigned(sentinelWire.length())};
    ZuCheck(verify(imported, *importedRow, bytes) &&
      Zpb::verify<demo::GenRow>(bytes));
  }
  demo::GenRow nativeNull;
  nativeNull.id = ZuCmp<uint32_t>::null();
  Output nullOutput;
  Zpb::ArrayStorage nullStorage{nullOutput};
  Zpb::Builder nullBuilder{nullStorage};
  ZuCheck(!Zpb::save(nullBuilder, nativeNull).length() &&
    !nullBuilder.failed());
  delete message;
}

static void testGeneratedMultiChoice()
{
  ZuTestScope(testGeneratedMultiChoice);
  ProtoText text;
  ZuCheck(readProto(text));
  if (!text.length()) return;
  google::protobuf::FileDescriptorProto canonical;
  ZuCheck(parse(text, "generated.proto", canonical));
  google::protobuf::DescriptorPool pool;
  auto file = pool.BuildFile(canonical);
  ZuCheck(file);
  if (!file) return;
  auto type = file->FindMessageTypeByName("GenMultiChoice");
  ZuCheck(type && type->oneof_decl_count() == 2);
  if (!type) return;
  auto aField = type->FindFieldByName("a");
  auto bField = type->FindFieldByName("b");
  auto cField = type->FindFieldByName("c");
  auto dField = type->FindFieldByName("d");
  ZuCheck(aField && bField && cField && dField);
  if (!aField || !bField || !cField || !dField) return;

  google::protobuf::FileDescriptorSet canonicalSet;
  file->CopyTo(canonicalSet.add_file());
  std::string descriptorBytes;
  bool validDesc = canonicalSet.SerializeToString(&descriptorBytes) &&
    verifySchema({reinterpret_cast<const uint8_t *>(descriptorBytes.data()),
      unsigned(descriptorBytes.length())});
  ZuCheck(validDesc);
  if (!validDesc) return;
  Schema imported{{reinterpret_cast<const uint8_t *>(descriptorBytes.data()),
    unsigned(descriptorBytes.length())}};
  auto importedType = imported.message(".demo.GenMultiChoice");
  ZuCheck(importedType && importedType->oneofs.length() == 2);
  if (!importedType) return;

  demo::GenMultiChoice native;
  demo::GenItem left;
  left.value = 7;
  native.left.p<1>(ZuMv(left));
  demo::GenOther right;
  right.text = "right";
  native.right.p<1>(ZuMv(right));
  Output output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto nativeWire = Zpb::save(builder, native);
  ZuCheck(!builder.failed() && Zpb::verify<demo::GenMultiChoice>(nativeWire) &&
    verify(imported, *importedType, nativeWire));
  google::protobuf::DynamicMessageFactory factory;
  auto message = factory.GetPrototype(type)->New();
  bool parsed = message->ParseFromArray(nativeWire.data(), nativeWire.length());
  ZuCheck(parsed);
  if (!parsed) { delete message; return; }
  auto fields = message->GetReflection();
  const auto &a = fields->GetMessage(*message, aField);
  const auto &c = fields->GetMessage(*message, cField);
  ZuCheck(fields->GetOneofFieldDescriptor(*message, type->oneof_decl(0)) ==
      aField &&
    fields->GetOneofFieldDescriptor(*message, type->oneof_decl(1)) == cField &&
    a.GetReflection()->GetUInt32(a,
      a.GetDescriptor()->FindFieldByName("value")) == 7 &&
    c.GetReflection()->GetString(c,
      c.GetDescriptor()->FindFieldByName("text")) == "right");

  message->Clear();
  auto b = fields->MutableMessage(message, bField);
  b->GetReflection()->SetString(b, b->GetDescriptor()->FindFieldByName("text"),
    "L");
  auto d = fields->MutableMessage(message, dField);
  d->GetReflection()->SetUInt32(d,
    d->GetDescriptor()->FindFieldByName("value"), 2);
  std::string canonicalWire;
  bool serialized = message->SerializeToString(&canonicalWire);
  ZuBSpan bytes{reinterpret_cast<const uint8_t *>(canonicalWire.data()),
    unsigned(canonicalWire.length())};
  ZuCheck(serialized && Zpb::verify<demo::GenMultiChoice>(bytes) &&
    verify(imported, *importedType, bytes));
  if (serialized) {
    auto loaded = Zpb::load<demo::GenMultiChoice>(bytes);
    ZuCheck(loaded.left.type() == 2 && loaded.left.p<2>().text == "L" &&
      loaded.right.type() == 2 && loaded.right.p<2>().value == 2);
  }
  const uint8_t interleaved[] = {
    0x0a, 0x02, 0x08, 0x01,
    0x1a, 0x03, 0x0a, 0x01, 'R',
    0x12, 0x03, 0x0a, 0x01, 'L',
    0x22, 0x02, 0x08, 0x02};
  ZuBSpan mixed{interleaved, sizeof(interleaved)};
  bool canonicalMixed = message->ParseFromArray(mixed.data(), mixed.length()) &&
    fields->GetOneofFieldDescriptor(*message, type->oneof_decl(0)) == bField &&
    fields->GetOneofFieldDescriptor(*message, type->oneof_decl(1)) == dField;
  ZuCheck(canonicalMixed && Zpb::verify<demo::GenMultiChoice>(mixed) &&
    verify(imported, *importedType, mixed));
  auto mixedNative = Zpb::load<demo::GenMultiChoice>(mixed);
  ZuCheck(mixedNative.left.type() == 2 &&
    mixedNative.left.p<2>().text == "L" &&
    mixedNative.right.type() == 2 &&
    mixedNative.right.p<2>().value == 2);
  delete message;
}

static void testGeneratedNumeric()
{
  ZuTestScope(testGeneratedNumeric);
  ProtoText text;
  ZuCheck(readProto(text));
  if (!text.length()) return;
  google::protobuf::FileDescriptorProto canonical;
  ZuCheck(parse(text, "generated.proto", canonical));
  google::protobuf::DescriptorPool pool;
  auto file = pool.BuildFile(canonical);
  auto type = file ? file->FindMessageTypeByName("NumericRow") : nullptr;
  ZuCheck(type && type->field_count() == 18);
  if (!type || type->field_count() != 18) return;
  auto field = [type](int n) { return type->FindFieldByNumber(n); };
  demo::NumericRow native;
  native.i32 = -37;
  native.u32 = 4000000000U;
  native.i64 = -1234567890123LL;
  native.u64 = 0xfedcba9876543210ULL;
  native.s32 = -73;
  native.s64 = -9876543210LL;
  native.f32 = 0x89abcdefU;
  native.f64 = 0x0123456789abcdefULL;
  native.sf32 = -1234567;
  native.sf64 = -1234567890123LL;
  native.r32 = 1.25f;
  native.r64 = -2.5;
  native.i32s.push(-19);
  native.u64s.push(0x123456789abcdef0ULL);
  native.s64s.push(-12345);
  native.f32s.push(0x87654321U);
  native.sf64s.push(-9876543210LL);
  native.r64s.push(3.75);
  Output output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto wire = Zpb::save(builder, native);
  ZuCheck(!builder.failed() && Zpb::verify<demo::NumericRow>(wire));
  google::protobuf::DynamicMessageFactory factory;
  auto message = factory.GetPrototype(type)->New();
  bool parsed = message->ParseFromArray(wire.data(), wire.length());
  ZuCheck(parsed);
  if (!parsed) { delete message; return; }
  auto fields = message->GetReflection();
  ZuCheck(fields->GetInt32(*message, field(1)) == native.i32 &&
    fields->GetUInt32(*message, field(2)) == native.u32 &&
    fields->GetInt64(*message, field(3)) == native.i64 &&
    fields->GetUInt64(*message, field(4)) == native.u64 &&
    fields->GetInt32(*message, field(5)) == native.s32 &&
    fields->GetInt64(*message, field(6)) == native.s64 &&
    fields->GetUInt32(*message, field(7)) == native.f32 &&
    fields->GetUInt64(*message, field(8)) == native.f64 &&
    fields->GetInt32(*message, field(9)) == native.sf32 &&
    fields->GetInt64(*message, field(10)) == native.sf64 &&
    fields->GetFloat(*message, field(11)) == native.r32 &&
    fields->GetDouble(*message, field(12)) == native.r64);
  ZuCheck(fields->FieldSize(*message, field(13)) == 1 &&
    fields->GetRepeatedInt32(*message, field(13), 0) == native.i32s[0] &&
    fields->GetRepeatedUInt64(*message, field(14), 0) == native.u64s[0] &&
    fields->GetRepeatedInt64(*message, field(15), 0) == native.s64s[0] &&
    fields->GetRepeatedUInt32(*message, field(16), 0) == native.f32s[0] &&
    fields->GetRepeatedInt64(*message, field(17), 0) == native.sf64s[0] &&
    fields->GetRepeatedDouble(*message, field(18), 0) == native.r64s[0]);
  message->Clear();
  fields->SetInt32(message, field(1), -91);
  fields->SetUInt32(message, field(2), 3900000000U);
  fields->SetInt64(message, field(3), -765432109876LL);
  fields->SetUInt64(message, field(4), 0xabcdef0123456789ULL);
  fields->SetInt32(message, field(5), -49);
  fields->SetInt64(message, field(6), -87654321);
  fields->SetUInt32(message, field(7), 0x87654321U);
  fields->SetUInt64(message, field(8), 0xfedcba9876543210ULL);
  fields->SetInt32(message, field(9), -765432);
  fields->SetInt64(message, field(10), -9876543210LL);
  fields->SetFloat(message, field(11), 2.5f);
  fields->SetDouble(message, field(12), -4.25);
  fields->AddInt32(message, field(13), -29);
  fields->AddUInt64(message, field(14), 0xabcdeffedcba0123ULL);
  fields->AddInt64(message, field(15), -54321);
  fields->AddUInt32(message, field(16), 0x12345678U);
  fields->AddInt64(message, field(17), -765432109LL);
  fields->AddDouble(message, field(18), 7.5);
  std::string reverse;
  bool serialized = message->SerializeToString(&reverse);
  ZuBSpan bytes{reinterpret_cast<const uint8_t *>(reverse.data()),
    unsigned(reverse.length())};
  ZuCheck(serialized && Zpb::verify<demo::NumericRow>(bytes));
  if (serialized && Zpb::verify<demo::NumericRow>(bytes)) {
    auto loaded = Zpb::load<demo::NumericRow>(bytes);
    ZuCheck(loaded.i32 == -91 && loaded.u32 == 3900000000U &&
      loaded.i64 == -765432109876LL &&
      loaded.u64 == 0xabcdef0123456789ULL && loaded.s32 == -49 &&
      loaded.s64 == -87654321 && loaded.f32 == 0x87654321U &&
      loaded.f64 == 0xfedcba9876543210ULL && loaded.sf32 == -765432 &&
      loaded.sf64 == -9876543210LL && loaded.r32 == 2.5f &&
      loaded.r64 == -4.25);
    ZuCheck(loaded.i32s.length() == 1 && loaded.i32s[0] == -29 &&
      loaded.u64s.length() == 1 &&
      loaded.u64s[0] == 0xabcdeffedcba0123ULL &&
      loaded.s64s.length() == 1 && loaded.s64s[0] == -54321 &&
      loaded.f32s.length() == 1 && loaded.f32s[0] == 0x12345678U &&
      loaded.sf64s.length() == 1 && loaded.sf64s[0] == -765432109LL &&
      loaded.r64s.length() == 1 && loaded.r64s[0] == 7.5);
  }
  delete message;
}

static void testDefaults()
{
  ZuTestScope(testDefaults);
  MsgDesc message;
  message.name = "Defaults";
  FieldDesc number{"number", {}, {}, 1, 1, Type::Int32};
  number.defaultValue = "-12";
  message.fields.push(number);
  FieldDesc text{"text", {}, {}, 2, 1, Type::String};
  text.defaultValue = "a\n\"b";
  message.fields.push(text);
  FieldDesc bytes{"bytes", {}, {}, 3, 1, Type::Bytes};
  bytes.defaultValue = "\\001\\377";
  message.fields.push(bytes);
  FileDesc file;
  file.name = "defaults.proto";
  file.package = "demo";
  file.syntax = "proto2";
  file.messages.push(ZuMv(message));
  DescSet descriptor;
  descriptor.files.push(ZuMv(file));
  Output output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto wire = Zpb::save(builder, descriptor);
  ZuCheck(!builder.failed() && verifySchema(wire));
  Schema schema{wire};
  ProtoText proto;
  auto source = schema.file("defaults.proto");
  ZuCheck(source && emitProto(proto, schema, *source,
      fileSyntax(*source)));
  google::protobuf::FileDescriptorProto canonical;
  ZuCheck(parse(proto, source->name, canonical));
  google::protobuf::DescriptorPool pool;
  auto built = pool.BuildFile(canonical);
  ZuCheck(built);
  if (!built) return;
  auto defaults = built->FindMessageTypeByName("Defaults");
  ZuCheck(defaults->FindFieldByName("number")->default_value_int32() == -12 &&
    defaults->FindFieldByName("text")->default_value_string() == "a\n\"b" &&
    defaults->FindFieldByName("bytes")->default_value_string() ==
      std::string("\001\377", 2));
  google::protobuf::FileDescriptorSet canonicalSet;
  built->CopyTo(canonicalSet.add_file());
  std::string canonicalBytes;
  ZuCheck(canonicalSet.SerializeToString(&canonicalBytes) &&
    verifySchema({reinterpret_cast<const uint8_t *>(canonicalBytes.data()),
      unsigned(canonicalBytes.length())}));
  Schema imported{{reinterpret_cast<const uint8_t *>(canonicalBytes.data()),
    unsigned(canonicalBytes.length())}};
  auto importedField = imported.field(*imported.message(".demo.Defaults"),
    "text");
  ZuCheck(importedField && importedField->defaultValue == "a\n\"b");
  DescSet native;
  ZuCheck(exportSchema<NativeDefaults>(native,
    "native-defaults.proto", "demo", Syntax::Proto2));
  Output nativeOutput;
  Zpb::ArrayStorage nativeStorage{nativeOutput};
  Zpb::Builder nativeBuilder{nativeStorage};
  auto nativeWire = Zpb::save(nativeBuilder, native);
  ZuCheck(!nativeBuilder.failed() && verifySchema(nativeWire));
  Schema nativeSchema{nativeWire};
  ProtoText nativeText;
  auto nativeFile = nativeSchema.file("native-defaults.proto");
  ZuCheck(nativeFile && emitProto(nativeText, nativeSchema, *nativeFile,
      fileSyntax(*nativeFile)));
  google::protobuf::FileDescriptorProto nativeCanonical;
  ZuCheck(parse(nativeText, nativeFile->name, nativeCanonical));
  auto nativeBuilt = pool.BuildFile(nativeCanonical);
  ZuCheck(nativeBuilt);
  if (!nativeBuilt) return;
  auto nativeDefaults = nativeBuilt->FindMessageTypeByName("NativeDefaults");
  ZuCheck(nativeDefaults &&
    nativeDefaults->reserved_range_count() == 1 &&
    nativeDefaults->reserved_range(0)->start == 4 &&
    nativeDefaults->reserved_range(0)->end == 5 &&
    nativeDefaults->FindFieldByName("number")->default_value_int32() == -12 &&
    nativeDefaults->FindFieldByName("text")->default_value_string() ==
      "a\n\"b" &&
    nativeDefaults->FindFieldByName("bytes")->default_value_string() ==
      std::string("\001\377", 2));
  if (!nativeDefaults) return;
  google::protobuf::DynamicMessageFactory factory;
  auto canonicalMsg = factory.GetPrototype(nativeDefaults)->New();
  auto fields = canonicalMsg->GetReflection();
  auto numberField = nativeDefaults->FindFieldByName("number");
  auto textField = nativeDefaults->FindFieldByName("text");
  auto bytesField = nativeDefaults->FindFieldByName("bytes");
  ZuCheck(!fields->HasField(*canonicalMsg, numberField) &&
    !fields->HasField(*canonicalMsg, textField) &&
    !fields->HasField(*canonicalMsg, bytesField) &&
    fields->GetInt32(*canonicalMsg, numberField) == -12 &&
    fields->GetString(*canonicalMsg, textField) == "a\n\"b" &&
    fields->GetString(*canonicalMsg, bytesField) ==
      std::string("\001\377", 2));
  fields->SetInt32(canonicalMsg, numberField, -12);
  fields->SetString(canonicalMsg, textField, "a\n\"b");
  fields->SetString(canonicalMsg, bytesField, std::string("\001\377", 2));
  std::string payload;
  bool explicitDefaults = fields->HasField(*canonicalMsg, numberField) &&
    fields->HasField(*canonicalMsg, textField) &&
    fields->HasField(*canonicalMsg, bytesField) &&
    canonicalMsg->SerializeToString(&payload);
  ZuCheck(explicitDefaults);
  if (explicitDefaults) {
    ZuBSpan bytes{reinterpret_cast<const uint8_t *>(payload.data()),
      unsigned(payload.length())};
    auto reflected = nativeSchema.message(".demo.NativeDefaults");
    ZuCheck(reflected && verify(nativeSchema, *reflected, bytes) &&
      Zpb::verify<NativeDefaults>(bytes));
    auto loaded = Zpb::load<NativeDefaults>(bytes);
    ZuCheck(loaded.number == -12 && loaded.text == "a\n\"b" &&
      loaded.bytes.length() == 2 && loaded.bytes[0] == 1 &&
      loaded.bytes[1] == 0xff);
  }
  delete canonicalMsg;
}

static void testNativeEnum()
{
  ZuTestScope(testNativeEnum);
  DescSet descriptor;
  ZuCheck(exportSchema<NativeEnumRow>(descriptor,
    "native-enum.proto", "demo", Syntax::Proto2));
  Output output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto wire = Zpb::save(builder, descriptor);
  ZuCheck(!builder.failed() && verifySchema(wire));
  Schema schema{wire};
  ProtoText proto;
  auto file = schema.file("native-enum.proto");
  ZuCheck(file && emitProto(proto, schema, *file,
      fileSyntax(*file)));
  google::protobuf::FileDescriptorProto canonical;
  ZuCheck(parse(proto, file->name, canonical));
  google::protobuf::DescriptorPool pool;
  auto built = pool.BuildFile(canonical);
  ZuCheck(built);
  if (!built) return;
  auto row = built->FindMessageTypeByName("NativeEnumRow");
  auto color = built->FindEnumTypeByName("NativeColor");
  ZuCheck(row && color && row->field(0)->enum_type() == color &&
    row->field(0)->default_value_enum()->number() == NativeColor::Blue);
  if (!row || !color) return;
  const uint8_t unknown[] = {0x08, 0x07};
  google::protobuf::DynamicMessageFactory factory;
  auto message = factory.GetPrototype(row)->New();
  auto fields = message->GetReflection();
  bool parsed = message->ParseFromArray(unknown, sizeof(unknown));
  ZuCheck(parsed && !fields->HasField(*message, row->field(0)) &&
    fields->GetEnumValue(*message, row->field(0)) == NativeColor::Blue &&
    fields->GetUnknownFields(*message).field_count() == 1);
  ZuBSpan bytes{unknown, sizeof(unknown)};
  auto reflected = schema.message(".demo.NativeEnumRow");
  ZuCheck(Zpb::verify<NativeEnumRow>(bytes) &&
    Zpb::load<NativeEnumRow>(bytes).color == 7 &&
    reflected && verify(schema, *reflected, bytes));
  delete message;
}

static void testGeneratedValues()
{
  ZuTestScope(testGeneratedValues);
  ProtoText valuesText, addressText, rowText;
  ZuCheck(readProto(valuesText, ZPB_VALUES_PROTO) &&
    readProto(addressText, ZPB_ADDRESS_PROTO) &&
    readProto(rowText, ZPB_VALUES_ROW_PROTO));
  google::protobuf::FileDescriptorProto valuesProto, addressProto, rowProto;
  ZuCheck(parse(valuesText, "zprotoc_values.proto", valuesProto) &&
    parse(addressText, "zprotoc_address.proto", addressProto) &&
    parse(rowText, "zprotoc_values_row.proto", rowProto));
  google::protobuf::DescriptorPool pool;
  auto valuesFile = pool.BuildFile(valuesProto);
  auto addressFile = pool.BuildFile(addressProto);
  auto rowFile = valuesFile && addressFile ? pool.BuildFile(rowProto) : nullptr;
  ZuCheck(valuesFile && addressFile && rowFile);
  if (!rowFile) return;
  auto type = rowFile->FindMessageTypeByName("ValueRow");
  auto signedWide = type ? type->FindFieldByName("signedWide") : nullptr;
  auto fixed = type ? type->FindFieldByName("fixed") : nullptr;
  auto ip = type ? type->FindFieldByName("ip") : nullptr;
  auto bitmap = type ? type->FindFieldByName("bitmap") : nullptr;
  auto hwBitmap = type ? type->FindFieldByName("hwBitmap") : nullptr;
  auto bitmaps = type ? type->FindFieldByName("bitmaps") : nullptr;
  auto hwBitmaps = type ? type->FindFieldByName("hwBitmaps") : nullptr;
  auto ips = type ? type->FindFieldByName("ips") : nullptr;
  auto mappedIPs = type ? type->FindFieldByName("ipsByName") : nullptr;
  auto bitsByName = type ? type->FindFieldByName("bitsByName") : nullptr;
  auto hwByName = type ? type->FindFieldByName("hwByName") : nullptr;
  auto signedValues = type ? type->FindFieldByName("signedValues") : nullptr;
  auto unsignedValues = type ? type->FindFieldByName("unsignedValues") : nullptr;
  auto fixedValues = type ? type->FindFieldByName("fixedValues") : nullptr;
  auto decimalValues = type ? type->FindFieldByName("decimalValues") : nullptr;
  auto timeValues = type ? type->FindFieldByName("timeValues") : nullptr;
  auto dateValues = type ? type->FindFieldByName("dateValues") : nullptr;
  ZuCheck(type && signedWide && fixed && ip && bitmap && hwBitmap &&
    bitmaps && hwBitmaps && ips &&
    mappedIPs && mappedIPs->is_map() && bitsByName &&
    bitsByName->is_map() && hwByName && hwByName->is_map() &&
    signedValues && unsignedValues &&
    fixedValues && decimalValues && timeValues && dateValues);
  if (!signedWide || !mappedIPs || !bitsByName || !hwByName ||
    !fixed || !ip || !bitmap ||
    !hwBitmap || !bitmaps || !hwBitmaps || !ips ||
    !signedValues || !unsignedValues ||
    !fixedValues || !decimalValues || !timeValues || !dateValues) return;

  google::protobuf::FileDescriptorSet dependencies;
  *dependencies.add_file() = valuesProto;
  *dependencies.add_file() = addressProto;
  std::string dependencyBytes;
  bool encoded = dependencies.SerializeToString(&dependencyBytes);
  ZuBSpan dependencyWire{
    reinterpret_cast<const uint8_t *>(dependencyBytes.data()),
    unsigned(dependencyBytes.length())};
  ZuCheck(encoded && verifySchema(dependencyWire));
  if (!encoded || !verifySchema(dependencyWire)) return;
  auto nativeSet = Zpb::load<DescSet>(dependencyWire);
  bool nativeExport = exportSchema<demo::ValueRow>(nativeSet,
    "native_values_row.proto", "demo", Syntax::Proto3);
  ZuCheck(nativeExport);
  if (!nativeExport) return;
  ZuCheck(nativeSet.files.length() == 3 &&
    nativeSet.files[2].dependencies.length() == 2);
  Output exportedBytes;
  Zpb::ArrayStorage exportedStorage{exportedBytes};
  Zpb::Builder exportedBuilder{exportedStorage};
  auto exportedWire = Zpb::save(exportedBuilder, nativeSet);
  ZuCheck(!exportedBuilder.failed() && verifySchema(exportedWire));
  if (exportedBuilder.failed() || !verifySchema(exportedWire)) return;
  Schema exported{exportedWire};
  auto exportedFile = exported.file("native_values_row.proto");
  ProtoText exportedText;
  bool emitted = exportedFile &&
    emitProto(exportedText, exported, *exportedFile,
      fileSyntax(*exportedFile));
  ZuCheck(emitted);
  if (!emitted) return;
  google::protobuf::FileDescriptorProto exportedProto;
  bool parsedExport = parse(exportedText, "native_values_row.proto",
    exportedProto);
  ZuCheck(parsedExport);
  if (!parsedExport) return;
  google::protobuf::DescriptorPool exportedPool;
  auto builtValues = exportedPool.BuildFile(valuesProto);
  auto builtAddress = exportedPool.BuildFile(addressProto);
  auto builtRow = builtValues && builtAddress ?
    exportedPool.BuildFile(exportedProto) : nullptr;
  ZuCheck(builtValues && builtAddress && builtRow &&
    builtRow->FindMessageTypeByName("ValueRow"));
  if (!builtRow) return;
  auto builtType = builtRow->FindMessageTypeByName("ValueRow");
  auto builtIP = builtType ? builtType->FindFieldByName("ip") : nullptr;
  auto builtBitmap = builtType ? builtType->FindFieldByName("bitmap") : nullptr;
  auto builtHw = builtType ? builtType->FindFieldByName("hwBitmap") : nullptr;
  ZuCheck(builtIP && builtBitmap && builtHw &&
    builtIP->message_type()->full_name() == "Zi.IP" &&
    builtBitmap->message_type()->full_name() == "Zf.Bitmap" &&
    builtHw->message_type()->full_name() == "Zf.HwBitmap");

  demo::ValueRow native;
  native.signedWide = -42;
  native.fixed = ZuFixed{-12345, 2};
  native.signedValues.push(-123);
  native.unsignedValues.push((uint128_t(1) << 80) | 7);
  native.fixedValues.push(ZuFixed{17, 1});
  native.decimalValues.push(ZuDecimal{"-12.25"});
  native.timeValues.push(ZuTime{456, 789});
  native.dateValues.push(ZuDateTime{2025, 1, 2, 3, 4, 5, 6});
  native.ip = ZiIP{"127.0.0.1"};
  native.ips.push(ZiIP{"::1"});
  native.ipsByName.add("loop", native.ip);
  native.bitmap = ZtBitmap{128};
  native.bitmap.set(0).set(65);
  native.bitmaps.push(ZtBitmap{128});
  native.bitmaps[0].set(1);
  native.bitsByName.add("bits", native.bitmap);
  native.hwBitmap = ZmBitmap{"3-5,7,9-"};
  native.hwBitmaps.push(ZmBitmap{"5,11"});
  native.hwByName.add("hw", native.hwBitmap);
  Output output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto wire = Zpb::save(builder, native);
  ZuCheck(!builder.failed() && Zpb::verify<demo::ValueRow>(wire));
  google::protobuf::DynamicMessageFactory factory;
  auto canonical = factory.GetPrototype(type)->New();
  bool parsed = canonical->ParseFromArray(wire.data(), wire.length());
  ZuCheck(parsed);
  if (!parsed) { delete canonical; return; }
  auto fields = canonical->GetReflection();
  const auto &wide = fields->GetMessage(*canonical, signedWide);
  const auto &number = fields->GetMessage(*canonical, fixed);
  const auto &addr = fields->GetMessage(*canonical, ip);
  const auto &bits = fields->GetMessage(*canonical, bitmap);
  const auto &ranges = fields->GetMessage(*canonical, hwBitmap);
  auto wideFields = wide.GetReflection();
  auto numberFields = number.GetReflection();
  auto addrFields = addr.GetReflection();
  auto bitsFields = bits.GetReflection();
  auto rangeFields = ranges.GetReflection();
  auto words = bitsFields->GetString(bits,
    bits.GetDescriptor()->FindFieldByName("words"));
  auto rawAddr = addrFields->GetString(addr,
    addr.GetDescriptor()->FindFieldByName("addr"));
  bool vectorSizes = fields->FieldSize(*canonical, signedValues) == 1 &&
    fields->FieldSize(*canonical, unsignedValues) == 1 &&
    fields->FieldSize(*canonical, fixedValues) == 1 &&
    fields->FieldSize(*canonical, decimalValues) == 1 &&
    fields->FieldSize(*canonical, timeValues) == 1 &&
    fields->FieldSize(*canonical, dateValues) == 1;
  ZuCheck(vectorSizes);
  if (!vectorSizes) { delete canonical; return; }
  const auto &signedValue = fields->GetRepeatedMessage(*canonical,
    signedValues, 0);
  const auto &unsignedValue = fields->GetRepeatedMessage(*canonical,
    unsignedValues, 0);
  const auto &fixedValue = fields->GetRepeatedMessage(*canonical,
    fixedValues, 0);
  const auto &decimalValue = fields->GetRepeatedMessage(*canonical,
    decimalValues, 0);
  const auto &timeValue = fields->GetRepeatedMessage(*canonical,
    timeValues, 0);
  const auto &dateValue = fields->GetRepeatedMessage(*canonical,
    dateValues, 0);
  ZuCheck(wideFields->GetUInt64(wide,
      wide.GetDescriptor()->FindFieldByName("h")) == UINT64_MAX &&
    wideFields->GetUInt64(wide,
      wide.GetDescriptor()->FindFieldByName("l")) == uint64_t(-42) &&
    numberFields->GetInt64(number,
      number.GetDescriptor()->FindFieldByName("mantissa")) == -12345 &&
    numberFields->GetUInt32(number,
      number.GetDescriptor()->FindFieldByName("ndp")) == 2 &&
    rawAddr == std::string("\x7f\0\0\x01", 4) &&
    words.length() == 16 && uint8_t(words[0]) == 1 &&
    uint8_t(words[8]) == 2 &&
    rangeFields->GetString(ranges,
      ranges.GetDescriptor()->FindFieldByName("ranges")) == "3-5,7,9-" &&
    fields->FieldSize(*canonical, bitmaps) == 1 &&
    fields->FieldSize(*canonical, hwBitmaps) == 1 &&
    fields->FieldSize(*canonical, ips) == 1 &&
    fields->FieldSize(*canonical, mappedIPs) == 1 &&
    fields->FieldSize(*canonical, bitsByName) == 1 &&
    fields->FieldSize(*canonical, hwByName) == 1);
  auto signedFields = signedValue.GetReflection();
  auto unsignedFields = unsignedValue.GetReflection();
  auto fixedVectorFields = fixedValue.GetReflection();
  auto decimalFields = decimalValue.GetReflection();
  auto timeFields = timeValue.GetReflection();
  auto dateFields = dateValue.GetReflection();
  uint128_t unsignedBits = native.unsignedValues[0];
  uint128_t decimalBits = uint128_t(native.decimalValues[0].value);
  ZuCheck(signedFields->GetUInt64(signedValue,
      signedValue.GetDescriptor()->FindFieldByName("h")) == UINT64_MAX &&
    signedFields->GetUInt64(signedValue,
      signedValue.GetDescriptor()->FindFieldByName("l")) == uint64_t(-123) &&
    unsignedFields->GetUInt64(unsignedValue,
      unsignedValue.GetDescriptor()->FindFieldByName("h")) ==
      uint64_t(unsignedBits >> 64) &&
    unsignedFields->GetUInt64(unsignedValue,
      unsignedValue.GetDescriptor()->FindFieldByName("l")) ==
      uint64_t(unsignedBits) &&
    fixedVectorFields->GetInt64(fixedValue,
      fixedValue.GetDescriptor()->FindFieldByName("mantissa")) == 17 &&
    fixedVectorFields->GetUInt32(fixedValue,
      fixedValue.GetDescriptor()->FindFieldByName("ndp")) == 1 &&
    decimalFields->GetUInt64(decimalValue,
      decimalValue.GetDescriptor()->FindFieldByName("h")) ==
      uint64_t(decimalBits >> 64) &&
    decimalFields->GetUInt64(decimalValue,
      decimalValue.GetDescriptor()->FindFieldByName("l")) ==
      uint64_t(decimalBits) &&
    timeFields->GetInt64(timeValue,
      timeValue.GetDescriptor()->FindFieldByName("sec")) == 456 &&
    timeFields->GetInt32(timeValue,
      timeValue.GetDescriptor()->FindFieldByName("nsec")) == 789 &&
    dateFields->GetInt32(dateValue,
      dateValue.GetDescriptor()->FindFieldByName("julian")) ==
      native.dateValues[0].julian() &&
    dateFields->GetInt32(dateValue,
      dateValue.GetDescriptor()->FindFieldByName("sec")) ==
      native.dateValues[0].sec() &&
    dateFields->GetInt32(dateValue,
      dateValue.GetDescriptor()->FindFieldByName("nsec")) ==
      native.dateValues[0].nsec());

  auto mutableFixed = fields->MutableMessage(canonical, fixed);
  auto fixedFields = mutableFixed->GetReflection();
  fixedFields->SetInt64(mutableFixed,
    mutableFixed->GetDescriptor()->FindFieldByName("mantissa"), 6789);
  fixedFields->SetUInt32(mutableFixed,
    mutableFixed->GetDescriptor()->FindFieldByName("ndp"), 1);
  auto mutableIP = fields->MutableMessage(canonical, ip);
  std::string v6(16, '\0');
  v6[15] = 1;
  mutableIP->GetReflection()->SetString(mutableIP,
    mutableIP->GetDescriptor()->FindFieldByName("addr"), v6);
  auto addedIP = fields->AddMessage(canonical, ips);
  addedIP->GetReflection()->SetString(addedIP,
    addedIP->GetDescriptor()->FindFieldByName("addr"), v6);
  auto addedIPEntry = fields->AddMessage(canonical, mappedIPs);
  auto ipEntryFields = addedIPEntry->GetReflection();
  ipEntryFields->SetString(addedIPEntry,
    addedIPEntry->GetDescriptor()->FindFieldByName("key"), "extra");
  auto addedIPValue = ipEntryFields->MutableMessage(addedIPEntry,
    addedIPEntry->GetDescriptor()->FindFieldByName("value"));
  addedIPValue->GetReflection()->SetString(addedIPValue,
    addedIPValue->GetDescriptor()->FindFieldByName("addr"), v6);
  auto mutableHw = fields->MutableMessage(canonical, hwBitmap);
  mutableHw->GetReflection()->SetString(mutableHw,
    mutableHw->GetDescriptor()->FindFieldByName("ranges"), "2,4");
  auto addedBitmap = fields->AddMessage(canonical, bitmaps);
  std::string secondWords(16, '\0');
  secondWords[0] = 4;
  addedBitmap->GetReflection()->SetString(addedBitmap,
    addedBitmap->GetDescriptor()->FindFieldByName("words"), secondWords);
  auto addedHw = fields->AddMessage(canonical, hwBitmaps);
  addedHw->GetReflection()->SetString(addedHw,
    addedHw->GetDescriptor()->FindFieldByName("ranges"), "8-10");
  auto addedBitsEntry = fields->AddMessage(canonical, bitsByName);
  auto bitsEntryFields = addedBitsEntry->GetReflection();
  bitsEntryFields->SetString(addedBitsEntry,
    addedBitsEntry->GetDescriptor()->FindFieldByName("key"), "extra");
  auto addedBits = bitsEntryFields->MutableMessage(addedBitsEntry,
    addedBitsEntry->GetDescriptor()->FindFieldByName("value"));
  std::string extraWords(16, '\0');
  extraWords[0] = 8;
  addedBits->GetReflection()->SetString(addedBits,
    addedBits->GetDescriptor()->FindFieldByName("words"), extraWords);
  auto addedHwEntry = fields->AddMessage(canonical, hwByName);
  auto hwEntryFields = addedHwEntry->GetReflection();
  hwEntryFields->SetString(addedHwEntry,
    addedHwEntry->GetDescriptor()->FindFieldByName("key"), "extra");
  auto addedHwValue = hwEntryFields->MutableMessage(addedHwEntry,
    addedHwEntry->GetDescriptor()->FindFieldByName("value"));
  addedHwValue->GetReflection()->SetString(addedHwValue,
    addedHwValue->GetDescriptor()->FindFieldByName("ranges"), "12-14");
  auto addWords = [&](const google::protobuf::FieldDescriptor *field,
    uint64_t h, uint64_t l) {
    auto value = fields->AddMessage(canonical, field);
    auto valueFields = value->GetReflection();
    valueFields->SetUInt64(value,
      value->GetDescriptor()->FindFieldByName("h"), h);
    valueFields->SetUInt64(value,
      value->GetDescriptor()->FindFieldByName("l"), l);
  };
  int128_t secondSigned = -321;
  uint128_t secondUnsigned = (uint128_t(2) << 80) | 11;
  ZuDecimal secondDecimal{"5.5"};
  uint128_t secondDecimalBits = uint128_t(secondDecimal.value);
  addWords(signedValues, uint64_t(uint128_t(secondSigned) >> 64),
    uint64_t(secondSigned));
  addWords(unsignedValues, uint64_t(secondUnsigned >> 64),
    uint64_t(secondUnsigned));
  addWords(decimalValues, uint64_t(secondDecimalBits >> 64),
    uint64_t(secondDecimalBits));
  auto secondFixed = fields->AddMessage(canonical, fixedValues);
  auto secondFixedFields = secondFixed->GetReflection();
  secondFixedFields->SetInt64(secondFixed,
    secondFixed->GetDescriptor()->FindFieldByName("mantissa"), 27);
  secondFixedFields->SetUInt32(secondFixed,
    secondFixed->GetDescriptor()->FindFieldByName("ndp"), 2);
  auto secondTime = fields->AddMessage(canonical, timeValues);
  auto secondTimeFields = secondTime->GetReflection();
  secondTimeFields->SetInt64(secondTime,
    secondTime->GetDescriptor()->FindFieldByName("sec"), 654);
  secondTimeFields->SetInt32(secondTime,
    secondTime->GetDescriptor()->FindFieldByName("nsec"), 321);
  ZuDateTime secondDate{2026, 4, 5, 6, 7, 8, 9};
  auto secondDateValue = fields->AddMessage(canonical, dateValues);
  auto secondDateFields = secondDateValue->GetReflection();
  secondDateFields->SetInt32(secondDateValue,
    secondDateValue->GetDescriptor()->FindFieldByName("julian"),
    secondDate.julian());
  secondDateFields->SetInt32(secondDateValue,
    secondDateValue->GetDescriptor()->FindFieldByName("sec"),
    secondDate.sec());
  secondDateFields->SetInt32(secondDateValue,
    secondDateValue->GetDescriptor()->FindFieldByName("nsec"),
    secondDate.nsec());
  std::string reverse;
  bool serialized = canonical->SerializeToString(&reverse);
  ZuBSpan bytes{reinterpret_cast<const uint8_t *>(reverse.data()),
    unsigned(reverse.length())};
  ZuCheck(serialized && Zpb::verify<demo::ValueRow>(bytes));
  if (serialized && Zpb::verify<demo::ValueRow>(bytes)) {
    auto loaded = Zpb::load<demo::ValueRow>(bytes);
    ZuFixed reverseFixed{6789, 1};
    ZuFixed secondFixedNative{27, 2};
    ZuTime secondTimeNative{654, 321};
    ZiIP reverseIP{"::1"};
    ZmBitmap reverseHw{"2,4"};
    ZtBitmap secondBitmap{128};
    secondBitmap.set(2);
    ZmBitmap secondHw{"8-10"};
    ZtBitmap extraBitmap{128};
    extraBitmap.set(3);
    ZmBitmap extraHw{"12-14"};
    ZuCheck(loaded.signedWide == native.signedWide &&
      loaded.fixed == reverseFixed && loaded.ip == reverseIP &&
      loaded.bitmap == native.bitmap &&
      loaded.hwBitmap == reverseHw &&
      loaded.bitmaps.length() == 2 &&
      loaded.bitmaps[0] == native.bitmaps[0] &&
      loaded.bitmaps[1] == secondBitmap &&
      loaded.hwBitmaps.length() == 2 &&
      loaded.hwBitmaps[0] == native.hwBitmaps[0] &&
      loaded.hwBitmaps[1] == secondHw &&
      loaded.bitsByName.findVal("bits") == native.bitmap &&
      loaded.bitsByName.findVal("extra") == extraBitmap &&
      loaded.hwByName.findVal("hw") == native.hwBitmap &&
      loaded.hwByName.findVal("extra") == extraHw &&
      loaded.signedValues.length() == 2 &&
      loaded.signedValues[0] == native.signedValues[0] &&
      loaded.signedValues[1] == secondSigned &&
      loaded.unsignedValues.length() == 2 &&
      loaded.unsignedValues[0] == native.unsignedValues[0] &&
      loaded.unsignedValues[1] == secondUnsigned &&
      loaded.fixedValues.length() == 2 &&
      loaded.fixedValues[0] == native.fixedValues[0] &&
      loaded.fixedValues[1] == secondFixedNative &&
      loaded.decimalValues.length() == 2 &&
      loaded.decimalValues[0] == native.decimalValues[0] &&
      loaded.decimalValues[1] == secondDecimal &&
      loaded.timeValues.length() == 2 &&
      loaded.timeValues[0] == native.timeValues[0] &&
      loaded.timeValues[1] == secondTimeNative &&
      loaded.dateValues.length() == 2 &&
      loaded.dateValues[0] == native.dateValues[0] &&
      loaded.dateValues[1] == secondDate &&
      loaded.ips.length() == 2 && loaded.ips[0] == native.ips[0] &&
      loaded.ips[1] == reverseIP &&
      loaded.ipsByName.findVal("loop") == native.ip &&
      loaded.ipsByName.findVal("extra") == reverseIP);
  }
  delete canonical;
}

int main(int argc, char **argv)
{
  if (argc != 1) {
    if (argc != 4) {
      std::cerr << "Usage: zpbinterop [COUNT "
	"small|nested|repeated|large parse|serialize]\n";
      return 1;
    }
    struct Workloads {
      using Keys = ZuStringTL<"small", "nested", "repeated", "large">;
    };
    struct Operations {
      using Keys = ZuStringTL<"parse", "serialize">;
    };
    static constexpr auto workloads = ZuMatcher<Workloads>();
    static constexpr auto operations = ZuMatcher<Operations>();
    unsigned count = ZuBox<unsigned>{argv[1]};
    int workload = workloads.exact(argv[2]);
    int operation = operations.exact(argv[3]);
    if (!count || workload < 0 || operation < 0) return 1;
    std::cout << "message,operation,encoded_bytes,count,seconds,checksum\n";
    bool ok = benchmark(count, workload, operation);
    google::protobuf::ShutdownProtobufLibrary();
    return ok ? 0 : 1;
  }
  ZuTestMain();
  ZuTestCall(testDelimited);
  ZuTestCall(testInterop);
  ZuTestCall(testNativeExport);
  ZuTestCall(testFacetInterop);
  ZuTestCall(testDiscardedChoice);
  ZuTestCall(testNativeMapped);
  ZuTestCall(testNativeEnumImport);
  ZuTestCall(testScalarVocabulary);
  ZuTestCall(testBoolVecInterop);
  ZuTestCall(testCStringInterop);
  ZuTestCall(testWireNumbers);
  ZuTestCall(testIPMapping);
  ZuTestCall(testFixedMapping);
  ZuTestCall(testWordMappings);
  ZuTestCall(testTemporalMappings);
  ZuTestCall(testBitmapMappings);
  ZuTestCall(testGenerated);
  ZuTestCall(testGeneratedMultiChoice);
  ZuTestCall(testGeneratedNumeric);
  ZuTestCall(testDefaults);
  ZuTestCall(testNativeEnum);
  ZuTestCall(testCompatibility);
  ZuTestCall(testRecursive);
  ZuTestCall(testRequiredSyntax);
  ZuTestCall(testProto3Optional);
  ZuTestCall(testGeneratedValues);
  ZuTestCall(testBenchReadiness);
  google::protobuf::ShutdownProtobufLibrary();
}
