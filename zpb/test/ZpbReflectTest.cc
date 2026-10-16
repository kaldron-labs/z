//  -*- mode:c++; indent-tabs-mode:t; tab-width=8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmRBTree.hh>
#include <zlib/ZpbReflect.hh>

#include <stdio.h>

using namespace Zpb::Reflect;

ZuAssert((ZuIsSame<ZmNHeapID_<MsgDesc>, ZuStringT<"Zpb.Reflect.MsgDesc">>{}));

struct NativeRow { uint32_t id = 0; };
ZfStruct(, (NativeRow, PB), (id,, UInt32));
Zpb::AsMessage<"demo.Row"> Zpb_Fmt(NativeRow *);
struct ExportRow {
  uint32_t first = 0, last = 0;
  uint8_t small = 0;
  int8_t signed_ = 0;
};
ZfStruct(, (ExportRow, PB),
  (first,,			UInt32),
  ((retired, Skip, int),,	Int32),
  (last,,			UInt32),
  (small,,			UInt8),
  (signed_,,			Int8));
Zpb::AsMessage<"demo.ExportRow"> Zpb_Fmt(ExportRow *);
ZuDerive(ExportBools,
  (ZtArray<uint8_t, ZtArrayHeapID<"ZpbReflectTest.ExportBools">>));
struct ExportBoolRow { ExportBools flags; };
ZfStruct(, (ExportBoolRow, PB),
  (flags, (PB::BoolVec), UInt8Vec));
Zpb::AsMessage<"demo.ExportBoolRow"> Zpb_Fmt(ExportBoolRow *);
struct ExportPackedBoolRow { ExportBools flags; };
ZfStruct(, (ExportPackedBoolRow, PB),
  (flags, (PB::BoolVec, PB::Packed), UInt8Vec));
Zpb::AsMessage<"demo.ExportPackedBoolRow">
  Zpb_Fmt(ExportPackedBoolRow *);
struct ExportDefaults {
  int32_t number;
  bool enabled;
  ZuCSpan text;
  ZuBSpan raw;
  float gain;
};
ZfStruct(, (ExportDefaults, PB),
  (number, (Deflt<-12>),	Int32),
  (enabled, (Deflt<true>),	Bool),
  (text, (Deflt<"a\n\"b"_z>),	String),
  (raw, (Deflt<"\001\377"_z>),	Bytes),
  (gain, (Deflt<1.25f>),	Float));
Zpb::AsMessage<"demo.ExportDefaults"> Zpb_Fmt(ExportDefaults *);
struct ExportRequired { uint32_t id = 0; };
ZfStruct(, (ExportRequired, PB), (id, (Required), UInt32));
Zpb::AsMessage<"demo.ExportRequired"> Zpb_Fmt(ExportRequired *);
ZtEnumNS(, ExportState, int32_t, None, Ready);
ZtEnumImplNS(ExportState);
ZuDerive(ExportStates,
  (ZtArray<int32_t, ZtArrayHeapID<"ZpbReflectTest.ExportStates">>));
struct ExportEnumRow { int32_t state = ExportState::Ready; ExportStates states; };
ZfStruct(, (ExportEnumRow, PB),
  (state, (Enum<ExportState::Map>, Deflt<ExportState::Ready>),	Int32),
  (states, (Enum<ExportState::Map>),				Int32Vec));
Zpb::AsMessage<"demo.ExportEnumRow"> Zpb_Fmt(ExportEnumRow *);
ZuDerive(ExportEnumMap, (ZmRBTreeKV<int32_t, int32_t,
  ZmRBTreeHeapID<"ZpbReflectTest.ExportEnumMap">>));
Zpb::AsMap<"demo.ExportEnumMap", ZfFieldTC::Int32, ZfFieldTC::Int32,
  ZuTypeList<>, ZuTypeList<ZuFieldProp::Enum<ExportState::Map>>>
  Zpb_Fmt(ExportEnumMap *);
struct ExportEnumMapRow { ExportEnumMap colors; };
ZfStruct(, (ExportEnumMapRow, PB), (colors,, UDT));
Zpb::AsMessage<"demo.ExportEnumMapRow"> Zpb_Fmt(ExportEnumMapRow *);
ZuStructFacet(ExportFacet);
ZfStructRender(, ExportRow, ExportFacet, (last, PB::ID<"renamed">));
struct ExportA { uint32_t value = 0; };
ZfStruct(, (ExportA, PB), (value,, UInt32));
Zpb::AsMessage<"demo.ExportA"> Zpb_Fmt(ExportA *);
struct ExportB { bool flag = false; };
ZfStruct(, (ExportB, PB), (flag,, Bool));
Zpb::AsMessage<"demo.ExportB"> Zpb_Fmt(ExportB *);
ZuDerive(ExportChoice, (ZuUnion<void, ExportA, ExportB>));
Zpb::AsUnion<"demo.ExportChoice",
  Zpb::Alt<"first", 4, ExportA>, Zpb::Alt<"second", 6, ExportB>>
  Zpb_Fmt(ExportChoice *);
struct ExportChoiceRow { uint32_t id = 0; ExportChoice choice; };
ZfStruct(, (ExportChoiceRow, PB), (id,, UInt32), (choice,, UDT));
Zpb::AsMessage<"demo.ExportChoiceRow"> Zpb_Fmt(ExportChoiceRow *);
struct ExportChoiceNameRow { uint32_t first = 0; ExportChoice choice; };
ZfStruct(, (ExportChoiceNameRow, PB),
  (first,, UInt32), (choice,, UDT));
Zpb::AsMessage<"demo.ExportChoiceNameRow">
  Zpb_Fmt(ExportChoiceNameRow *);
struct ExportGroupNameRow { uint32_t id = 0; ExportChoice choice; };
ZfStruct(, (ExportGroupNameRow, PB),
  (id, (PB::ID<"choice">), UInt32), (choice,, UDT));
Zpb::AsMessage<"demo.ExportGroupNameRow">
  Zpb_Fmt(ExportGroupNameRow *);
ZuDerive(ExportMap, (ZmRBTreeKV<int32_t, int32_t,
  ZmRBTreeHeapID<"ZpbReflectTest.ExportMap">>));
Zpb::AsMap<"demo.ExportMap", ZfFieldTC::Int32, ZfFieldTC::Int32>
  Zpb_Fmt(ExportMap *);
struct ExportMapRow { ExportMap entries; };
ZfStruct(, (ExportMapRow, PB), (entries,, UDT));
Zpb::AsMessage<"demo.ExportMapRow"> Zpb_Fmt(ExportMapRow *);
ZuDerive(ExportChildMap, (ZmRBTreeKV<int32_t, ExportA,
  ZmRBTreeHeapID<"ZpbReflectTest.ExportChildMap">>));
Zpb::AsMap<"demo.ExportChildMap", ZfFieldTC::Int32, ZfFieldTC::UDT>
  Zpb_Fmt(ExportChildMap *);
struct ExportChildMapRow { ExportChildMap entries; };
ZfStruct(, (ExportChildMapRow, PB), (entries,, UDT));
Zpb::AsMessage<"demo.ExportChildMapRow">
  Zpb_Fmt(ExportChildMapRow *);
ZuDerive(ExportArray,
  (ZtArray<ExportA, ZtArrayHeapID<"ZpbReflectTest.ExportArray">>));
Zpb::AsArray<"demo.ExportArray"> Zpb_Fmt(ExportArray *);
struct ExportArrayRow { ExportArray items; };
ZfStruct(, (ExportArrayRow, PB), (items,, UDT));
Zpb::AsMessage<"demo.ExportArrayRow"> Zpb_Fmt(ExportArrayRow *);
ZuDerive(ExportFixedVec,
  (ZtArray<ZuFixed, ZtArrayHeapID<"ZpbReflectTest.ExportFixedVec">>));
struct ExportMappedRow { ZuFixed amount; ExportFixedVec amounts; };
ZfStruct(, (ExportMappedRow, PB),
  (amount,, Fixed), (amounts,, FixedVec));
Zpb::AsMessage<"demo.ExportMappedRow"> Zpb_Fmt(ExportMappedRow *);
ZuDerive(ExportFixedMap, (ZmRBTreeKV<int32_t, ZuFixed,
  ZmRBTreeHeapID<"ZpbReflectTest.ExportFixedMap">>));
Zpb::AsMap<"demo.ExportFixedMap", ZfFieldTC::Int32, ZfFieldTC::Fixed>
  Zpb_Fmt(ExportFixedMap *);
struct ExportFixedMapRow { ExportFixedMap amounts; };
ZfStruct(, (ExportFixedMapRow, PB), (amounts,, UDT));
Zpb::AsMessage<"demo.ExportFixedMapRow">
  Zpb_Fmt(ExportFixedMapRow *);
struct ExportAppValue { uint32_t value = 0; };
struct ExportAppMap {
  using T = ExportA;
  static T save(const ExportAppValue &v) { return {v.value}; }
  static ExportAppValue load(const T &v) { return {v.value}; }
};
ExportAppMap ZpbTransform(ExportAppValue *);
struct ExportAppRow { ExportAppValue item; };
ZfStruct(, (ExportAppRow, PB), (item,, UDT));
Zpb::AsMessage<"demo.ExportAppRow"> Zpb_Fmt(ExportAppRow *);
struct DepMessage { uint32_t id = 0; };
ZfStruct(, (DepMessage, PB), (id,, UInt32));
Zpb::AsMessage<"dep.DepMessage"> Zpb_Fmt(DepMessage *);
struct DepStateMap : ExportState::Map {
  static constexpr const auto &id() { return "dep.ExportState"; }
};
struct DepBundle { DepMessage child; int32_t state = ExportState::None; };
ZfStruct(, (DepBundle, PB),
  (child,, UDT), (state, (Enum<DepStateMap>), Int32));
Zpb::AsMessage<"dep.DepBundle"> Zpb_Fmt(DepBundle *);
struct CrossRow { DepMessage child; int32_t state = ExportState::None; };
ZfStruct(, (CrossRow, PB),
  (child,, UDT), (state, (Enum<DepStateMap>), Int32));
Zpb::AsMessage<"demo.CrossRow"> Zpb_Fmt(CrossRow *);
struct BadStateMap : DepStateMap {
  template <typename Fn> static void all(Fn fn) {
    fn("None", 0);
    fn("Ready", 2);
  }
};
struct BadCrossRow { int32_t state = 0; };
ZfStruct(, (BadCrossRow, PB),
  (state, (Enum<BadStateMap>), Int32));
Zpb::AsMessage<"demo.BadCrossRow"> Zpb_Fmt(BadCrossRow *);
ZuDerive(Output, (ZtArray<uint8_t, ZtArrayHeapID<"ZpbReflectTest.Output">>));
ZuDerive(Events, (ZtArray<Value, ZtArrayHeapID<"ZpbReflectTest.Events">>));

static void testDescriptor()
{
  ZuTestScope(testDescriptor);
  // FileDescriptorSet for proto3 package demo, message Row { uint32 id = 1; }.
  // This fixture is encoded independently of Zpb's serializer.
  const uint8_t bytes[] = {
    0x0a, 0x30,
      0x0a, 0x09, 'r', 'o', 'w', '.', 'p', 'r', 'o', 't', 'o',
      0x12, 0x04, 'd', 'e', 'm', 'o',
      0x22, 0x15,
	0x0a, 0x03, 'R', 'o', 'w',
	0x12, 0x0e,
	  0x0a, 0x02, 'i', 'd',
	  0x18, 0x01, 0x20, 0x01, 0x28, 0x0d,
	  0x52, 0x02, 'i', 'd',
      0x62, 0x06, 'p', 'r', 'o', 't', 'o', '3'};
  ZuCheck(verifySchema({bytes, sizeof(bytes)}));
  const uint8_t mapOptionWire[] = {0x38, 0x01};
  ZuCheck(Zpb::verify<MsgOpts>({mapOptionWire, sizeof(mapOptionWire)}));
  ZuPtr<MsgOpts> allocatedOption{
    Zpb::handler<MsgOpts>({mapOptionWire, sizeof(mapOptionWire)}).alloc()};
  ZuCheck(allocatedOption && allocatedOption->mapEntry);
  uint8_t input[sizeof(bytes)];
  memcpy(input, bytes, sizeof(bytes));
  Schema schema{{input, sizeof(input)}};
  memset(input, 0, sizeof(input));
  auto file = schema.file("row.proto");
  ZuCheck(file && file->package == "demo" && file->syntax == "proto3");
  ZuCheck(schema.file("row.proto", Syntax::Proto3) == file &&
    !schema.file("row.proto", Syntax::Proto2) &&
    !schema.file("row.proto", Syntax::T(2)));
  ProtoText proto;
  const char expectedProto[] =
    "syntax = \"proto3\";\npackage demo;\n\n"
    "message Row {\n  uint32 id = 1;\n}\n\n";
  ZuCheck(emitProto(proto, schema, *file,
      fileSyntax(*file)) &&
    !strcmp(proto.data(), expectedProto));
  auto message = schema.message(*file, "Row");
  ZuCheck(message && message->fields.length() == 1);
  auto field = schema.field(*message, 1);
  ZuCheck(field && field->name == "id" && field->number == 1 &&
    field->label == 1 && field->type == 13 && field->jsonName == "id");
  ZuCheck(schema.field(*message, "id") == field);
  ZuCheck(!schema.file("missing.proto") && !schema.message(*file, "Missing") &&
    !schema.field(*message, 2));
  const uint8_t row[] = {0x08, 0x96, 0x01};
  ZuCheck(verify(*message, {row, sizeof(row)}) &&
    Zpb::verify<NativeRow>({row, sizeof(row)}) &&
    Zpb::load<NativeRow>({row, sizeof(row)}).id == 150);

  uint8_t badType[sizeof(bytes)];
  memcpy(badType, bytes, sizeof(bytes));
  badType[39] = 0;
  ZuCheck(!verifySchema({badType, sizeof(badType)}));
  uint8_t groupType[sizeof(bytes)];
  memcpy(groupType, bytes, sizeof(bytes));
  groupType[37] = Type::Group;
  ZuCSpan reason;
  ZuCheck(!verifySchema({groupType, sizeof(groupType)}, {}, &reason) &&
    reason == "unsupported group field");
  ZuCheck(!verifySchema({bytes, sizeof(bytes) - 1}));
  uint8_t dependency[sizeof(bytes) + 2];
  memcpy(dependency, bytes, sizeof(bytes));
  dependency[1] += 2;
  dependency[sizeof(bytes)] = 0x1a; // FileDescriptorProto.dependency
  dependency[sizeof(bytes) + 1] = 0;
  ZuCheck(!verifySchema({dependency, sizeof(dependency)}));
  // FileOptions.java_package affects other language generators, not Zpb wire.
  uint8_t optioned[sizeof(bytes) + 7];
  memcpy(optioned, bytes, sizeof(bytes));
  optioned[1] += 7;
  const uint8_t javaPackage[] = {0x42, 0x05, 0x0a, 0x03, 'a', 'b', 'c'};
  memcpy(optioned + sizeof(bytes), javaPackage, sizeof(javaPackage));
  ZuCheck(verifySchema({optioned, sizeof(optioned)}));
  Schema withOptions{{optioned, sizeof(optioned)}};
  ProtoText withoutOptions;
  auto optionFile = withOptions.file("row.proto", Syntax::Proto3);
  ZuCheck(optionFile && emitProto(withoutOptions, withOptions, *optionFile,
    Syntax::Proto3) && !strcmp(withoutOptions.data(), expectedProto));
  const uint8_t rejectedOptions[][5] = {
    {0x42, 0x03, 0x92, 0x03, 0x00}, // FileOptions.features
    {0x42, 0x03, 0xba, 0x3e, 0x00}  // uninterpreted custom option
  };
  for (const auto &option : rejectedOptions) {
    uint8_t unsupportedOption[sizeof(bytes) + sizeof(option)];
    memcpy(unsupportedOption, bytes, sizeof(bytes));
    unsupportedOption[1] += sizeof(option);
    memcpy(unsupportedOption + sizeof(bytes), option, sizeof(option));
    ZuCheck(!verifySchema({unsupportedOption, sizeof(unsupportedOption)},
      {}, &reason) && reason == "unsupported file options");
  }
  // These valid descriptor wire fields carry schema semantics we do not import.
  const uint8_t unsupported[] = {
    0x32, // FileDescriptorProto.service
    0x3a, // FileDescriptorProto.extension
    0x50, // FileDescriptorProto.public_dependency
    0x58, // FileDescriptorProto.weak_dependency
    0x70  // FileDescriptorProto.edition
  };
  for (uint8_t tag : unsupported) {
    uint8_t semantic[sizeof(bytes) + 2];
    memcpy(semantic, bytes, sizeof(bytes));
    semantic[1] += 2;
    semantic[sizeof(bytes)] = tag;
    semantic[sizeof(bytes) + 1] = 0;
    ZuCSpan expected;
    switch (tag) {
      case 0x32: expected = "unsupported service descriptor"; break;
      case 0x3a: expected = "unsupported extension descriptor"; break;
      case 0x42: expected = "unsupported file options"; break;
      case 0x50: case 0x58:
	expected = "unsupported import qualifier"; break;
      case 0x70: expected = "unsupported edition descriptor"; break;
    }
    ZuCheck(!verifySchema({semantic, sizeof(semantic)}, {}, &reason) &&
      reason == expected);
  }
  uint8_t duplicate[sizeof(bytes) * 2];
  memcpy(duplicate, bytes, sizeof(bytes));
  memcpy(duplicate + sizeof(bytes), bytes, sizeof(bytes));
  ZuCheck(!verifySchema({duplicate, sizeof(duplicate)}));
}

static void testNativeExport()
{
  ZuTestScope(testNativeExport);
  DescSet descriptor;
  ZuCheck(exportSchema<ExportRow>(descriptor, "export.proto", "demo",
    Syntax::Proto2));
  ZuCheck(descriptor.files.length() == 1 &&
    descriptor.files[0].messages.length() == 1);
  const auto &message = descriptor.files[0].messages[0];
  ZuCheck(message.name == "ExportRow" && message.fields.length() == 4);
  ZuCheck(message.fields[0].name == "first" &&
    message.fields[0].number == 1 &&
    message.fields[0].type == Type::UInt32);
  ZuCheck(message.fields[1].name == "last" &&
    message.fields[1].number == 3 &&
    message.fields[1].type == Type::UInt32);
  ZuCheck(message.fields[2].number == 4 &&
    message.fields[2].type == Type::UInt32 &&
    message.fields[3].number == 5 &&
    message.fields[3].type == Type::Int32);
  ZuCheck(message.reservedRanges.length() == 1 &&
    message.reservedRanges[0].start == 2 &&
    message.reservedRanges[0].end == 3);
  Output output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto wire = Zpb::save(builder, descriptor);
  ZuCheck(!builder.failed() && verifySchema(wire));
  Schema schema{wire};
  auto file = schema.file("export.proto");
  ProtoText proto;
  ZuCheck(file && emitProto(proto, schema, *file,
      fileSyntax(*file)) &&
    strstr(proto.data(), "reserved 2;") &&
    strstr(proto.data(), "uint32 last = 3;") &&
    strstr(proto.data(), "uint32 small = 4;") &&
    strstr(proto.data(), "int32 signed_ = 5;"));
  DescSet faceted;
  ZuCheck((exportSchema<ExportRow, ZuFacet::ExportFacet>(faceted,
    "faceted.proto", "demo", Syntax::Proto2)));
  const auto &fields = faceted.files[0].messages[0].fields;
  ZuCheck(fields.length() == 4 && fields[0].number == 1 &&
    fields[1].name == "renamed" && fields[1].number == 3 &&
    fields[2].number == 4 && fields[3].number == 5);
  ZuCheck(faceted.files[0].messages[0].reservedRanges.length() == 1 &&
    faceted.files[0].messages[0].reservedRanges[0].start == 2);
  DescSet boolDescriptor;
  ZuCheck(exportSchema<ExportBoolRow>(boolDescriptor, "bool.proto", "demo",
    Syntax::Proto2));
  const auto &boolField = boolDescriptor.files[0].messages[0].fields[0];
  ZuCheck(boolField.number == 1 && boolField.label == 3 &&
    boolField.type == Type::Bool);
}

static void testRequiredSyntax()
{
  ZuTestScope(testRequiredSyntax);
  DescSet proto2, proto3;
  ZuCheck(exportSchema<ExportRequired>(proto2, "required2.proto", "demo",
    Syntax::Proto2));
  ZuCheck(exportSchema<ExportRequired>(proto3, "required3.proto", "demo",
    Syntax::Proto3));
  ZuCheck(proto2.files[0].messages[0].fields[0].label == 2 &&
    proto3.files[0].messages[0].fields[0].label == 1);
  Output output2, output3;
  Zpb::ArrayStorage storage2{output2}, storage3{output3};
  Zpb::Builder builder2{storage2}, builder3{storage3};
  auto wire2 = Zpb::save(builder2, proto2);
  auto wire3 = Zpb::save(builder3, proto3);
  ZuCheck(!builder2.failed() && verifySchema(wire2) &&
    !builder3.failed() && verifySchema(wire3));
  Schema schema2{wire2}, schema3{wire3};
  ProtoText text2, text3;
  ZuCheck(emitProto(text2, schema2, *schema2.file("required2.proto"),
      Syntax::Proto2) &&
    emitProto(text3, schema3, *schema3.file("required3.proto"),
      Syntax::Proto3) &&
    strstr(text2.data(), "required uint32 id = 1;") &&
    strstr(text3.data(), "uint32 id = 1;"));
  ProtoText mismatch;
  ZuCheck(!emitProto(mismatch, schema2, *schema2.file("required2.proto"),
      Syntax::Proto3) && !mismatch.length());
  DescSet invalid;
  ZuCheck(!exportSchema<ExportRequired>(invalid, "invalid.proto", "demo",
      Syntax::T(2)) && !invalid.files.length());
  ZuCheck(!emitProto(mismatch, schema2, *schema2.file("required2.proto"),
      Syntax::T(2)) && !mismatch.length());
}

static void testPackedOptions()
{
  ZuTestScope(testPackedOptions);
  auto check = []<typename T>(ZuCSpan fileName, Syntax::T syntax,
    int expected, ZuCSpan option) {
    DescSet descriptor;
    if (!exportSchema<T>(descriptor, fileName, "demo", syntax)) return false;
    const auto &field = descriptor.files[0].messages[0].fields[0];
    if (field.label != 3 || field.type != Type::Bool ||
      packedOption(field) != expected) return false;
    Output output;
    Zpb::ArrayStorage storage{output};
    Zpb::Builder builder{storage};
    auto wire = Zpb::save(builder, descriptor);
    if (builder.failed() || !verifySchema(wire)) return false;
    Schema schema{wire};
    auto file = schema.file(fileName, syntax);
    if (!file || packedOption(file->messages[0].fields[0]) != expected)
      return false;
    ProtoText proto;
    if (!emitProto(proto, schema, *file, syntax)) return false;
    return option.length() ?
      strstr(proto.data(), option.data()) != nullptr :
      strstr(proto.data(), "packed =") == nullptr;
  };
  ZuCheck(check.template operator()<ExportBoolRow>(
    "bool2.proto", Syntax::Proto2, -1, {}));
  ZuCheck(check.template operator()<ExportBoolRow>(
    "bool3.proto", Syntax::Proto3, 0, "packed = false"));
  ZuCheck(check.template operator()<ExportPackedBoolRow>(
    "packed2.proto", Syntax::Proto2, 1, "packed = true"));
  ZuCheck(check.template operator()<ExportPackedBoolRow>(
    "packed3.proto", Syntax::Proto3, 1, "packed = true"));
  auto option = [](ZuBSpan raw, int label, int type, bool valid,
    ZuCSpan expectedReason = {}) {
    DescSet descriptor;
    FileDesc file;
    file.name = "option.proto";
    file.syntax = "proto2";
    MsgDesc message;
    message.name = "Option";
    FieldDesc field{"values", {}, {}, 1, label, type};
    field.options = raw;
    message.fields.push(ZuMv(field));
    file.messages.push(ZuMv(message));
    descriptor.files.push(ZuMv(file));
    Output output;
    Zpb::ArrayStorage storage{output};
    Zpb::Builder builder{storage};
    auto wire = Zpb::save(builder, descriptor);
    ZuCSpan reason;
    bool result = !builder.failed() && verifySchema(wire, {}, &reason);
    return result == valid &&
      (!expectedReason.length() || reason == expectedReason);
  };
  const uint8_t duplicate[] = {0x10, 1, 0x10, 0};
  const uint8_t unknown[] = {0x18, 1};
  const uint8_t invalid[] = {0x10, 2};
  ZuCheck(option({duplicate, sizeof(duplicate)}, 3, Type::Bool, true));
  ZuCheck(option({unknown, sizeof(unknown)}, 3, Type::Bool, false,
    "unsupported field options"));
  ZuCheck(option({invalid, sizeof(invalid)}, 3, Type::Bool, false));
  ZuCheck(option(packedOptionBytes(true), 1, Type::Bool, false));
  ZuCheck(option(packedOptionBytes(true), 3, Type::String, false));
}

static void testNativeOneof()
{
  ZuTestScope(testNativeOneof);
  DescSet descriptor;
  ZuCheck(exportSchema<ExportChoiceRow>(descriptor, "choice.proto", "demo",
    Syntax::Proto2));
  Output output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto wire = Zpb::save(builder, descriptor);
  ZuCheck(!builder.failed() && verifySchema(wire));
  Schema schema{wire};
  auto message = schema.message(".demo.ExportChoiceRow");
  ZuCheck(message && message->fields.length() == 3 &&
    message->oneofs.length() == 1 && message->oneofs[0].name == "choice");
  ZuCheck(message->fields[0].number == 1 &&
    message->fields[1].number == 4 && message->fields[2].number == 6 &&
    message->fields[1].oneofIndex == 0 &&
    message->fields[2].oneofIndex == 0);
  ProtoText proto;
  ZuCheck(emitProto(proto, schema, *schema.file("choice.proto"),
      fileSyntax(*schema.file("choice.proto"))) &&
    strstr(proto.data(), "oneof choice {") &&
    strstr(proto.data(), ".demo.ExportA first = 4;") &&
    strstr(proto.data(), ".demo.ExportB second = 6;"));
  DescSet collision;
  ZuCheck(!exportSchema<ExportChoiceNameRow>(collision,
    "collision.proto", "demo", Syntax::Proto2) && !collision.files.length());
  ZuCheck(!exportSchema<ExportGroupNameRow>(collision,
    "collision.proto", "demo", Syntax::Proto2) && !collision.files.length());
}

static void testNativeMap()
{
  ZuTestScope(testNativeMap);
  DescSet descriptor;
  ZuCheck(exportSchema<ExportMapRow>(descriptor, "native_map.proto", "demo",
    Syntax::Proto2));
  Output output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto wire = Zpb::save(builder, descriptor);
  ZuCheck(!builder.failed() && verifySchema(wire));
  Schema schema{wire};
  auto message = schema.message(".demo.ExportMapRow");
  auto entry = schema.message(".demo.ExportMapRow.EntriesEntry");
  ZuCheck(message && entry && entry->options && entry->options->mapEntry &&
    message->fields.length() == 1 && message->fields[0].number == 1 &&
    message->fields[0].label == 3 &&
    message->fields[0].typeName == ".demo.ExportMapRow.EntriesEntry");
  ZuCheck(entry->fields.length() == 2 &&
    entry->fields[0].type == Type::Int32 &&
    entry->fields[1].type == Type::Int32);
  ProtoText proto;
  ZuCheck(emitProto(proto, schema, *schema.file("native_map.proto"),
      fileSyntax(*schema.file("native_map.proto"))) &&
    strstr(proto.data(), "map<int32, int32> entries = 1;") &&
    !strstr(proto.data(), "message EntriesEntry"));

  DescSet childDesc;
  ZuCheck(exportSchema<ExportChildMapRow>(childDesc,
    "child_map.proto", "demo", Syntax::Proto2));
  Output childOutput;
  Zpb::ArrayStorage childStorage{childOutput};
  Zpb::Builder childBuilder{childStorage};
  auto childWire = Zpb::save(childBuilder, childDesc);
  ZuCheck(!childBuilder.failed() && verifySchema(childWire));
  Schema childSchema{childWire};
  auto childEntry = childSchema.message(
    ".demo.ExportChildMapRow.EntriesEntry");
  ZuCheck(childEntry && childEntry->fields[1].type == Type::Message &&
    childEntry->fields[1].typeName == ".demo.ExportA");
}

static void testNativeArray()
{
  ZuTestScope(testNativeArray);
  DescSet descriptor;
  ZuCheck(exportSchema<ExportArrayRow>(descriptor, "array.proto", "demo",
    Syntax::Proto2));
  Output output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto wire = Zpb::save(builder, descriptor);
  ZuCheck(!builder.failed() && verifySchema(wire));
  Schema schema{wire};
  auto message = schema.message(".demo.ExportArrayRow");
  ZuCheck(message && message->fields.length() == 1 &&
    message->fields[0].label == 3 &&
    message->fields[0].typeName == ".demo.ExportA");
  ProtoText proto;
  ZuCheck(emitProto(proto, schema, *schema.file("array.proto"),
      fileSyntax(*schema.file("array.proto"))) &&
    strstr(proto.data(), "repeated .demo.ExportA items = 1;"));
}

static void testNativeMapped()
{
  ZuTestScope(testNativeMapped);
  DescSet descriptor;
  ZuCheck(!exportSchema<ExportMappedRow>(descriptor, "mapped.proto", "demo",
    Syntax::Proto2));
  ZuCheck(!descriptor.files.length());
  DescSet mapDesc;
  ZuCheck(!exportSchema<ExportFixedMapRow>(mapDesc,
    "mapped_map.proto", "demo", Syntax::Proto2) &&
    !mapDesc.files.length());
  DescSet appDesc;
  ZuCheck(exportSchema<ExportAppRow>(appDesc, "app.proto", "demo",
    Syntax::Proto2));
  ZuCheck(appDesc.files.length() == 1 &&
    appDesc.files[0].messages.length() == 2);
  Output appOutput;
  Zpb::ArrayStorage appStorage{appOutput};
  Zpb::Builder appBuilder{appStorage};
  ZuCheck(verifySchema(Zpb::save(appBuilder, appDesc)));
  DescSet rootMapped;
  ZuCheck(exportSchema<ZuFixed>(rootMapped, "fixed.proto", "Zf",
    Syntax::Proto2) &&
    rootMapped.files.length() == 1 &&
    rootMapped.files[0].messages.length() == 1 &&
    rootMapped.files[0].messages[0].name == "Fixed");
  ZuCheck(!exportSchema<ZuFixed>(rootMapped, "wrong.proto", "demo",
    Syntax::Proto2) &&
    rootMapped.files.length() == 1);
}

static void testDefaults()
{
  ZuTestScope(testDefaults);
  MsgDesc message;
  message.name = "Defaults";
  FieldDesc number{"number", {}, {}, 1, 1, Type::Int32};
  number.defaultValue = "-12";
  message.fields.push(ZuMv(number));
  FieldDesc text{"text", {}, {}, 2, 1, Type::String};
  text.defaultValue = "a\n\"b";
  message.fields.push(ZuMv(text));
  FieldDesc bytes{"bytes", {}, {}, 3, 1, Type::Bytes};
  bytes.defaultValue = "\\001\\377";
  message.fields.push(ZuMv(bytes));
  FieldDesc flag{"flag", {}, {}, 4, 1, Type::Bool};
  flag.defaultValue = "true";
  message.fields.push(ZuMv(flag));
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
  auto found = schema.message(".demo.Defaults");
  ZuCheck(found && found->fields[0].defaultValue == "-12" &&
    found->fields[1].defaultValue == "a\n\"b" &&
    found->fields[2].defaultValue == "\\001\\377");
  ProtoText proto;
  ZuCheck(emitProto(proto, schema, *schema.file("defaults.proto"),
      fileSyntax(*schema.file("defaults.proto"))) &&
    strstr(proto.data(), "default = -12") &&
    strstr(proto.data(), "default = \"a\\n\\\"b\"") &&
    strstr(proto.data(), "default = \"\\001\\377\"") &&
    strstr(proto.data(), "default = true"));
  descriptor.files[0].syntax = "proto3";
  Output invalidOutput;
  Zpb::ArrayStorage invalidStorage{invalidOutput};
  Zpb::Builder invalidBuilder{invalidStorage};
  ZuCheck(!verifySchema(Zpb::save(invalidBuilder, descriptor)));
}

static void testNativeDefaults()
{
  ZuTestScope(testNativeDefaults);
  DescSet descriptor;
  ZuCheck(exportSchema<ExportDefaults>(descriptor,
    "native-defaults.proto", "demo", Syntax::Proto2));
  const auto &fields = descriptor.files[0].messages[0].fields;
  ZuCheck(fields.length() == 5 && fields[0].defaultValue == "-12" &&
    fields[1].defaultValue == "true" &&
    fields[2].defaultValue == "a\n\"b" &&
    fields[3].defaultValue == "\\001\\377" &&
    fields[4].type == Type::Float && fields[4].defaultValue == "1.25");
  Output output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto wire = Zpb::save(builder, descriptor);
  ZuCheck(!builder.failed() && verifySchema(wire));
  Schema schema{wire};
  auto file = schema.file("native-defaults.proto");
  ProtoText proto;
  ZuCheck(file && emitProto(proto, schema, *file,
      fileSyntax(*file)) &&
    strstr(proto.data(), "default = -12") &&
    strstr(proto.data(), "default = true") &&
    strstr(proto.data(), "default = 1.25"));
  DescSet proto3;
  ZuCheck(exportSchema<ExportDefaults>(proto3,
    "native-defaults3.proto", "demo", Syntax::Proto3));
  bool noDefaults = true;
  for (const auto &field : proto3.files[0].messages[0].fields)
    noDefaults &= !field.defaultValue.data();
  ZuCheck(noDefaults);
}

static void testNativeEnum()
{
  ZuTestScope(testNativeEnum);
  DescSet descriptor;
  ZuCheck(exportSchema<ExportEnumRow>(descriptor, "enum-export.proto", "demo",
    Syntax::Proto2));
  const auto &file = descriptor.files[0];
  const auto &message = file.messages[0];
  ZuCheck(file.enums.length() == 1 &&
    file.enums[0].name == "ExportState" &&
    file.enums[0].values.length() == 2 &&
    file.enums[0].values[1].name == "Ready" &&
    file.enums[0].values[1].number == 1);
  ZuCheck(message.fields.length() == 2 &&
    message.fields[0].type == Type::Enum &&
    message.fields[0].typeName == ".demo.ExportState" &&
    message.fields[0].defaultValue == "Ready" &&
    message.fields[1].type == Type::Enum &&
    message.fields[1].label == 3);
  Output output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto wire = Zpb::save(builder, descriptor);
  ZuCheck(!builder.failed() && verifySchema(wire));
  Schema schema{wire};
  ProtoText proto;
  ZuCheck(emitProto(proto, schema, *schema.file("enum-export.proto"),
      fileSyntax(*schema.file("enum-export.proto"))) &&
    strstr(proto.data(), "enum ExportState {") &&
    strstr(proto.data(), "ExportState state = 1 [default = Ready];") &&
    strstr(proto.data(), "repeated .demo.ExportState states = 2;"));
  DescSet mapped;
  ZuCheck(exportSchema<ExportEnumMapRow>(mapped, "enum-map.proto", "demo",
    Syntax::Proto2));
  const auto &mapFile = mapped.files[0];
  const auto &entry = *mapFile.messages[0].nested[0].ptr();
  ZuCheck(mapFile.enums.length() == 1 &&
    entry.fields[1].type == Type::Enum &&
    entry.fields[1].typeName == ".demo.ExportState");
  Output mapOutput;
  Zpb::ArrayStorage mapStorage{mapOutput};
  Zpb::Builder mapBuilder{mapStorage};
  auto mapWire = Zpb::save(mapBuilder, mapped);
  ZuCheck(!mapBuilder.failed() && verifySchema(mapWire));
  Schema mapSchema{mapWire};
  ProtoText mapProto;
  ZuCheck(emitProto(mapProto, mapSchema, *mapSchema.file("enum-map.proto"),
      fileSyntax(*mapSchema.file("enum-map.proto"))) &&
    strstr(mapProto.data(), "map<int32, .demo.ExportState> colors = 1;"));
}

static void testNativeImports()
{
  ZuTestScope(testNativeImports);
  DescSet descriptor;
  ZuCheck(!exportSchema<CrossRow>(descriptor, "cross.proto", "demo",
    Syntax::Proto2) && !descriptor.files.length());
  ZuCheck(exportSchema<DepBundle>(descriptor, "dep.proto", "dep",
    Syntax::Proto2));
  ZuCheck(!exportSchema<BadCrossRow>(descriptor, "bad-cross.proto", "demo",
    Syntax::Proto2) && descriptor.files.length() == 1);
  ZuCheck(exportSchema<CrossRow>(descriptor, "cross.proto", "demo",
    Syntax::Proto2));
  if (descriptor.files.length() != 2) return;
  const auto &cross = descriptor.files[1];
  ZuCheck(cross.dependencies.length() == 1 &&
    cross.dependencies[0] == "dep.proto" && cross.messages.length() == 1 &&
    cross.messages[0].fields.length() == 2 &&
    cross.messages[0].fields[0].typeName == ".dep.DepMessage" &&
    cross.messages[0].fields[1].typeName == ".dep.ExportState");
  Output output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto wire = Zpb::save(builder, descriptor);
  ZuCheck(!builder.failed() && verifySchema(wire));
  if (builder.failed() || !verifySchema(wire)) return;
  Schema schema{wire};
  auto file = schema.file("cross.proto");
  ProtoText text;
  ZuCheck(file && emitProto(text, schema, *file,
      fileSyntax(*file)) &&
    strstr(text.data(), "import \"dep.proto\";") &&
    strstr(text.data(), ".dep.DepMessage child = 1;") &&
    strstr(text.data(), ".dep.ExportState state = 2;"));
}

static void testReservedSchema()
{
  ZuTestScope(testReservedSchema);
  MsgDesc message;
  message.name = "Retained";
  message.fields.push(FieldDesc{"current", {}, {}, 1, 1, Type::UInt32});
  message.reservedRanges.push(ReservedRange{2, 3});
  message.reservedRanges.push(ReservedRange{5, 8});
  message.reservedNames.push("former");
  FileDesc file;
  file.name = "reserved.proto";
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
  auto retained = schema.message(".demo.Retained");
  ZuCheck(retained && retained->reservedRanges.length() == 2 &&
    retained->reservedNames.length() == 1 &&
    retained->reservedNames[0] == "former" &&
    retained->reservedRanges[1].start == 5 &&
    retained->reservedRanges[1].end == 8);
  ProtoText proto;
  ZuCheck(emitProto(proto, schema, *schema.file("reserved.proto"),
      fileSyntax(*schema.file("reserved.proto"))) &&
    strstr(proto.data(), "reserved 2;") &&
    strstr(proto.data(), "reserved 5 to 7;") &&
    strstr(proto.data(), "reserved \"former\";"));
  descriptor.files[0].messages[0].fields[0].number = 5;
  Output invalidOutput;
  Zpb::ArrayStorage invalidStorage{invalidOutput};
  Zpb::Builder invalidBuilder{invalidStorage};
  ZuCheck(!verifySchema(Zpb::save(invalidBuilder, descriptor)));
  descriptor.files[0].messages[0].fields[0].number = 1;
  char names[64][8];
  auto verifyCurrent = [&]() {
    Output bytes;
    Zpb::ArrayStorage storage{bytes};
    Zpb::Builder builder{storage};
    auto wire = Zpb::save(builder, descriptor);
    return !builder.failed() && verifySchema(wire);
  };
  for (unsigned i = 0; i < 32; ++i) {
    snprintf(names[i], sizeof(names[i]), "r%u", i);
    descriptor.files[0].messages[0].reservedNames.push(
      ZuCSpan{&names[i][0]});
  }
  ZuCheck(verifyCurrent());
  descriptor.files[0].messages[0].reservedNames[32] =
    ZuCSpan{&names[3][0]};
  ZuCheck(!verifyCurrent());
  descriptor.files[0].messages[0].reservedNames[32] =
    ZuCSpan{&names[31][0]};
  for (unsigned i = 32; i < 64; ++i) {
    snprintf(names[i], sizeof(names[i]), "r%u", i);
    descriptor.files[0].messages[0].reservedNames.push(
      ZuCSpan{&names[i][0]});
  }
  ZuCheck(verifyCurrent());
  descriptor.files[0].messages[0].reservedNames[64] =
    ZuCSpan{&names[3][0]};
  ZuCheck(!verifyCurrent());
}

static void testRecursiveSchema()
{
  ZuTestScope(testRecursiveSchema);
  // protoc 36.1 descriptor set for proto2 demo.Node with optional Node next.
  const uint8_t bytes[] = {
    0x0a, 0x50, 0x0a, 0x0a, 0x6e, 0x6f, 0x64, 0x65, 0x2e, 0x70, 0x72, 0x6f,
    0x74, 0x6f, 0x12, 0x04, 0x64, 0x65, 0x6d, 0x6f, 0x22, 0x3c, 0x0a, 0x04,
    0x4e, 0x6f, 0x64, 0x65, 0x12, 0x14, 0x0a, 0x05, 0x76, 0x61, 0x6c, 0x75,
    0x65, 0x18, 0x01, 0x20, 0x01, 0x28, 0x0d, 0x52, 0x05, 0x76, 0x61, 0x6c,
    0x75, 0x65, 0x12, 0x1e, 0x0a, 0x04, 0x6e, 0x65, 0x78, 0x74, 0x18, 0x02,
    0x20, 0x01, 0x28, 0x0b, 0x32, 0x0a, 0x2e, 0x64, 0x65, 0x6d, 0x6f, 0x2e,
    0x4e, 0x6f, 0x64, 0x65, 0x52, 0x04, 0x6e, 0x65, 0x78, 0x74};
  ZuCheck(verifySchema({bytes, sizeof(bytes)}));
  Schema schema{{bytes, sizeof(bytes)}};
  auto message = schema.message(".demo.Node");
  ZuCheck(message && message->fields.length() == 2);
  auto next = schema.field(*message, "next");
  ZuCheck(next && next->type == Type::Message &&
    schema.message(next->typeName) == message);
  const uint8_t node[] = {0x08, 1, 0x12, 0x02, 0x08, 2};
  ZuCheck(verify(schema, *message, {node, sizeof(node)}, {1}));
  ZuCheck(!verify(schema, *message, {node, sizeof(node)}, {0}));
  ZuCheck(!verify(*message, {node, sizeof(node)}));
  const uint8_t child[] = {0x08, 2};
  Value fields[] = {
    {schema.field(*message, "value"), 1, 0, {}},
    {next, 0, 0, {child, sizeof(child)}}};
  Output output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  ZuCheck(write(builder, *message, ZuSpan<const Value>{fields, 2}));
  auto written = builder.bytes();
  ZuCheck(written.length() == sizeof(node) &&
    !memcmp(written.data(), node, sizeof(node)) &&
    verify(schema, *message, written, {1}));
  const uint8_t broken[] = {0x12, 0x01, 0x80};
  ZuCheck(!verify(schema, *message, {broken, sizeof(broken)}));
  uint8_t unresolved[sizeof(bytes)];
  memcpy(unresolved, bytes, sizeof(bytes));
  for (unsigned i = 0; i + 10 <= sizeof(bytes); ++i)
    if (!memcmp(unresolved + i, ".demo.Node", 10)) {
      unresolved[i + 9] = 'X';
      break;
    }
  ZuCheck(!verifySchema({unresolved, sizeof(unresolved)}));
}

static void testDependencies()
{
  ZuTestScope(testDependencies);
  FileDesc shared;
  shared.name = "shared.proto";
  shared.package = "demo";
  MsgDesc item;
  item.name = "Item";
  item.fields.push(FieldDesc{"id", {}, {}, 1, 1, Type::UInt32});
  shared.messages.push(ZuMv(item));
  FileDesc user;
  user.name = "user.proto";
  user.package = "demo";
  user.dependencies.push("shared.proto");
  MsgDesc holder;
  holder.name = "Holder";
  holder.fields.push(FieldDesc{"item", ".demo.Item", {},
    1, 1, Type::Message});
  user.messages.push(ZuMv(holder));
  DescSet descriptor;
  descriptor.files.push(ZuMv(shared));
  descriptor.files.push(ZuMv(user));
  Output output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto wire = Zpb::save(builder, descriptor);
  ZuCheck(!builder.failed() && verifySchema(wire));
  Schema schema{wire};
  auto file = schema.file("user.proto");
  auto message = schema.message(".demo.Holder");
  ZuCheck(file && file->dependencies.length() == 1 &&
    file->dependencies[0] == "shared.proto" && message);
  ZuCheck(schema.file("user.proto", Syntax::Proto2) == file &&
    !schema.file("user.proto", Syntax::Proto3));
  ProtoText proto;
  ZuCheck(emitProto(proto, schema, *file,
      fileSyntax(*file)) &&
    strstr(proto.data(), "import \"shared.proto\";") &&
    strstr(proto.data(), ".demo.Item item = 1;"));
  const uint8_t payload[] = {0x0a, 0x02, 0x08, 7};
  ZuCheck(verify(schema, *message, {payload, sizeof(payload)}));
  descriptor.files[1].dependencies[0] = "missing.proto";
  Output missingOutput;
  Zpb::ArrayStorage missingStorage{missingOutput};
  Zpb::Builder missingBuilder{missingStorage};
  auto missing = Zpb::save(missingBuilder, descriptor);
  ZuCheck(!missingBuilder.failed() && !verifySchema(missing));
  descriptor.files[1].dependencies.clear();
  Output undeclaredOutput;
  Zpb::ArrayStorage undeclaredStorage{undeclaredOutput};
  Zpb::Builder undeclaredBuilder{undeclaredStorage};
  auto undeclared = Zpb::save(undeclaredBuilder, descriptor);
  ZuCheck(!undeclaredBuilder.failed() && !verifySchema(undeclared));

  DescSet enumSet;
  FileDesc enumBase;
  enumBase.name = "enum-base.proto";
  enumBase.package = "demo";
  enumBase.syntax = "proto2";
  EnumDesc state;
  state.name = "State";
  state.values.push(EnumValueDesc{"STATE_NONE", 0});
  enumBase.enums.push(ZuMv(state));
  enumSet.files.push(ZuMv(enumBase));
  FileDesc enumUser;
  enumUser.name = "enum-user.proto";
  enumUser.package = "demo";
  enumUser.syntax = "proto3";
  enumUser.dependencies.push("enum-base.proto");
  MsgDesc enumHolder;
  enumHolder.name = "EnumHolder";
  enumHolder.fields.push(FieldDesc{"state", ".demo.State", {},
    1, 1, Type::Enum});
  ZuPtr<MsgDesc> stateEntry{new MsgDesc};
  stateEntry->name = "StatesEntry";
  stateEntry->options = new MsgOpts{true};
  stateEntry->fields.push(FieldDesc{"key", {}, {},
    1, 1, Type::String});
  stateEntry->fields.push(FieldDesc{"value", ".demo.State", {},
    2, 1, Type::Enum});
  enumHolder.nested.push(ZuMv(stateEntry));
  enumHolder.fields.push(FieldDesc{"states", ".demo.EnumHolder.StatesEntry",
    {}, 2, 3, Type::Message});
  enumUser.messages.push(ZuMv(enumHolder));
  enumSet.files.push(ZuMv(enumUser));
  auto validEnumSet = [&] {
    Output bytes;
    Zpb::ArrayStorage store{bytes};
    Zpb::Builder encode{store};
    auto wire = Zpb::save(encode, enumSet);
    return !encode.failed() && verifySchema(wire);
  };
  ZuCheck(!validEnumSet());
  auto &directEnum = enumSet.files[1].messages[0].fields[0];
  directEnum.type = Type::UInt32;
  directEnum.typeName = {};
  ZuCheck(!validEnumSet());
  enumSet.files[1].syntax = "proto2";
  ZuCheck(validEnumSet());
  enumSet.files[1].syntax = "proto3";
  enumSet.files[0].syntax = "proto3";
  ZuCheck(validEnumSet());

  char fileNames[64][16];
  DescSet manyFiles;
  auto addFiles = [&](unsigned from, unsigned to) {
    for (unsigned i = from; i < to; ++i) {
      snprintf(fileNames[i], sizeof(fileNames[i]), "file%u.proto", i);
      FileDesc next;
      next.name = ZuCSpan{&fileNames[i][0]};
      manyFiles.files.push(ZuMv(next));
    }
  };
  auto validFiles = [&]() {
    Output bytes;
    Zpb::ArrayStorage storage{bytes};
    Zpb::Builder builder{storage};
    auto wire = Zpb::save(builder, manyFiles);
    return !builder.failed() && verifySchema(wire);
  };
  addFiles(0, 32);
  manyFiles.files[0].dependencies.push(ZuCSpan{&fileNames[31][0]});
  ZuCheck(validFiles());
  manyFiles.files[31].name = ZuCSpan{&fileNames[3][0]};
  ZuCheck(!validFiles());
  manyFiles.files[31].name = ZuCSpan{&fileNames[31][0]};
  addFiles(32, 64);
  manyFiles.files[0].dependencies[0] = ZuCSpan{&fileNames[63][0]};
  ZuCheck(validFiles());
  manyFiles.files[63].name = ZuCSpan{&fileNames[3][0]};
  ZuCheck(!validFiles());
  manyFiles.files[63].name = ZuCSpan{&fileNames[63][0]};
  manyFiles.files[0].dependencies[0] = "missing.proto";
  ZuCheck(!validFiles());
}

static void testSymbolNames()
{
  ZuTestScope(testSymbolNames);
  DescSet descriptor;
  FileDesc base;
  base.name = "base.proto";
  base.package = "demo";
  base.syntax = "proto2";
  EnumDesc state;
  state.name = "Clash";
  state.values.push(EnumValueDesc{"ZERO", 0});
  base.enums.push(ZuMv(state));
  descriptor.files.push(ZuMv(base));
  auto valid = [&] {
    Output output;
    Zpb::ArrayStorage storage{output};
    Zpb::Builder builder{storage};
    auto wire = Zpb::save(builder, descriptor);
    return !builder.failed() && verifySchema(wire);
  };
  ZuCheck(valid());
  MsgDesc same;
  same.name = "Clash";
  descriptor.files[0].messages.push(ZuMv(same));
  ZuCheck(!valid());
  descriptor.files[0].messages.clear();

  FileDesc user;
  user.name = "user.proto";
  user.package = "demo";
  user.syntax = "proto2";
  MsgDesc cross;
  cross.name = "Clash";
  user.messages.push(ZuMv(cross));
  descriptor.files.push(ZuMv(user));
  ZuCheck(!valid());
  descriptor.files[1].messages.clear();
  EnumDesc repeated;
  repeated.name = "Clash";
  repeated.values.push(EnumValueDesc{"ZERO", 0});
  descriptor.files[1].enums.push(ZuMv(repeated));
  ZuCheck(!valid());
  descriptor.files[1].package = "other";
  ZuCheck(valid());

  MsgDesc outer;
  outer.name = "Outer";
  ZuPtr<MsgDesc> inner{new MsgDesc};
  inner->name = "Inner";
  outer.nested.push(ZuMv(inner));
  EnumDesc innerEnum;
  innerEnum.name = "Inner";
  innerEnum.values.push(EnumValueDesc{"ZERO", 0});
  outer.enums.push(ZuMv(innerEnum));
  descriptor.files[1].messages.push(ZuMv(outer));
  ZuCheck(!valid());

  char msgNames[64][8];
  FileDesc typeFile;
  typeFile.name = "many-types.proto";
  typeFile.package = "demo";
  DescSet manyTypes;
  manyTypes.files.push(ZuMv(typeFile));
  auto addMessages = [&](unsigned from, unsigned to) {
    for (unsigned i = from; i < to; ++i) {
      snprintf(msgNames[i], sizeof(msgNames[i]), "M%u", i);
      MsgDesc message;
      message.name = ZuCSpan{&msgNames[i][0]};
      manyTypes.files[0].messages.push(ZuMv(message));
    }
  };
  auto validTypes = [&]() {
    Output bytes;
    Zpb::ArrayStorage storage{bytes};
    Zpb::Builder builder{storage};
    auto wire = Zpb::save(builder, manyTypes);
    return !builder.failed() && verifySchema(wire);
  };
  addMessages(0, 8);
  ZuCheck(validTypes());
  manyTypes.files[0].messages[7].name = ZuCSpan{&msgNames[0][0]};
  ZuCheck(!validTypes());
  manyTypes.files[0].messages[7].name = ZuCSpan{&msgNames[7][0]};
  addMessages(8, 32);
  ZuCheck(validTypes());
  manyTypes.files[0].messages[31].name = ZuCSpan{&msgNames[3][0]};
  ZuCheck(!validTypes());
  manyTypes.files[0].messages[31].name = ZuCSpan{&msgNames[31][0]};
  EnumDesc mediumEnum;
  mediumEnum.name = ZuCSpan{&msgNames[31][0]};
  mediumEnum.values.push(EnumValueDesc{"V", 0});
  manyTypes.files[0].enums.push(ZuMv(mediumEnum));
  ZuCheck(!validTypes());
  manyTypes.files[0].enums.length(0);
  FileDesc mediumFile;
  mediumFile.name = "medium.proto";
  mediumFile.package = "other";
  MsgDesc mediumMessage;
  mediumMessage.name = ZuCSpan{&msgNames[31][0]};
  mediumFile.messages.push(ZuMv(mediumMessage));
  manyTypes.files.push(ZuMv(mediumFile));
  ZuCheck(validTypes());
  manyTypes.files[1].package = "demo";
  ZuCheck(!validTypes());
  manyTypes.files.length(1);
  addMessages(32, 48);
  ZuCheck(validTypes());
  manyTypes.files[0].messages[47].name = ZuCSpan{&msgNames[3][0]};
  ZuCheck(!validTypes());
  manyTypes.files[0].messages[47].name = ZuCSpan{&msgNames[47][0]};
  EnumDesc typeEnum;
  typeEnum.name = ZuCSpan{&msgNames[47][0]};
  typeEnum.values.push(EnumValueDesc{"V", 0});
  manyTypes.files[0].enums.push(ZuMv(typeEnum));
  ZuCheck(!validTypes());
  manyTypes.files[0].enums[0].name = "E";
  ZuCheck(validTypes());
  addMessages(48, 64);
  ZuCheck(validTypes());
  manyTypes.files[0].messages[63].name = ZuCSpan{&msgNames[3][0]};
  ZuCheck(!validTypes());
  manyTypes.files[0].messages[63].name = ZuCSpan{&msgNames[63][0]};
  FileDesc second;
  second.name = "more-types.proto";
  second.package = "demo";
  MsgDesc clash;
  clash.name = ZuCSpan{&msgNames[63][0]};
  second.messages.push(ZuMv(clash));
  manyTypes.files.push(ZuMv(second));
  ZuCheck(!validTypes());
  manyTypes.files[1].package = "other";
  ZuCheck(validTypes());
}

static void testNestedSchema()
{
  ZuTestScope(testNestedSchema);
  MsgDesc outer;
  outer.name = "Outer";
  outer.fields.push(FieldDesc{"child", ".demo.Outer.Inner", {},
    1, 1, Type::Message});
  ZuPtr<MsgDesc> inner{new MsgDesc{}};
  inner->name = "Inner";
  inner->fields.push(FieldDesc{"id", {}, {}, 1, 2, Type::UInt32});
  ZuPtr<MsgDesc> leaf{new MsgDesc{}};
  leaf->name = "Leaf";
  inner->nested.push(ZuMv(leaf));
  outer.nested.push(ZuMv(inner));
  FileDesc file;
  file.name = "nested.proto";
  file.package = "demo";
  file.messages.push(ZuMv(outer));
  DescSet descriptor;
  descriptor.files.push(ZuMv(file));
  Output output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto wire = Zpb::save(builder, descriptor);
  ZuCheck(!builder.failed() && verifySchema(wire));
  Schema schema{wire};
  auto root = schema.message(".demo.Outer");
  auto child = schema.message(".demo.Outer.Inner");
  ProtoText proto;
  ZuCheck(emitProto(proto, schema, *schema.file("nested.proto"),
      fileSyntax(*schema.file("nested.proto"))) &&
    strstr(proto.data(), "message Inner {") &&
    strstr(proto.data(), "required uint32 id = 1;"));
  ZuCheck(root && child && child == root->nested[0].ptr() &&
    schema.message(*schema.file("nested.proto"), "Outer") == root);
  ZuCheck(schema.message(".demo.Outer.Inner.Leaf") ==
    child->nested[0].ptr());
  const uint8_t payload[] = {0x0a, 0x02, 0x08, 7};
  ZuCheck(verify(schema, *root, {payload, sizeof(payload)}, {1}));
  const uint8_t emptyChild[] = {0x0a, 0};
  ZuCheck(!verify(schema, *root, {emptyChild, sizeof(emptyChild)}));
  ZuCheck(!schema.message(".demo.Outer.Missing"));
  ZuPtr<MsgDesc> duplicate{new MsgDesc{}};
  duplicate->name = "Inner";
  descriptor.files[0].messages[0].nested.push(ZuMv(duplicate));
  Output badOutput;
  Zpb::ArrayStorage badStorage{badOutput};
  Zpb::Builder badBuilder{badStorage};
  auto bad = Zpb::save(badBuilder, descriptor);
  ZuCheck(!badBuilder.failed() && !verifySchema(bad));

  char nestedNames[32][8], enumNames[32][8];
  MsgDesc many;
  many.name = "Many";
  FileDesc manyFile;
  manyFile.name = "many.proto";
  manyFile.messages.push(ZuMv(many));
  DescSet manySet;
  manySet.files.push(ZuMv(manyFile));
  auto validMany = [&]() {
    Output bytes;
    Zpb::ArrayStorage storage{bytes};
    Zpb::Builder builder{storage};
    auto wire = Zpb::save(builder, manySet);
    return !builder.failed() && verifySchema(wire);
  };
  for (unsigned i = 0; i < 16; ++i) {
    snprintf(nestedNames[i], sizeof(nestedNames[i]), "N%u", i);
    snprintf(enumNames[i], sizeof(enumNames[i]), "E%u", i);
    ZuPtr<MsgDesc> next{new MsgDesc{}};
    next->name = ZuCSpan{&nestedNames[i][0]};
    manySet.files[0].messages[0].nested.push(ZuMv(next));
    EnumDesc enumeration;
    enumeration.name = ZuCSpan{&enumNames[i][0]};
    enumeration.values.push(EnumValueDesc{"V", 0});
    manySet.files[0].messages[0].enums.push(ZuMv(enumeration));
  }
  ZuCheck(validMany());
  manySet.files[0].messages[0].enums[15].name = ZuCSpan{&nestedNames[0][0]};
  ZuCheck(!validMany());
  manySet.files[0].messages[0].enums[15].name = ZuCSpan{&enumNames[15][0]};
  for (unsigned i = 16; i < 32; ++i) {
    snprintf(nestedNames[i], sizeof(nestedNames[i]), "N%u", i);
    snprintf(enumNames[i], sizeof(enumNames[i]), "E%u", i);
    ZuPtr<MsgDesc> next{new MsgDesc{}};
    next->name = ZuCSpan{&nestedNames[i][0]};
    manySet.files[0].messages[0].nested.push(ZuMv(next));
    EnumDesc enumeration;
    enumeration.name = ZuCSpan{&enumNames[i][0]};
    enumeration.values.push(EnumValueDesc{"V", 0});
    manySet.files[0].messages[0].enums.push(ZuMv(enumeration));
  }
  ZuCheck(validMany());
  manySet.files[0].messages[0].enums[31].name = ZuCSpan{&nestedNames[31][0]};
  ZuCheck(!validMany());
}

static void testEnumSchema()
{
  ZuTestScope(testEnumSchema);
  EnumDesc color;
  color.name = "Color";
  color.values.push(EnumValueDesc{"RED", 0});
  color.values.push(EnumValueDesc{"BLUE", 2});
  MsgDesc paint;
  paint.name = "Paint";
  paint.fields.push(FieldDesc{"color", ".demo.Color", {},
    1, 1, Type::Enum});
  EnumDesc shade;
  shade.name = "Shade";
  shade.values.push(EnumValueDesc{"LIGHT", 0});
  shade.values.push(EnumValueDesc{"DARK", 1});
  paint.enums.push(ZuMv(shade));
  paint.fields.push(FieldDesc{"shade", ".demo.Paint.Shade", {},
    2, 1, Type::Enum});
  FileDesc file;
  file.name = "enum.proto";
  file.package = "demo";
  file.messages.push(ZuMv(paint));
  file.enums.push(ZuMv(color));
  DescSet descriptor;
  descriptor.files.push(ZuMv(file));
  Output output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto wire = Zpb::save(builder, descriptor);
  ZuCheck(!builder.failed() && verifySchema(wire));
  Schema schema{wire};
  auto message = schema.message(".demo.Paint");
  auto top = schema.enumeration(".demo.Color");
  auto nested = schema.enumeration(".demo.Paint.Shade");
  ProtoText proto;
  ZuCheck(emitProto(proto, schema, *schema.file("enum.proto"),
      fileSyntax(*schema.file("enum.proto"))) &&
    strstr(proto.data(), "enum Color {") &&
    strstr(proto.data(), "enum Shade {") &&
    strstr(proto.data(), "BLUE = 2;"));
  ZuCheck(message && top && nested && top->values.length() == 2 &&
    top->values[1].number == 2 && nested->values[1].name == "DARK");
  const uint8_t payload[] = {0x08, 2, 0x10, 1};
  ZuCheck(verify(schema, *message, {payload, sizeof(payload)}));
  unsigned count = 0;
  bool values = true;
  ZuCheck(visit(*message, {payload, sizeof(payload)}, [&](const Value &value) {
    values &= value.integer == (count++ ? 1 : 2);
    return true;
  }) && values && count == 2);
  const uint8_t unknown[] = {0x08, 7};
  ZuCheck(verify(schema, *message, {unknown, sizeof(unknown)}));
  const uint8_t wide[] = {0x08, 0x80, 0x80, 0x80, 0x80, 0x10};
  ZuCheck(!verify(schema, *message, {wide, sizeof(wide)}));
  Value emitted[] = {
    {schema.field(*message, 1), 2, 0, {}},
    {schema.field(*message, 2), 1, 0, {}}};
  Output writtenOutput;
  Zpb::ArrayStorage writtenStorage{writtenOutput};
  Zpb::Builder writtenBuilder{writtenStorage};
  ZuCheck(write(writtenBuilder, *message, ZuSpan<const Value>{emitted, 2}) &&
    writtenBuilder.bytes().length() == sizeof(payload) &&
    !memcmp(writtenBuilder.bytes().data(), payload, sizeof(payload)));
  Value negative{schema.field(*message, 1), uint64_t(int64_t(-1)), 0, {}};
  Output negativeOutput;
  Zpb::ArrayStorage negativeStorage{negativeOutput};
  Zpb::Builder negativeBuilder{negativeStorage};
  const uint8_t minusOne[] = {
    0x08, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0x01};
  ZuCheck(write(negativeBuilder, negative) &&
    negativeBuilder.bytes().length() == sizeof(minusOne) &&
    !memcmp(negativeBuilder.bytes().data(), minusOne, sizeof(minusOne)) &&
    verify(schema, *message, negativeBuilder.bytes()));
  Value tooWide{schema.field(*message, 1), uint64_t(INT32_MAX) + 1, 0, {}};
  Output wideOutput;
  Zpb::ArrayStorage wideStorage{wideOutput};
  Zpb::Builder wideBuilder{wideStorage};
  ZuCheck(!write(wideBuilder, tooWide) && !wideBuilder.size() &&
    wideBuilder.error() == Zpb::Error::Invalid);
  descriptor.files[0].messages[0].fields[0].typeName = ".demo.Missing";
  Output badOutput;
  Zpb::ArrayStorage badStorage{badOutput};
  Zpb::Builder badBuilder{badStorage};
  auto bad = Zpb::save(badBuilder, descriptor);
  ZuCheck(!badBuilder.failed() && !verifySchema(bad));
  descriptor.files[0].messages[0].fields[0].typeName = ".demo.Color";
  descriptor.files[0].enums[0].values[1].number = 0;
  Output aliasOutput;
  Zpb::ArrayStorage aliasStorage{aliasOutput};
  Zpb::Builder aliasBuilder{aliasStorage};
  auto alias = Zpb::save(aliasBuilder, descriptor);
  ZuCheck(!aliasBuilder.failed() && !verifySchema(alias));
  descriptor.files[0].syntax = "proto3";
  descriptor.files[0].enums[0].values[0].number = 1;
  descriptor.files[0].enums[0].values[1].number = 2;
  Output zeroOutput;
  Zpb::ArrayStorage zeroStorage{zeroOutput};
  Zpb::Builder zeroBuilder{zeroStorage};
  auto zero = Zpb::save(zeroBuilder, descriptor);
  ZuCheck(!zeroBuilder.failed() && !verifySchema(zero));

  EnumDesc large;
  large.name = "Large";
  char names[64][8];
  for (unsigned i = 0; i < 32; ++i) {
    snprintf(names[i], sizeof(names[i]), "V%u", i);
    large.values.push(EnumValueDesc{ZuCSpan{&names[i][0]}, int32_t(i)});
  }
  FileDesc largeFile;
  largeFile.name = "large.proto";
  largeFile.syntax = "proto3";
  largeFile.enums.push(ZuMv(large));
  DescSet largeSet;
  largeSet.files.push(ZuMv(largeFile));
  auto validLarge = [&]() {
    Output bytes;
    Zpb::ArrayStorage storage{bytes};
    Zpb::Builder builder{storage};
    auto wire = Zpb::save(builder, largeSet);
    return !builder.failed() && verifySchema(wire);
  };
  ZuCheck(validLarge());
  largeSet.files[0].enums[0].values[31].name = ZuCSpan{&names[3][0]};
  ZuCheck(!validLarge());
  largeSet.files[0].enums[0].values[31].name = ZuCSpan{&names[31][0]};
  largeSet.files[0].enums[0].values[31].number = 3;
  ZuCheck(!validLarge());
  largeSet.files[0].enums[0].values[31].number = 31;
  for (unsigned i = 32; i < 64; ++i) {
    snprintf(names[i], sizeof(names[i]), "V%u", i);
    largeSet.files[0].enums[0].values.push(
      EnumValueDesc{ZuCSpan{&names[i][0]}, int32_t(i)});
  }
  ZuCheck(validLarge());
  largeSet.files[0].enums[0].values[63].name = ZuCSpan{&names[3][0]};
  ZuCheck(!validLarge());
  largeSet.files[0].enums[0].values[63].name = ZuCSpan{&names[63][0]};
  largeSet.files[0].enums[0].values[63].number = 3;
  ZuCheck(!validLarge());
}

static void testLargeFieldSchema()
{
  ZuTestScope(testLargeFieldSchema);
  char names[64][8];
  MsgDesc message;
  message.name = "Wide";
  for (unsigned i = 0; i < 32; ++i) {
    snprintf(names[i], sizeof(names[i]), "f%u", i);
    message.fields.push(FieldDesc{ZuCSpan{&names[i][0]}, {}, {},
      int32_t(i + 1), 1, Type::UInt32});
  }
  FileDesc file;
  file.name = "wide.proto";
  file.syntax = "proto3";
  file.messages.push(ZuMv(message));
  DescSet set;
  set.files.push(ZuMv(file));
  auto valid = [&]() {
    Output bytes;
    Zpb::ArrayStorage storage{bytes};
    Zpb::Builder builder{storage};
    auto wire = Zpb::save(builder, set);
    return !builder.failed() && verifySchema(wire);
  };
  ZuCheck(valid());
  set.files[0].messages[0].fields[31].name = ZuCSpan{&names[3][0]};
  ZuCheck(!valid());
  set.files[0].messages[0].fields[31].name = ZuCSpan{&names[31][0]};
  set.files[0].messages[0].fields[31].number = 4;
  ZuCheck(!valid());
  set.files[0].messages[0].fields[31].number = 32;
  set.files[0].messages[0].reservedNames.push(ZuCSpan{&names[31][0]});
  ZuCheck(!valid());
  set.files[0].messages[0].reservedNames.length(0);
  set.files[0].messages[0].oneofs.push(OneofDesc{ZuCSpan{&names[31][0]}});
  ZuCheck(!valid());
  set.files[0].messages[0].oneofs.length(0);
  for (unsigned i = 32; i < 64; ++i) {
    snprintf(names[i], sizeof(names[i]), "f%u", i);
    set.files[0].messages[0].fields.push(FieldDesc{
      ZuCSpan{&names[i][0]}, {}, {}, int32_t(i + 1), 1, Type::UInt32});
  }
  ZuCheck(valid());
  set.files[0].messages[0].reservedNames.push(ZuCSpan{&names[63][0]});
  ZuCheck(!valid());
  set.files[0].messages[0].reservedNames.length(0);
  set.files[0].messages[0].oneofs.push(OneofDesc{ZuCSpan{&names[63][0]}});
  ZuCheck(!valid());
  set.files[0].messages[0].oneofs.length(0);
  Output indexedBytes;
  Zpb::ArrayStorage indexedStorage{indexedBytes};
  Zpb::Builder indexedBuilder{indexedStorage};
  auto indexedWire = Zpb::save(indexedBuilder, set);
  Schema schema{indexedWire};
  auto loaded = schema.message(".Wide");
  const uint8_t present[] = {0x80, 0x04, 7};
  const uint8_t unknown[] = {0x88, 0x04, 7};
  ZuCheck(!indexedBuilder.failed());
  ZuCheck(loaded);
  if (loaded) {
    ZuCheck(schema.field(*loaded, 64) == schema.field(*loaded, "f63"));
    ZuCheck(!schema.field(*loaded, 65));
    ZuCheck(verify(schema, *loaded, {present, sizeof(present)}));
    ZuCheck(verify(schema, *loaded, {unknown, sizeof(unknown)}));
    unsigned visits = 0;
    ZuCheck(visit(schema, *loaded, {present, sizeof(present)},
      [&](const Value &value) {
	++visits;
	return value.field == schema.field(*loaded, 64) &&
	  value.integer == 7;
      }) && visits == 1);
    ZuCheck(visit(schema, *loaded, {unknown, sizeof(unknown)},
      [&](const Value &) { return false; }));
  }
  ZuCheck(schema.field(set.files[0].messages[0], 64) ==
    &set.files[0].messages[0].fields[63]);
  set.files[0].messages[0].fields[63].name = ZuCSpan{&names[3][0]};
  ZuCheck(!valid());
  set.files[0].messages[0].fields[63].name = ZuCSpan{&names[63][0]};
  set.files[0].messages[0].fields[63].number = 4;
  ZuCheck(!valid());
}

static void testOneofSchema()
{
  ZuTestScope(testOneofSchema);
  MsgDesc first;
  first.name = "First";
  first.fields.push(FieldDesc{"a", {}, {}, 1, 2, Type::UInt32});
  first.fields.push(FieldDesc{"b", {}, {}, 2, 2, Type::UInt32});
  MsgDesc second;
  second.name = "Second";
  second.fields.push(FieldDesc{"c", {}, {}, 1, 2, Type::UInt32});
  MsgDesc choice;
  choice.name = "Choice";
  choice.oneofs.push(OneofDesc{"choice"});
  FieldDesc a{"first", ".demo.First", {}, 1, 1, Type::Message};
  a.oneofIndex = 0;
  FieldDesc b{"second", ".demo.Second", {}, 2, 1, Type::Message};
  b.oneofIndex = 0;
  choice.fields.push(ZuMv(a));
  choice.fields.push(ZuMv(b));
  FileDesc file;
  file.name = "oneof.proto";
  file.package = "demo";
  file.messages.push(ZuMv(first));
  file.messages.push(ZuMv(second));
  file.messages.push(ZuMv(choice));
  DescSet descriptor;
  descriptor.files.push(ZuMv(file));
  Output output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto wire = Zpb::save(builder, descriptor);
  ZuCheck(!builder.failed() && verifySchema(wire));
  Schema schema{wire};
  auto message = schema.message(".demo.Choice");
  ProtoText proto;
  ZuCheck(emitProto(proto, schema, *schema.file("oneof.proto"),
      fileSyntax(*schema.file("oneof.proto"))) &&
    strstr(proto.data(), "oneof choice {") &&
    strstr(proto.data(), ".demo.First first = 1;"));
  ZuCheck(message && message->oneofs.length() == 1 &&
    message->oneofs[0].name == "choice" &&
    message->fields[0].oneofIndex == 0 &&
    message->fields[1].oneofIndex == 0);
  const uint8_t merged[] = {
    0x0a, 2, 0x08, 1,
    0x0a, 2, 0x10, 2};
  ZuCheck(verify(schema, *message, {merged, sizeof(merged)}));
  const uint8_t replaced[] = {
    0x0a, 2, 0x08, 1,
    0x12, 2, 0x08, 3};
  ZuCheck(verify(schema, *message, {replaced, sizeof(replaced)}));
  const uint8_t malformedEarlier[] = {
    0x0a, 1, 0x80,
    0x12, 2, 0x08, 3};
  ZuCheck(!verify(schema, *message,
    {malformedEarlier, sizeof(malformedEarlier)}));
  const uint8_t switchedBack[] = {
    0x0a, 2, 0x08, 1,
    0x12, 2, 0x08, 3,
    0x0a, 2, 0x10, 2};
  ZuCheck(!verify(schema, *message,
    {switchedBack, sizeof(switchedBack)}));
  descriptor.files[0].messages[2].fields[0].oneofIndex = 1;
  Output badOutput;
  Zpb::ArrayStorage badStorage{badOutput};
  Zpb::Builder badBuilder{badStorage};
  auto bad = Zpb::save(badBuilder, descriptor);
  ZuCheck(!badBuilder.failed() && !verifySchema(bad));
  descriptor.files[0].messages[2].fields[0].oneofIndex = 0;
  descriptor.files[0].messages[2].fields.push(
    FieldDesc{"choice", {}, {}, 3, 1, Type::UInt32});
  Zpb::Builder nameBuilder{badStorage};
  auto badName = Zpb::save(nameBuilder, descriptor);
  ZuCheck(!nameBuilder.failed() && !verifySchema(badName));
  auto &wide = descriptor.files[0].messages[2];
  wide.fields.length(wide.fields.length() - 1);
  char oneofNames[64][12], fieldNames[64][12];
  for (unsigned i = 1; i < 64; ++i) {
    snprintf(oneofNames[i], sizeof(oneofNames[i]), "choice%u", i);
    snprintf(fieldNames[i], sizeof(fieldNames[i]), "field%u", i);
    wide.oneofs.push(OneofDesc{ZuCSpan{&oneofNames[i][0]}});
    FieldDesc field{ZuCSpan{&fieldNames[i][0]}, ".demo.First", {},
      int32_t(i + 2), 1, Type::Message};
    field.oneofIndex = int32_t(i);
    wide.fields.push(ZuMv(field));
  }
  auto verifyWide = [&]() {
    Output bytes;
    Zpb::ArrayStorage storage{bytes};
    Zpb::Builder builder{storage};
    auto wire = Zpb::save(builder, descriptor);
    return !builder.failed() && verifySchema(wire);
  };
  ZuCheck(verifyWide());
  wide.oneofs[63].name = "choice";
  ZuCheck(!verifyWide());

  DescSet grouped;
  FileDesc groupedFile;
  groupedFile.name = "grouped.proto";
  groupedFile.syntax = "proto3";
  MsgDesc groupedMessage;
  groupedMessage.name = "Grouped";
  ZuCSpan groupOneofs[] = {"first", "second", "third", "fourth"};
  ZuCSpan groupFields[] = {"a", "b", "c", "d"};
  for (unsigned i = 0; i < 4; ++i) {
    groupedMessage.oneofs.push(OneofDesc{groupOneofs[i]});
    FieldDesc field{groupFields[i], {}, {}, int32_t(i + 1), 1, Type::UInt32};
    field.oneofIndex = int32_t(i);
    field.proto3Optional = i == 3;
    groupedMessage.fields.push(ZuMv(field));
  }
  groupedFile.messages.push(ZuMv(groupedMessage));
  grouped.files.push(ZuMv(groupedFile));
  auto verifyGrouped = [&]() {
    Output bytes;
    Zpb::ArrayStorage storage{bytes};
    Zpb::Builder builder{storage};
    auto wire = Zpb::save(builder, grouped);
    return !builder.failed() && verifySchema(wire);
  };
  ZuCheck(verifyGrouped());
  auto &groupedFields = grouped.files[0].messages[0].fields;
  groupedFields[0].proto3Optional = true;
  ZuCheck(!verifyGrouped());
  groupedFields[0].proto3Optional = false;
  FieldDesc extra{"extra", {}, {}, 5, 1, Type::UInt32};
  extra.oneofIndex = 3;
  groupedFields.push(ZuMv(extra));
  ZuCheck(!verifyGrouped());
}

static void testMapSchema()
{
  ZuTestScope(testMapSchema);
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
  FileDesc file;
  file.name = "map.proto";
  file.package = "demo";
  file.messages.push(ZuMv(row));
  DescSet descriptor;
  descriptor.files.push(ZuMv(file));
  Output output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto wire = Zpb::save(builder, descriptor);
  ZuCheck(!builder.failed() && verifySchema(wire));
  Schema schema{wire};
  auto message = schema.message(".demo.Row");
  auto mapEntry = schema.message(".demo.Row.EntriesEntry");
  ProtoText proto;
  ZuCheck(emitProto(proto, schema, *schema.file("map.proto"),
      fileSyntax(*schema.file("map.proto"))) &&
    strstr(proto.data(), "map<uint32, string> entries = 1;") &&
    !strstr(proto.data(), "message EntriesEntry"));
  ZuCheck(message && mapEntry && mapEntry->options &&
    mapEntry->options->mapEntry);
  const uint8_t payload[] = {
    0x0a, 5, 0x08, 7, 0x12, 1, 'a',
    0x0a, 5, 0x12, 1, 'b', 0x08, 7};
  ZuCheck(verify(schema, *message, {payload, sizeof(payload)}));
  unsigned count = 0;
  ZuCheck(visit(*message, {payload, sizeof(payload)},
    [&](const Value &value) {
      count += value.field == &message->fields[0] &&
	value.bytes.length() == 5;
      return true;
    }) && count == 2);
  auto field = schema.field(*message, 1);
  Value entries[] = {
    {field, 0, 0, {payload + 2, 5}},
    {field, 0, 0, {payload + 9, 5}}};
  Output writtenOutput;
  Zpb::ArrayStorage writtenStorage{writtenOutput};
  Zpb::Builder writtenBuilder{writtenStorage};
  ZuCheck(write(writtenBuilder, *message, ZuSpan<const Value>{entries, 2}) &&
    writtenBuilder.bytes().length() == sizeof(payload) &&
    !memcmp(writtenBuilder.bytes().data(), payload, sizeof(payload)));
  const uint8_t malformed[] = {0x0a, 2, 0x08, 0x80};
  ZuCheck(!verify(schema, *message, {malformed, sizeof(malformed)}));
  descriptor.files[0].messages[0].nested[0]->fields[0].type = Type::Bytes;
  Output badOutput;
  Zpb::ArrayStorage badStorage{badOutput};
  Zpb::Builder badBuilder{badStorage};
  auto bad = Zpb::save(badBuilder, descriptor);
  ZuCheck(!badBuilder.failed() && !verifySchema(bad));
}

static void testMessageMerge()
{
  ZuTestScope(testMessageMerge);
  MsgDesc child;
  child.name = "Child";
  child.fields.push(FieldDesc{"a", {}, {}, 1, 2, Type::UInt32});
  child.fields.push(FieldDesc{"b", {}, {}, 2, 2, Type::UInt32});
  MsgDesc parent;
  parent.name = "Parent";
  parent.fields.push(FieldDesc{"child", ".demo.Child", {},
    1, 1, Type::Message});
  FileDesc file;
  file.name = "merge.proto";
  file.package = "demo";
  file.messages.push(ZuMv(child));
  file.messages.push(ZuMv(parent));
  DescSet descriptor;
  descriptor.files.push(ZuMv(file));
  Output output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto descriptorWire = Zpb::save(builder, descriptor);
  ZuCheck(!builder.failed() && verifySchema(descriptorWire));
  Schema schema{descriptorWire};
  auto message = schema.message(".demo.Parent");
  auto childMessage = schema.message(".demo.Child");
  ZuCheck(message && childMessage);
  const uint8_t merged[] = {
    0x0a, 0x02, 0x08, 1,
    0x0a, 0x02, 0x10, 2};
  ZuCheck(verify(schema, *message, {merged, sizeof(merged)}));
  ZuCheck(!verify(schema, *message, {merged, 4}));
  MsgDesc repeated;
  repeated.name = "RepeatedParent";
  repeated.fields.push(FieldDesc{"child", ".demo.Child", {},
    1, 3, Type::Message});
  ZuCheck(!verify(schema, repeated, {merged, sizeof(merged)}));

  Value missing[] = {{schema.field(*childMessage, "a"), 1, 0, {}}};
  Output missingOutput;
  Zpb::ArrayStorage missingStorage{missingOutput};
  Zpb::Builder missingBuilder{missingStorage};
  ZuCheck(!write(missingBuilder, *childMessage,
    ZuSpan<const Value>{missing, 1}) &&
    missingBuilder.error() == Zpb::Error::Required && !missingBuilder.size());
  Value complete[] = {
    {schema.field(*childMessage, "a"), 1, 0, {}},
    {schema.field(*childMessage, "b"), 2, 0, {}}};
  Output completeOutput;
  Zpb::ArrayStorage completeStorage{completeOutput};
  Zpb::Builder completeBuilder{completeStorage};
  ZuCheck(write(completeBuilder, *childMessage,
    ZuSpan<const Value>{complete, 2}) &&
    verify(schema, *childMessage, completeBuilder.bytes()));

  auto childField = schema.field(*message, "child");
  ZuCheck(childField);
  if (!childField) return;
  Output nestedOutput;
  Zpb::ArrayStorage nestedStorage{nestedOutput};
  Zpb::Builder nestedBuilder{nestedStorage};
  const uint8_t nestedWire[] = {0x0a, 0x04, 0x08, 1, 0x10, 2};
  ZuCheck(writeNested(nestedBuilder, schema, *message, *childField,
    ZuSpan<const Value>{complete, 2}) &&
    nestedBuilder.bytes().length() == sizeof(nestedWire) &&
    !memcmp(nestedBuilder.bytes().data(), nestedWire, sizeof(nestedWire)) &&
    verify(schema, *message, nestedBuilder.bytes()));

  Output missingNestedOutput;
  Zpb::ArrayStorage missingNestedStorage{missingNestedOutput};
  Zpb::Builder missingNestedBuilder{missingNestedStorage};
  ZuCheck(!writeNested(missingNestedBuilder, schema, *message, *childField,
    ZuSpan<const Value>{missing, 1}) &&
    missingNestedBuilder.error() == Zpb::Error::Required &&
    !missingNestedBuilder.size());

  Output wrongParentOutput;
  Zpb::ArrayStorage wrongParentStorage{wrongParentOutput};
  Zpb::Builder wrongParentBuilder{wrongParentStorage};
  ZuCheck(!writeNested(wrongParentBuilder, schema, *childMessage, *childField,
    ZuSpan<const Value>{complete, 2}) &&
    wrongParentBuilder.error() == Zpb::Error::Invalid &&
    !wrongParentBuilder.size());
}

static void testPayload()
{
  ZuTestScope(testPayload);
  MsgDesc message;
  message.fields.push(FieldDesc{"id", {}, {}, 1, 1, Type::UInt32});
  message.fields.push(FieldDesc{"delta", {}, {}, 2, 1, Type::SInt32});
  message.fields.push(FieldDesc{"word", {}, {}, 3, 1, Type::Fixed32});
  message.fields.push(FieldDesc{"text", {}, {}, 4, 1, Type::String});
  message.fields.push(FieldDesc{"ratio", {}, {}, 5, 1, Type::Float});
  message.fields.push(FieldDesc{"enabled", {}, {}, 6, 1, Type::Bool});
  message.fields.push(FieldDesc{"values", {}, {}, 7, 3, Type::UInt32});
  message.fields.push(FieldDesc{"double", {}, {}, 8, 1, Type::Double});
  message.fields.push(FieldDesc{"blob", {}, {}, 9, 1, Type::Bytes});
  message.fields.push(FieldDesc{"negative", {}, {}, 10, 1, Type::SFixed64});
  message.fields.push(FieldDesc{"zigzag", {}, {}, 11, 1, Type::SInt64});
  message.fields.push(FieldDesc{"signed", {}, {}, 12, 1, Type::Int32});
  const uint8_t bytes[] = {
    0x08, 0x96, 0x01,
    0x10, 0x01,
    0x1d, 0x12, 0x34, 0x56, 0x78,
    0x22, 0x03, 'a', 0, 'b',
    0x2d, 0, 0, 0xc0, 0x3f,
    0x30, 0x02,
    0x3a, 0x03, 1, 2, 3,
    0x41, 0, 0, 0, 0, 0, 0, 0x04, 0x40,
    0x4a, 0x02, 0, 0xff,
    0x51, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x58, 0x03,
    0x60, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01,
    0x68, 7}; // unknown field
  ZuCheck(verify(message, {bytes, sizeof(bytes)}));
  unsigned values = 0, total = 0;
  bool decoded = true;
  Events events;
  ZuCheck(visit(message, {bytes, sizeof(bytes)}, [&](const Value &value) {
    events.push(value);
    ++total;
    switch (value.field->number) {
      case 1: decoded &= value.integer == 150; break;
      case 2: decoded &= int64_t(value.integer) == -1; break;
      case 3: decoded &= value.integer == 0x78563412; break;
      case 4: decoded &= value.bytes.length() == 3 &&
	!memcmp(value.bytes.data(), "a\0b", 3); break;
      case 5: decoded &= value.real == 1.5; break;
      case 6: decoded &= value.integer == 1; break;
      case 7: decoded &= value.integer == ++values; break;
      case 8: decoded &= value.real == 2.5; break;
      case 9: decoded &= value.bytes.length() == 2 &&
	value.bytes[0] == 0 && value.bytes[1] == 0xff; break;
      case 10: decoded &= int64_t(value.integer) == -2; break;
      case 11: decoded &= int64_t(value.integer) == -2; break;
      case 12: decoded &= int64_t(value.integer) == -1; break;
      default: decoded = false;
    }
    return true;
  }));
  ZuCheck(decoded && values == 3 && total == 14);
  Output output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  ZuCheck(write(builder, ZuSpan<const Value>{events.data(), events.length()}));
  auto rewritten = builder.bytes();
  ZuCheck(verify(message, rewritten));
  unsigned compared = 0;
  bool same = true;
  ZuCheck(visit(message, rewritten, [&](const Value &value) {
    const auto &old = events[compared++];
    same &= old.field == value.field;
    switch (value.field->type) {
      case Type::String: case Type::Bytes:
	same &= old.bytes.length() == value.bytes.length() &&
	  !memcmp(old.bytes.data(), value.bytes.data(), old.bytes.length());
	break;
      case Type::Float: case Type::Double:
	same &= old.real == value.real;
	break;
      default: same &= old.integer == value.integer;
    }
    return true;
  }) && same && compared == events.length());
  unsigned stopped = 0;
  ZuCheck(!visit(message, {bytes, sizeof(bytes)}, [&](const Value &) {
    ++stopped;
    return false;
  }) && stopped == 1);

  const uint8_t unpacked[] = {0x38, 4, 0x38, 5};
  ZuCheck(verify(message, {unpacked, sizeof(unpacked)}));
  const uint8_t wide[] = {0x08, 0x80, 0x80, 0x80, 0x80, 0x10};
  ZuCheck(!verify(message, {wide, sizeof(wide)}));
  const uint8_t wrongWire[] = {0x0a, 0x01, 1};
  ZuCheck(!verify(message, {wrongWire, sizeof(wrongWire)}));
  const uint8_t shortPacked[] = {0x3a, 0x01, 0x80};
  ZuCheck(!verify(message, {shortPacked, sizeof(shortPacked)}));

  MsgDesc fixed;
  fixed.fields.push(FieldDesc{"signed", {}, {}, 1, 3, Type::SFixed32});
  const uint8_t fixedPacked[] = {
    0x0a, 0x08, 0xfe, 0xff, 0xff, 0xff, 0x02, 0, 0, 0};
  ZuCheck(verify(fixed, {fixedPacked, sizeof(fixedPacked)}));
  int64_t fixedSum = 0;
  ZuCheck(visit(fixed, {fixedPacked, sizeof(fixedPacked)},
    [&](const Value &value) {
      fixedSum += int64_t(value.integer);
      return true;
    }) && fixedSum == 0);
  const uint8_t shortFixed[] = {0x0a, 0x03, 1, 2, 3};
  ZuCheck(!verify(fixed, {shortFixed, sizeof(shortFixed)}));

  MsgDesc required;
  required.fields.push(FieldDesc{"id", {}, {}, 1, 2, Type::UInt32});
  ZuCheck(!verify(required, {}));
  const uint8_t present[] = {0x08, 0};
  ZuCheck(verify(required, {present, sizeof(present)}));
  const uint8_t group[] = {0xa3, 0x01, 0xa4, 0x01};
  ZuCheck(!verify(message, {group, sizeof(group)}, {0}));
  ZuCheck(verify(message, {group, sizeof(group)}, {1}));
}

static void testWrite()
{
  ZuTestScope(testWrite);
  FieldDesc id{"id", {}, {}, 1, 1, Type::UInt32};
  FieldDesc delta{"delta", {}, {}, 2, 1, Type::SInt32};
  FieldDesc text{"text", {}, {}, 4, 1, Type::Bytes};
  FieldDesc values{"values", {}, {}, 7, 3, Type::UInt32};
  const uint8_t chars[] = {'a', 0, 'b'};
  Value input[] = {
    {&id, 150, 0, {}},
    {&delta, uint64_t(int64_t(-1)), 0, {}},
    {&text, 0, 0, {chars, sizeof(chars)}},
    {&values, 1, 0, {}},
    {&values, 2, 0, {}},
    {&values, 3, 0, {}}};
  Output output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  ZuCheck(write(builder, ZuSpan<const Value>{input,
    sizeof(input) / sizeof(input[0])}));
  const uint8_t expected[] = {
    0x08, 0x96, 0x01, 0x10, 0x01, 0x22, 0x03, 'a', 0, 'b',
    0x38, 1, 0x38, 2, 0x38, 3};
  auto wire = builder.bytes();
  ZuCheck(wire.length() == sizeof(expected) &&
    !memcmp(wire.data(), expected, sizeof(expected)));
  MsgDesc message;
  message.fields.push(ZuMv(id));
  message.fields.push(ZuMv(delta));
  message.fields.push(ZuMv(text));
  message.fields.push(ZuMv(values));
  ZuCheck(verify(message, wire));

  Output invalidOutput;
  Zpb::ArrayStorage invalidStorage{invalidOutput};
  Zpb::Builder invalidBuilder{invalidStorage};
  Value tooWide{&id, uint64_t(UINT32_MAX) + 1, 0, {}};
  ZuCheck(!write(invalidBuilder, tooWide) && !invalidBuilder.size() &&
    invalidBuilder.error() == Zpb::Error::Invalid);
}

int main(int argc, char **argv)
{
  ZuTestMain();
  ZuTestCall(testDescriptor);
  ZuTestCall(testNativeExport);
  ZuTestCall(testRequiredSyntax);
  ZuTestCall(testPackedOptions);
  ZuTestCall(testNativeOneof);
  ZuTestCall(testNativeMap);
  ZuTestCall(testNativeArray);
  ZuTestCall(testNativeMapped);
  ZuTestCall(testDefaults);
  ZuTestCall(testNativeDefaults);
  ZuTestCall(testNativeEnum);
  ZuTestCall(testNativeImports);
  ZuTestCall(testReservedSchema);
  ZuTestCall(testRecursiveSchema);
  ZuTestCall(testDependencies);
  ZuTestCall(testSymbolNames);
  ZuTestCall(testNestedSchema);
  ZuTestCall(testEnumSchema);
  ZuTestCall(testLargeFieldSchema);
  ZuTestCall(testOneofSchema);
  ZuTestCall(testMapSchema);
  ZuTestCall(testMessageMerge);
  ZuTestCall(testPayload);
  ZuTestCall(testWrite);
}
