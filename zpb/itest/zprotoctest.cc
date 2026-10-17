//  -*- mode:c++; indent-tabs-mode:t; tab-width=8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuBox.hh>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZpbReflect.hh>

#include "ZiTestResidue.hh"
#include "ZpbTestUtil.hh"

using namespace ZuTestUtil;
using namespace ZpbTest;

static void addMap(Zpb::Reflect::MsgDesc &message, ZuCSpan name,
  ZuCSpan entryName, ZuCSpan entryType, int32_t number, int32_t keyType,
  int32_t valueType, ZuCSpan valueName = {}, bool reverse = false)
{
  using namespace Zpb::Reflect;
  ZuPtr<MsgDesc> entry{new MsgDesc};
  entry->name = entryName;
  entry->options = new MsgOpts{true};
  FieldDesc key{"key", {}, {}, 1, 1, keyType};
  FieldDesc value{"value", valueName, {}, 2, 1, valueType};
  if (reverse) {
    entry->fields.push(ZuMv(value));
    entry->fields.push(ZuMv(key));
  } else {
    entry->fields.push(ZuMv(key));
    entry->fields.push(ZuMv(value));
  }
  message.fields.push(FieldDesc{name, entryType, {}, number, 3, Type::Message});
  message.nested.push(ZuMv(entry));
}

static bool descriptor(const Zi::Path &path, ZuCSpan fileName = "row.proto")
{
  using namespace Zpb::Reflect;
  DescSet set;
  FileDesc file;
  file.name = fileName;
  file.syntax = "proto3";
  file.package = "demo";
  EnumDesc shade;
  shade.name = "Shade";
  shade.values.push(EnumValueDesc{"SHADE_NONE", 0});
  shade.values.push(EnumValueDesc{"SHADE_BLUE", 5});
  shade.values.push(EnumValueDesc{"SHADE_RED", 7});
  shade.values.push(EnumValueDesc{"SHADE_NEG", -5});
  file.enums.push(ZuMv(shade));
  MsgDesc message;
  message.name = "Row";
  ZuPtr<MsgDesc> child{new MsgDesc};
  child->name = "Child";
  child->fields.push(FieldDesc{"value", {}, {}, 1, 1, Type::UInt32});
  message.nested.push(ZuMv(child));
  EnumDesc kind;
  kind.name = "Kind";
  kind.values.push(EnumValueDesc{"KIND_NONE", 0});
  kind.values.push(EnumValueDesc{"KIND_TWO", 2});
  message.enums.push(ZuMv(kind));
  message.fields.push(FieldDesc{"id", {}, {}, 1, 1, Type::UInt32});
  message.fields.push(FieldDesc{"change", {}, {}, 3, 1, Type::SInt64});
  message.fields.push(FieldDesc{"enabled", {}, {}, 4, 1, Type::Bool});
  message.fields.push(FieldDesc{"samples", {}, {}, 5, 3, Type::SInt32});
  message.fields.push(FieldDesc{"tags", {}, {}, 6, 3, Type::String});
  message.fields.push(FieldDesc{"chunks", {}, {}, 7, 3, Type::Bytes});
  message.fields.push(FieldDesc{"peer", ".demo.Extra", {}, 8, 1, Type::Message});
  message.fields.push(FieldDesc{"peers", ".demo.Extra", {}, 9, 3, Type::Message});
  message.fields.push(FieldDesc{"shade", ".demo.Shade", {}, 10, 1, Type::Enum});
  message.fields.push(FieldDesc{"shades", ".demo.Shade", {}, 11, 3, Type::Enum});
  message.fields.push(FieldDesc{"child", ".demo.Row.Child", {}, 12, 1, Type::Message});
  message.fields.push(FieldDesc{"children", ".demo.Row.Child", {}, 13, 3, Type::Message});
  message.fields.push(FieldDesc{"kind", ".demo.Row.Kind", {}, 14, 1, Type::Enum});
  message.oneofs.push(OneofDesc{"choice"});
  FieldDesc first{"first", ".demo.OptionA", {}, 15, 1, Type::Message};
  first.oneofIndex = 0;
  message.fields.push(ZuMv(first));
  FieldDesc second{"second", ".demo.OptionB", {}, 16, 1, Type::Message};
  second.oneofIndex = 0;
  message.fields.push(ZuMv(second));
  addMap(message, "labels", "LabelsEntry", ".demo.Row.LabelsEntry", 17,
    Type::Int32, Type::String);
  addMap(message, "items", "ItemsEntry", ".demo.Row.ItemsEntry", 18,
    Type::String, Type::Message, ".demo.OptionA");
  addMap(message, "totals", "TotalsEntry", ".demo.Row.TotalsEntry", 19,
    Type::SInt32, Type::Fixed64, {}, true);
  addMap(message, "shadesByName", "ShadesByNameEntry",
    ".demo.Row.ShadesByNameEntry", 20, Type::String, Type::Enum,
    ".demo.Shade");
  message.fields.push(FieldDesc{"flags", {}, {}, 26, 3, Type::Bool});
  file.messages.push(ZuMv(message));
  MsgDesc extra;
  extra.name = "Extra";
  extra.fields.push(FieldDesc{"value", {}, {}, 1, 1, Type::UInt64});
  extra.fields.push(FieldDesc{"back", ".demo.Row", {}, 2, 1, Type::Message});
  file.messages.push(ZuMv(extra));
  MsgDesc optionA;
  optionA.name = "OptionA";
  optionA.fields.push(FieldDesc{"value", {}, {}, 1, 1, Type::UInt32});
  file.messages.push(ZuMv(optionA));
  MsgDesc optionB;
  optionB.name = "OptionB";
  optionB.fields.push(FieldDesc{"label", {}, {}, 1, 1, Type::String});
  file.messages.push(ZuMv(optionB));
  MsgDesc choiceOnly;
  choiceOnly.name = "ChoiceOnly";
  choiceOnly.oneofs.push(OneofDesc{"selection"});
  FieldDesc choiceFirst{"first", ".demo.OptionA", {}, 1, 1,
    Type::Message};
  choiceFirst.oneofIndex = 0;
  choiceOnly.fields.push(ZuMv(choiceFirst));
  FieldDesc choiceSecond{"second", ".demo.OptionB", {}, 2, 1,
    Type::Message};
  choiceSecond.oneofIndex = 0;
  choiceOnly.fields.push(ZuMv(choiceSecond));
  file.messages.push(ZuMv(choiceOnly));
  set.files.push(ZuMv(file));
  Bytes output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto wire = Zpb::save(builder, set);
  return !builder.failed() && write(path,
    {reinterpret_cast<const char *>(wire.data()), wire.length()});
}

static bool optionalDescriptor(const Zi::Path &path)
{
  using namespace Zpb::Reflect;
  DescSet set;
  FileDesc file;
  file.name = "optional.proto";
  file.package = "demo";
  file.syntax = "proto3";
  MsgDesc message;
  message.name = "Optional";
  message.oneofs.push(OneofDesc{"_value"});
  FieldDesc field{"value", {}, {}, 1, 1, Type::UInt32};
  field.oneofIndex = 0;
  field.proto3Optional = true;
  message.fields.push(ZuMv(field));
  file.messages.push(ZuMv(message));
  set.files.push(ZuMv(file));
  Bytes output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto wire = Zpb::save(builder, set);
  return !builder.failed() && Zpb::Reflect::verifySchema(wire) &&
    write(path, {reinterpret_cast<const char *>(wire.data()), wire.length()});
}

static bool importDescriptor(const Zi::Path &path)
{
  using namespace Zpb::Reflect;
  DescSet set;
  FileDesc base;
  base.name = "base.proto";
  base.syntax = "proto3";
  base.package = "shared";
  EnumDesc state;
  state.name = "State";
  state.values.push(EnumValueDesc{"STATE_NONE", 0});
  state.values.push(EnumValueDesc{"STATE_ON", 3});
  base.enums.push(ZuMv(state));
  MsgDesc record;
  record.name = "Record";
  ZuPtr<MsgDesc> detail{new MsgDesc};
  detail->name = "Detail";
  detail->fields.push(FieldDesc{"value", {}, {}, 1, 1, Type::UInt32});
  record.nested.push(ZuMv(detail));
  record.fields.push(FieldDesc{"id", {}, {}, 1, 1, Type::UInt32});
  base.messages.push(ZuMv(record));
  set.files.push(ZuMv(base));
  FileDesc user;
  user.name = "user.proto";
  user.syntax = "proto3";
  user.package = "demo";
  user.dependencies.push("base.proto");
  MsgDesc imported;
  imported.name = "Imported";
  imported.fields.push(FieldDesc{"record", ".shared.Record", {}, 1, 1,
    Type::Message});
  imported.fields.push(FieldDesc{"records", ".shared.Record", {}, 2, 3,
    Type::Message});
  imported.fields.push(FieldDesc{"state", ".shared.State", {}, 3, 1,
    Type::Enum});
  imported.fields.push(FieldDesc{"states", ".shared.State", {}, 4, 3,
    Type::Enum});
  imported.fields.push(FieldDesc{"detail", ".shared.Record.Detail", {}, 5,
    1, Type::Message});
  addMap(imported, "recordsByName", "RecordsByNameEntry",
    ".demo.Imported.RecordsByNameEntry", 6, Type::String, Type::Message,
    ".shared.Record");
  user.messages.push(ZuMv(imported));
  set.files.push(ZuMv(user));
  Bytes output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto wire = Zpb::save(builder, set);
  return !builder.failed() && write(path,
    {reinterpret_cast<const char *>(wire.data()), wire.length()});
}

static bool fixedDescriptor(const Zi::Path &path, bool bad = false)
{
  using namespace Zpb::Reflect;
  DescSet set;
  FileDesc fixed;
  fixed.name = "fixed.proto";
  fixed.syntax = "proto3";
  fixed.package = "Zf";
  MsgDesc value;
  value.name = "Fixed";
  value.fields.push(FieldDesc{"mantissa", {}, {}, 1, 1, Type::Int64});
  value.fields.push(FieldDesc{"ndp", {}, {}, 2, 1,
    bad ? Type::String : Type::UInt32});
  fixed.messages.push(ZuMv(value));
  set.files.push(ZuMv(fixed));
  FileDesc user;
  user.name = "fixed-user.proto";
  user.syntax = "proto3";
  user.package = "demo";
  user.dependencies.push("fixed.proto");
  MsgDesc row;
  row.name = "FixedRow";
  row.fields.push(FieldDesc{"value", ".Zf.Fixed", {}, 1, 1, Type::Message});
  row.fields.push(FieldDesc{"values", ".Zf.Fixed", {}, 2, 3, Type::Message});
  addMap(row, "indexed", "IndexedEntry", ".demo.FixedRow.IndexedEntry",
    3, Type::String, Type::Message, ".Zf.Fixed");
  user.messages.push(ZuMv(row));
  set.files.push(ZuMv(user));
  Bytes output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto wire = Zpb::save(builder, set);
  return !builder.failed() && write(path,
    {reinterpret_cast<const char *>(wire.data()), wire.length()});
}

static void cases(const Zi::Path &dir)
{
  ZuTestScopeRT(cases);
  auto cwd = ZiTestResidue::dir("cases");
  auto tool = executable(ZiFile::append(dir, "../src"), "zprotoc");
  auto consumer = executable(dir, "zprotocconsumer");
  auto imported = executable(dir, "zprotocimport");
  unsigned seq = 0;
  auto path = [&](ZuCSpan name) { return ZiFile::append(cwd, Zi::Path{name}); };
  auto log = [&]() {
    Text name{"child-"};
    name << ZuBox<unsigned>{seq++} << ".log";
    return path(name);
  };
  ZuCheckRT(run(tool, cwd, log(), "--help") == 0);
  ZuCheckRT(write(path("row.proto"),
    "syntax = \"proto3\"; package demo; "
    "enum Shade { SHADE_NONE = 0; SHADE_BLUE = 5; "
    "SHADE_RED = 7; SHADE_NEG = -5; } message Row { "
    "message Child { uint32 value = 1; } "
    "enum Kind { KIND_NONE = 0; KIND_TWO = 2; } "
    "uint32 id = 1; sint64 change = 3; bool enabled = 4; "
    "repeated sint32 samples = 5; repeated string tags = 6; "
    "repeated bytes chunks = 7; Extra peer = 8; "
    "repeated Extra peers = 9; Shade shade = 10; "
    "repeated Shade shades = 11; Child child = 12; "
    "repeated Child children = 13; Kind kind = 14; "
    "oneof choice { OptionA first = 15; OptionB second = 16; } "
    "map<int32, string> labels = 17; "
    "map<string, OptionA> items = 18; "
    "map<sint32, fixed64> totals = 19; "
    "map<string, Shade> shadesByName = 20; "
    "repeated bool flags = 26; } "
    "message Extra { uint64 value = 1; Row back = 2; } "
    "message OptionA { uint32 value = 1; } "
    "message OptionB { string label = 1; } "
    "message ChoiceOnly { oneof selection { "
    "OptionA first = 1; OptionB second = 2; } }"));
  ZuCheckRT(run(tool, cwd, log(), "row.proto", "-o", "row") == 0);
  Text header = read(path("row.hh")), source = read(path("row.cc"));
  ZuCheckRT(header.find("PB::FieldID<3>, PB::ZigZag") >= 0 &&
    header.find("PB::Packed") >= 0 &&
    source.find("ZfStructImpl(Row, PB)") >= 0 &&
    header.find("SourceSyntax") < 0 &&
    header.find("SourceFile") < 0 &&
    header.find("SourcePackage") < 0);
  ZuCheckRT(run(tool, cwd, log(), "row.proto", "-o", "row") == 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  ZuCheckRT(run(tool, cwd, log(), "row.proto", "--syntax", "proto3",
    "-o", "row") == 0 && header == read(path("row.hh")) &&
    source == read(path("row.cc")));
  ZuCheckRT(write(path("renamed.proto"), read(path("row.proto"))) &&
    run(tool, cwd, log(), "renamed.proto", "-o", "row") == 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  Text fileOptions = read(path("row.proto"));
  fileOptions << "\noption java_package = \"other.example\";\n"
    "option optimize_for = CODE_SIZE;\n"
    "option cc_enable_arenas = true;\n";
  ZuCheckRT(write(path("file-options.proto"), fileOptions) &&
    run(tool, cwd, log(), "file-options.proto", "-o", "row") == 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  auto syntaxLog = log();
  ZuCheckRT(run(tool, cwd, syntaxLog, "row.proto", "--syntax", "proto2",
    "-o", "row") != 0 &&
    read(syntaxLog).find("schema syntax mismatch") >= 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  ZuCheckRT(write(path("syntax2.proto"),
    "syntax = \"proto2\"; message Syntax2 { required uint32 id = 1; }") &&
    run(tool, cwd, log(), "syntax2.proto", "--syntax", "proto2",
      "-o", "syntax2") == 0 &&
    read(path("syntax2.hh")).find("Required, PB::FieldID<1>") >= 0);
  ZuCheckRT(write(path("packed2.proto"),
    "syntax = \"proto2\"; message Packed { "
    "repeated sint32 values = 1 [packed = true]; "
    "repeated fixed32 words = 2 [packed = false]; }") &&
    run(tool, cwd, log(), "packed2.proto", "-o", "packed2") == 0);
  Text packedHeader = read(path("packed2.hh"));
  ZuCheckRT(packedHeader.find(
      "PB::FieldID<1>, PB::ZigZag, PB::Packed") >= 0 &&
    packedHeader.find("PB::FieldID<2>, PB::Fixed") >= 0 &&
    packedHeader.find("PB::FieldID<2>, PB::Fixed, PB::Packed") < 0);
  ZuCheckRT(write(path("unpacked3.proto"),
    "syntax = \"proto3\"; message Unpacked { "
    "repeated bool values = 1 [packed = false]; }") &&
    run(tool, cwd, log(), "unpacked3.proto", "-o", "unpacked3") == 0 &&
    read(path("unpacked3.hh")).find("PB::Packed") < 0);
  auto badPackedLog = log();
  ZuCheckRT(write(path("bad-packed.proto"),
    "syntax = \"proto2\"; message Bad { "
    "optional uint32 id = 1 [packed = true]; }") &&
    run(tool, cwd, badPackedLog, "bad-packed.proto", "-o", "row") != 0 &&
    read(badPackedLog).find("invalid schema") >= 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  auto badPackedTypeLog = log();
  ZuCheckRT(write(path("bad-packed-type.proto"),
    "syntax = \"proto2\"; message Bad { "
    "repeated string names = 1 [packed = true]; }") &&
    run(tool, cwd, badPackedTypeLog, "bad-packed-type.proto",
      "-o", "row") != 0 &&
    read(badPackedTypeLog).find("invalid schema") >= 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  ZuCheckRT(write(path("implicit2.proto"),
    "message Implicit2 { required uint32 id = 1; }") &&
    run(tool, cwd, log(), "implicit2.proto", "--syntax", "proto2",
      "-o", "implicit2") == 0 &&
    read(path("implicit2.hh")).find("Required, PB::FieldID<1>") >= 0);
  Text implicitHeader = read(path("implicit2.hh"));
  auto implicitLog = log();
  ZuCheckRT(run(tool, cwd, implicitLog, "implicit2.proto", "--syntax",
    "proto3", "-o", "implicit2") != 0 &&
    read(implicitLog).find("schema syntax mismatch") >= 0 &&
    implicitHeader == read(path("implicit2.hh")));
  ZuCheckRT(write(path("order-base.proto"),
    "syntax = \"proto3\"; message Base { uint32 id = 1; }") &&
    write(path("order-user.proto"),
      "syntax = \"proto3\"; import \"order-base.proto\"; "
      "package demo; message Ordered { Base item = 1; }") &&
    run(tool, cwd, log(), "order-user.proto", "-o", "order-user") == 0 &&
    read(path("order-user.hh")).find("#include \"order-base.hh\"") >= 0);
  ZuCheckRT(write(path("order-late.proto"),
    "syntax = \"proto3\"; message Late { Base item = 1; } "
    "import \"order-base.proto\"; package demo;") &&
    run(tool, cwd, log(), "order-late.proto", "-o", "order-late") == 0 &&
    read(path("order-late.hh")).find("#include \"order-base.hh\"") >= 0);
  auto duplicatePackageLog = log();
  ZuCheckRT(write(path("duplicate-package.proto"),
    "syntax = \"proto3\"; package a; message Row {} package b;") &&
    run(tool, cwd, duplicatePackageLog, "duplicate-package.proto",
      "-o", "row") != 0 &&
    read(duplicatePackageLog).find("duplicate package") >= 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  ZuCheckRT(write(path("multi-oneof.proto"),
    "syntax = \"proto3\"; message ItemA { uint32 value = 1; } "
    "message ItemB { uint32 value = 1; } "
    "message Multi { oneof left { ItemA a = 1; ItemB b = 2; } "
    "oneof right { ItemB c = 3; ItemA d = 4; } }") &&
    run(tool, cwd, log(), "multi-oneof.proto", "-o", "multi") == 0);
  Text multi = read(path("multi.hh"));
  ZuCheckRT(multi.find("Zpb::AsUnion<\"Multi.left\"") >= 0 &&
    multi.find("Zpb::AsUnion<\"Multi.right\"") >= 0 &&
    multi.find("Zpb::Alt<\"a\", 1") >= 0 &&
    multi.find("Zpb::Alt<\"b\", 2") >= 0 &&
    multi.find("Zpb::Alt<\"c\", 3") >= 0 &&
    multi.find("Zpb::Alt<\"d\", 4") >= 0);
  ZuCheckRT(run(tool, cwd, log(), "row.proto", "-o", "a_2Eb") == 0 &&
    run(tool, cwd, log(), "row.proto", "-o", "a.b") == 0 &&
    read(path("a_2Eb.hh")).find("#ifndef Zprotoc_a_5F2Eb_HH") >= 0 &&
    read(path("a.b.hh")).find("#ifndef Zprotoc_a_2Eb_HH") >= 0);
  ZuCheckRT(run(tool, cwd, log(), "row.proto") == 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  ZuCheckRT(run(tool, cwd, log(), "row.proto", "-o", "exported",
    "--export", "ZpbAPI") == 0 &&
    read(path("exported.hh")).find("struct ZpbAPI Row") >= 0 &&
    read(path("exported.hh")).find("ZfStruct(ZpbAPI, (Row, PB)") >= 0);
  auto invalidExportLog = log();
  ZuCheckRT(run(tool, cwd, invalidExportLog, "row.proto", "-o", "row",
    "--export", "namespace") != 0 &&
    read(invalidExportLog).find("invalid export macro name") >= 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  ZuCheckRT(descriptor(path("row.desc")));
  ZuCheckRT(run(tool, cwd, log(), "--descriptor", "--file", "row.proto",
    "--syntax", "proto3", "row.desc", "-o", "row") == 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  ZuCheckRT(descriptor(path("renamed.desc"), "renamed.proto") &&
    run(tool, cwd, log(), "--descriptor", "--file", "renamed.proto",
      "--syntax", "proto3", "renamed.desc", "-o", "row") == 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  auto descSyntaxLog = log();
  ZuCheckRT(run(tool, cwd, descSyntaxLog, "--descriptor", "--file",
    "row.proto", "--syntax", "proto2", "row.desc", "-o", "row") != 0 &&
    read(descSyntaxLog).find("schema syntax mismatch") >= 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  ZuCheckRT(run(tool, cwd, log(), "--descriptor", "row.desc", "-o", "row") == 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  auto missingLog = log();
  ZuCheckRT(run(tool, cwd, missingLog, "--descriptor", "--file",
    "missing.proto", "row.desc", "-o", "row") != 0 &&
    read(missingLog).find("descriptor file not found") >= 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  ZuCheckRT(write(path("bad.desc"), "not a descriptor"));
  auto badDescLog = log();
  ZuCheckRT(run(tool, cwd, badDescLog, "--descriptor", "bad.desc", "-o",
    "row") != 0 && read(badDescLog).find("invalid descriptor set") >= 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  const uint8_t serviceDesc[] = {
    0x0a, 19, 0x0a, 7, 'x', '.', 'p', 'r', 'o', 't', 'o',
    0x62, 6, 'p', 'r', 'o', 't', 'o', '3', 0x32, 0};
  auto serviceDescLog = log();
  ZuCheckRT(write(path("service.desc"),
    {reinterpret_cast<const char *>(serviceDesc), sizeof(serviceDesc)}) &&
    run(tool, cwd, serviceDescLog, "--descriptor", "service.desc", "-o",
      "row") != 0 &&
    read(serviceDescLog).find("unsupported service descriptor") >= 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  ZuCheckRT(write(path("duplicate.proto"),
    "syntax = \"proto3\"; package demo; "
    "message Duplicate { uint32 value = 1; } "
    "message Duplicate { uint32 other = 2; }"));
  auto duplicateLog = log();
  ZuCheckRT(run(tool, cwd, duplicateLog, "duplicate.proto", "-o", "row") != 0 &&
    read(duplicateLog).find("duplicate protobuf declaration") >= 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  auto editionLog = log();
  ZuCheckRT(write(path("edition.proto"),
    "edition = \"2023\"; message Row { uint32 id = 1; }") &&
    run(tool, cwd, editionLog, "edition.proto", "-o", "row") != 0 &&
    read(editionLog).find("unsupported edition schema") >= 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  auto serviceLog = log();
  ZuCheckRT(write(path("service.proto"),
    "syntax = \"proto3\"; message Row { uint32 id = 1; } "
    "service API { rpc Read (Row) returns (Row); }") &&
    run(tool, cwd, serviceLog, "service.proto", "-o", "row") != 0 &&
    read(serviceLog).find("unsupported file declaration") >= 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  auto groupLog = log();
  ZuCheckRT(write(path("group.proto"),
    "syntax = \"proto2\"; message Row { "
    "optional group Old = 1 { optional uint32 id = 2; } }") &&
    run(tool, cwd, groupLog, "group.proto", "-o", "row") != 0 &&
    read(groupLog).find("unsupported group field") >= 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  auto extensionsLog = log();
  ZuCheckRT(write(path("extensions.proto"),
    "syntax = \"proto2\"; message Row { extensions 100 to 199; }") &&
    run(tool, cwd, extensionsLog, "extensions.proto", "-o", "row") != 0 &&
    read(extensionsLog).find("unsupported message declaration") >= 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  auto publicLog = log();
  ZuCheckRT(write(path("public.proto"),
    "syntax = \"proto3\"; import public \"base.proto\"; "
    "message Row { uint32 id = 1; }") &&
    run(tool, cwd, publicLog, "public.proto", "-o", "row") != 0 &&
    read(publicLog).find("unsupported import qualifier") >= 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  auto fieldOptionLog = log();
  ZuCheckRT(write(path("field-option.proto"),
    "syntax = \"proto3\"; message Row { "
    "uint32 id = 1 [deprecated = true]; }") &&
    run(tool, cwd, fieldOptionLog, "field-option.proto", "-o", "row") != 0 &&
    read(fieldOptionLog).find("unsupported field option") >= 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  auto fileOptionLog = log();
  ZuCheckRT(write(path("custom-file-option.proto"),
    "syntax = \"proto3\"; option (app.custom) = true; "
    "message Row { uint32 id = 1; }") &&
    run(tool, cwd, fileOptionLog, "custom-file-option.proto", "-o",
      "row") != 0 &&
    read(fileOptionLog).find("unsupported file option") >= 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  auto optionValueLog = log();
  ZuCheckRT(write(path("bad-file-option.proto"),
    "syntax = \"proto3\"; option cc_enable_arenas = \"true\"; "
    "message Row { uint32 id = 1; }") &&
    run(tool, cwd, optionValueLog, "bad-file-option.proto", "-o",
      "row") != 0 &&
    read(optionValueLog).find("invalid file option value") >= 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  ZuCheckRT(write(path("bad-file-option.proto"),
    "syntax = \"proto3\"; option optimize_for = \"SPEED\"; "
    "message Row { uint32 id = 1; }") &&
    run(tool, cwd, optionValueLog, "bad-file-option.proto", "-o",
      "row") != 0 &&
    read(optionValueLog).find("invalid file option value") >= 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  ZuCheckRT(write(path("bad-fixed.proto"),
    "syntax = \"proto3\"; package Zf; "
    "message Fixed { int64 mantissa = 1; string ndp = 2; }") &&
    write(path("bad-fixed-user.proto"),
      "syntax = \"proto3\"; package demo; "
      "import \"bad-fixed.proto\"; "
      "message Row { Zf.Fixed value = 1; }"));
  auto badFixedLog = log();
  ZuCheckRT(run(tool, cwd, badFixedLog, "-I", ".",
    "bad-fixed-user.proto", "-o", "row") != 0 &&
    read(badFixedLog).find("invalid Zf.Fixed mapping") >= 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  auto badDateLog = log();
  ZuCheckRT(write(path("bad-datetime.proto"),
    "syntax = \"proto3\"; package Zf; "
    "message DateTime { int32 julian = 1; int32 sec = 2; "
    "uint64 nsec = 3; }") &&
    run(tool, cwd, badDateLog, "bad-datetime.proto", "-o", "row") != 0 &&
    read(badDateLog).find("invalid Zf.DateTime mapping") >= 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  auto badIPLog = log();
  ZuCheckRT(write(path("bad-ip.proto"),
    "syntax = \"proto3\"; package Zi; "
    "message IP { string addr = 1; }") &&
    run(tool, cwd, badIPLog, "bad-ip.proto", "-o", "row") != 0 &&
    read(badIPLog).find("invalid Zi.IP mapping") >= 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  auto badBitmapLog = log();
  ZuCheckRT(write(path("bad-bitmap.proto"),
    "syntax = \"proto3\"; package Zf; "
    "message Bitmap { repeated bytes words = 1; }") &&
    run(tool, cwd, badBitmapLog, "bad-bitmap.proto", "-o", "row") != 0 &&
    read(badBitmapLog).find("invalid Zf.Bitmap mapping") >= 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  auto badHwBitmapLog = log();
  ZuCheckRT(write(path("bad-hwbitmap.proto"),
    "syntax = \"proto3\"; package Zf; "
    "message HwBitmap { string wrong = 1; }") &&
    run(tool, cwd, badHwBitmapLog, "bad-hwbitmap.proto", "-o", "row") != 0 &&
    read(badHwBitmapLog).find("invalid Zf.HwBitmap mapping") >= 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  ZuCheckRT(write(path("same-fixed.proto"),
    "syntax = \"proto3\"; package Zf; "
    "message Fixed { int64 mantissa = 1; uint32 ndp = 2; } "
    "message Holder { map<string, Fixed> indexed = 1; }") &&
    run(tool, cwd, log(), "same-fixed.proto", "-o", "same-fixed") == 0 &&
    read(path("same-fixed.hh")).find("ZuFixed") >= 0 &&
    read(path("same-fixed.hh")).find("struct Holder") >= 0);
  ZuCheckRT(ZiFile::mkdir(path("fixed-imports")) == Zi::OK &&
    write(path("fixed-imports/fixed.proto"),
      "syntax = \"proto3\"; package Zf; "
      "message Fixed { int64 mantissa = 1; uint32 ndp = 2; }") &&
    write(path("fixed-user.proto"),
      "syntax = \"proto3\"; package demo; import \"fixed.proto\"; "
      "message FixedRow { Zf.Fixed value = 1; "
      "repeated Zf.Fixed values = 2; "
      "map<string, Zf.Fixed> indexed = 3; }") &&
    run(tool, cwd, log(), "-I", "fixed-imports", "fixed-user.proto",
      "-o", "fixed-row") == 0);
  Text fixedHeader = read(path("fixed-row.hh"));
  Text fixedSource = read(path("fixed-row.cc"));
  ZuCheckRT(fixedHeader.find("ZuFixed") >= 0 &&
    fixedHeader.find("#include \"fixed.hh\"") < 0 &&
    fixedHeader.find("struct FixedRow") >= 0 &&
    fixedHeader.find("struct Fixed {") < 0);
  ZuCheckRT(fixedDescriptor(path("fixed.desc")) &&
    run(tool, cwd, log(), "--descriptor", "--file", "fixed-user.proto",
      "fixed.desc", "-o", "fixed-row") == 0 &&
    fixedHeader == read(path("fixed-row.hh")) &&
    fixedSource == read(path("fixed-row.cc")));
  auto badFixedDescLog = log();
  ZuCheckRT(fixedDescriptor(path("bad-fixed.desc"), true) &&
    run(tool, cwd, badFixedDescLog, "--descriptor", "--file",
      "fixed-user.proto", "bad-fixed.desc", "-o", "fixed-row") != 0 &&
    read(badFixedDescLog).find("invalid Zf.Fixed mapping") >= 0 &&
    fixedHeader == read(path("fixed-row.hh")) &&
    fixedSource == read(path("fixed-row.cc")));
  ZuCheckRT(optionalDescriptor(path("optional.desc")));
  auto optionalLog = log();
  ZuCheckRT(run(tool, cwd, optionalLog, "--descriptor", "optional.desc",
    "-o", "row") != 0 &&
    read(optionalLog).find("unsupported proto3 optional field") >= 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  ZuCheckRT(write(path("optional.proto"),
    "syntax = \"proto3\"; package demo; "
    "message Optional { optional uint32 value = 1; }") &&
    run(tool, cwd, optionalLog, "optional.proto", "-o", "row") != 0 &&
    read(optionalLog).find("unsupported proto3 optional field") >= 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  ZuCheckRT(ZiFile::mkdir(path("imports")) == Zi::OK &&
    write(path("imports/base.proto"),
      "syntax = \"proto3\"; package shared; "
      "enum State { STATE_NONE = 0; STATE_ON = 3; } "
      "message Record { message Detail { uint32 value = 1; } "
      "uint32 id = 1; }") &&
    write(path("user.proto"),
      "syntax = \"proto3\"; package demo; import \"base.proto\"; "
      "message Imported { shared.Record record = 1; "
      "repeated shared.Record records = 2; shared.State state = 3; "
      "repeated shared.State states = 4; "
      "shared.Record.Detail detail = 5; "
      "map<string, shared.Record> recordsByName = 6; }"));
  ZuCheckRT(run(tool, cwd, log(), "-I", "imports", "user.proto", "-o",
    "imported") == 0);
  Text importHeader = read(path("imported.hh"));
  Text importSource = read(path("imported.cc"));
  ZuCheckRT(importHeader.find("#include \"base.hh\"") >= 0 &&
    importHeader.find("ZuPtr<::shared::Record>") >= 0);
  ZuCheckRT(write(path("imports/local.proto"),
    "syntax = \"proto3\"; package demo; import \"base.proto\"; "
    "message Local { shared.Record record = 1; }") &&
    run(tool, cwd, log(), "imports/local.proto", "-o", "local") == 0 &&
    read(path("local.hh")).find("#include \"base.hh\"") >= 0);
  ZuCheckRT(importDescriptor(path("import.desc")) &&
    run(tool, cwd, log(), "--descriptor", "--file", "user.proto",
      "import.desc", "-o", "imported") == 0 &&
    importHeader == read(path("imported.hh")) &&
    importSource == read(path("imported.cc")));
  auto missingImportLog = log();
  ZuCheckRT(run(tool, cwd, missingImportLog, "user.proto", "-o",
    "imported") != 0 &&
    read(missingImportLog).find("import schema not found") >= 0 &&
    importHeader == read(path("imported.hh")) &&
    importSource == read(path("imported.cc")));
  ZuCheckRT(write(path("imports/base.proto"),
    "syntax = \"proto3\"; package shared; import \"user.proto\"; "
    "message Record { uint32 id = 1; }"));
  auto cycleLog = log();
  ZuCheckRT(run(tool, cwd, cycleLog, "-I", "imports", "-I", ".",
    "user.proto", "-o", "imported") != 0 &&
    read(cycleLog).find("cyclic schema import") >= 0 &&
    importHeader == read(path("imported.hh")) &&
    importSource == read(path("imported.cc")));
  ZuCheckRT(ZiFile::mkdir(path("collisions")) == Zi::OK &&
    write(path("collisions/base.proto"),
      "syntax = \"proto3\"; package demo; "
      "message ZpA0_0 { uint32 id = 1; }") &&
    write(path("collision-import.proto"),
      "syntax = \"proto3\"; package demo; import \"base.proto\"; "
      "message Row { repeated ZpA0_0 children = 1; }"));
  auto importedNameLog = log();
  ZuCheckRT(run(tool, cwd, importedNameLog, "-I", "collisions",
    "collision-import.proto", "-o", "imported") != 0 &&
    read(importedNameLog).find("generated array name collides with message") >= 0 &&
    importHeader == read(path("imported.hh")) &&
    importSource == read(path("imported.cc")));
  ZuCheckRT(write(path("collisions/keyword.proto"),
    "syntax = \"proto3\"; package demo; message class { uint32 id = 1; }") &&
    write(path("keyword-import.proto"),
      "syntax = \"proto3\"; package demo; import \"keyword.proto\"; "
      "message Row { class child = 1; }"));
  auto importedKeywordLog = log();
  ZuCheckRT(run(tool, cwd, importedKeywordLog, "-I", "collisions",
    "keyword-import.proto", "-o", "imported") != 0 &&
    read(importedKeywordLog).find("invalid imported C++ type name") >= 0 &&
    importHeader == read(path("imported.hh")) &&
    importSource == read(path("imported.cc")));
  ZuCheckRT(write(path("collisions/package.proto"),
    "syntax = \"proto3\"; package class; "
    "message Foreign { uint32 id = 1; }") &&
    write(path("package-import.proto"),
      "syntax = \"proto3\"; import \"package.proto\"; "
      "message Row { class.Foreign child = 1; }"));
  auto importedPackageLog = log();
  ZuCheckRT(run(tool, cwd, importedPackageLog, "-I", "collisions",
    "package-import.proto", "-o", "imported") != 0 &&
    read(importedPackageLog).find("invalid imported C++ package name") >= 0 &&
    importHeader == read(path("imported.hh")) &&
    importSource == read(path("imported.cc")));
  ZuCheckRT(write(path("collisions/arrays.proto"),
      "syntax = \"proto3\"; package demo; "
      "message Base { repeated Base children = 1; }") &&
    write(path("collision-arrays.proto"),
      "syntax = \"proto3\"; package demo; import \"arrays.proto\"; "
      "message Row { repeated Base children = 1; }"));
  auto importedArrayLog = log();
  ZuCheckRT(run(tool, cwd, importedArrayLog, "-I", "collisions",
    "collision-arrays.proto", "-o", "imported") != 0 &&
    read(importedArrayLog).find("generated array name collision") >= 0 &&
    importHeader == read(path("imported.hh")) &&
    importSource == read(path("imported.cc")));
  ZuCheckRT(write(path("collisions/namespace.proto"),
      "syntax = \"proto3\"; package demo.Row; "
      "message Record { uint32 id = 1; }") &&
    write(path("collision-namespace.proto"),
      "syntax = \"proto3\"; package demo; import \"namespace.proto\"; "
      "message Row { uint32 id = 1; }"));
  auto importedNamespaceLog = log();
  ZuCheckRT(run(tool, cwd, importedNamespaceLog, "-I", "collisions",
    "collision-namespace.proto", "-o", "imported") != 0 &&
    read(importedNamespaceLog).find("generated namespace name collision") >= 0 &&
    importHeader == read(path("imported.hh")) &&
    importSource == read(path("imported.cc")));
  ZuCheckRT(write(path("keyword.proto"),
    "syntax = \"proto3\"; package namespace; message Row { uint32 id = 1; }"));
  auto keywordLog = log();
  ZuCheckRT(run(tool, cwd, keywordLog, "keyword.proto", "-o", "row") != 0 &&
    read(keywordLog).find("invalid C++ package name") >= 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  ZuCheckRT(write(path("enum-only.proto"),
    "syntax = \"proto3\"; package demo; "
    "enum Only { ONLY_NONE = 0; ONLY_TWO = 2; }") &&
    run(tool, cwd, log(), "enum-only.proto", "-o", "enum-only") == 0 &&
    read(path("enum-only.hh")).find("namespace Only") >= 0 &&
    read(path("enum-only.cc")).find("Only::Map::v2s") >= 0);
  ZuCheckRT(write(path("bad-enum.proto"),
    "syntax = \"proto3\"; enum Bad { Map = 0; }"));
  auto badEnumLog = log();
  ZuCheckRT(run(tool, cwd, badEnumLog, "bad-enum.proto", "-o", "row") != 0 &&
    read(badEnumLog).find("invalid C++ enum value name") >= 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  ZuCheckRT(write(path("collision.proto"),
    "syntax = \"proto3\"; message Row { uint32 Row = 1; }"));
  auto collisionLog = log();
  ZuCheckRT(run(tool, cwd, collisionLog, "collision.proto", "-o", "row") != 0 &&
    read(collisionLog).find("invalid C++ field name") >= 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  ZuCheckRT(write(path("heap-collision.proto"),
    "syntax = \"proto3\"; message Zprotoc_ { uint32 value = 1; }"));
  auto heapCollisionLog = log();
  ZuCheckRT(run(tool, cwd, heapCollisionLog, "heap-collision.proto",
    "-o", "row") != 0 &&
    read(heapCollisionLog).find("generated namespace name collision") >= 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  ZuCheckRT(write(path("helper.proto"),
    "syntax = \"proto3\"; message Row { repeated Extra peers = 1; } "
    "message ZpA0_0 { uint32 value = 1; } message Extra { uint32 value = 1; }"));
  auto helperLog = log();
  ZuCheckRT(run(tool, cwd, helperLog, "helper.proto", "-o", "row") != 0 &&
    read(helperLog).find("generated array name collides with message") >= 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  ZuCheckRT(write(path("nested-collision.proto"),
    "syntax = \"proto3\"; message Outer { "
    "message Child { uint32 value = 1; } } "
    "message Outer_Child { uint32 value = 1; }"));
  auto nestedCollisionLog = log();
  ZuCheckRT(run(tool, cwd, nestedCollisionLog, "nested-collision.proto",
    "-o", "row") != 0 &&
    read(nestedCollisionLog).find("generated message name collision") >= 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  ZuCheckRT(write(path("bad-oneof.proto"),
    "syntax = \"proto3\"; message Bad { "
    "oneof choice { uint32 scalar = 1; } }"));
  auto badOneofLog = log();
  ZuCheckRT(run(tool, cwd, badOneofLog, "bad-oneof.proto", "-o", "row") != 0 &&
    read(badOneofLog).find("unsupported oneof alternative") >= 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  ZuCheckRT(write(path("cyclic-oneof.proto"),
    "syntax = \"proto3\"; "
    "message A { oneof choice { B b = 1; } } "
    "message B { oneof choice { A a = 1; } }"));
  auto cyclicOneofLog = log();
  ZuCheckRT(run(tool, cwd, cyclicOneofLog, "cyclic-oneof.proto", "-o", "row")
    != 0 &&
    read(cyclicOneofLog).find("cyclic oneof value dependency") >= 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  ZuCheckRT(write(path("oneof-name.proto"),
    "syntax = \"proto3\"; "
    "message Row { oneof choice { Item item = 1; } } "
    "message Item { uint32 value = 1; } "
    "message ZpU0_0 { uint32 value = 1; }"));
  auto oneofNameLog = log();
  ZuCheckRT(run(tool, cwd, oneofNameLog, "oneof-name.proto", "-o", "row")
    != 0 &&
    read(oneofNameLog).find("generated union name collision") >= 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  ZuCheckRT(write(path("oneof-duplicate.proto"),
    "syntax = \"proto3\"; message Item { uint32 value = 1; } "
    "message Row { oneof choice { Item first = 1; } "
    "oneof choice { Item second = 2; } }"));
  auto oneofDuplicateLog = log();
  ZuCheckRT(run(tool, cwd, oneofDuplicateLog, "oneof-duplicate.proto",
    "-o", "row") != 0 &&
    read(oneofDuplicateLog).find("duplicate oneof name") >= 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  ZuCheckRT(write(path("oneof-field-small.proto"),
    "syntax = \"proto3\"; message Item { uint32 value = 1; } "
    "message Row { oneof a { Item a1 = 1; } "
    "oneof b { Item b1 = 2; } string b = 3; }"));
  auto oneofFieldSmallLog = log();
  ZuCheckRT(run(tool, cwd, oneofFieldSmallLog, "oneof-field-small.proto",
    "-o", "row") != 0 &&
    read(oneofFieldSmallLog).find("field collides with oneof name") >= 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  ZuCheckRT(write(path("oneof-field.proto"),
    "syntax = \"proto3\"; message Item { uint32 value = 1; } "
    "message Row { oneof a { Item a1 = 1; } "
    "oneof b { Item b1 = 2; } oneof c { Item c1 = 3; } "
    "oneof d { Item d1 = 4; } oneof e { Item e1 = 5; } "
    "oneof f { Item f1 = 6; } oneof g { Item g1 = 7; } "
    "oneof h { Item h1 = 8; } oneof i { Item i1 = 9; } "
    "string h = 10; }"));
  auto oneofFieldLog = log();
  ZuCheckRT(run(tool, cwd, oneofFieldLog, "oneof-field.proto", "-o", "row")
    != 0 &&
    read(oneofFieldLog).find("field collides with oneof name") >= 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  ZuCheckRT(write(path("field-oneof-small.proto"),
    "syntax = \"proto3\"; message Item { uint32 value = 1; } "
    "message Row { string clash = 1; oneof clash { Item item = 2; } }"));
  auto fieldOneofSmallLog = log();
  ZuCheckRT(run(tool, cwd, fieldOneofSmallLog, "field-oneof-small.proto",
    "-o", "row") != 0 &&
    read(fieldOneofSmallLog).find("field collides with oneof name") >= 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  Text fieldOneofLarge{
    "syntax = \"proto3\"; message Item { uint32 value = 1; } "
    "message Row { "};
  for (unsigned i = 0; i < 8; ++i)
    fieldOneofLarge << "string f" << i << " = " << i + 1 << "; ";
  Text firstQuery{fieldOneofLarge};
  firstQuery << "oneof f7 { Item item = 9; } }";
  ZuCheckRT(write(path("field-oneof-first.proto"), firstQuery));
  auto fieldOneofFirstLog = log();
  ZuCheckRT(run(tool, cwd, fieldOneofFirstLog, "field-oneof-first.proto",
    "-o", "row") != 0 &&
    read(fieldOneofFirstLog).find("field collides with oneof name") >= 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  fieldOneofLarge << "oneof choice { Item item = 9; } "
    "oneof f7 { Item other = 10; } }";
  ZuCheckRT(write(path("field-oneof-reuse.proto"), fieldOneofLarge));
  auto fieldOneofReuseLog = log();
  ZuCheckRT(run(tool, cwd, fieldOneofReuseLog, "field-oneof-reuse.proto",
    "-o", "row") != 0 &&
    read(fieldOneofReuseLog).find("field collides with oneof name") >= 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  ZuCheckRT(write(path("map-name.proto"),
    "syntax = \"proto3\"; "
    "message Row { map<int32, string> labels = 1; } "
    "message ZpM0_0 { uint32 value = 1; }"));
  auto mapNameLog = log();
  ZuCheckRT(run(tool, cwd, mapNameLog, "map-name.proto", "-o", "row") != 0 &&
    read(mapNameLog).find("generated map name collision") >= 0 &&
    header == read(path("row.hh")) && source == read(path("row.cc")));
  ZuCheckRT(write(path("bad.proto"),
    "syntax = \"proto3\"; message Bad { required uint32 id = 1; }"));
  ZuCheckRT(write(path("keep.hh"), "old header") &&
    write(path("keep.cc"), "old source"));
  auto badLog = log();
  ZuCheckRT(run(tool, cwd, badLog, "bad.proto", "-o", "keep") != 0 &&
    read(badLog).find("unsupported field label") >= 0 &&
    read(path("keep.hh")) == "old header" &&
    read(path("keep.cc")) == "old source");
  ZuCheckRT(write(path("closed-base.proto"),
    "syntax = \"proto2\"; enum State { STATE_NONE = 0; }") &&
    write(path("closed-field.proto"),
      "syntax = \"proto3\"; import \"closed-base.proto\"; "
      "message Bad { State state = 1; }") &&
    write(path("closed-map.proto"),
      "syntax = \"proto3\"; import \"closed-base.proto\"; "
      "message Bad { map<string, State> states = 1; }"));
  auto closedFieldLog = log();
  ZuCheckRT(run(tool, cwd, closedFieldLog, "closed-field.proto", "-o",
    "keep") != 0 &&
    read(closedFieldLog).find("proto2 enum in proto3 field") >= 0 &&
    read(path("keep.hh")) == "old header" &&
    read(path("keep.cc")) == "old source");
  auto closedMapLog = log();
  ZuCheckRT(run(tool, cwd, closedMapLog, "closed-map.proto", "-o",
    "keep") != 0 &&
    read(closedMapLog).find("proto2 enum in proto3 field") >= 0 &&
    read(path("keep.hh")) == "old header" &&
    read(path("keep.cc")) == "old source");
  ZuCheckRT(write(path("bad-default.proto"),
    "syntax = \"proto2\"; message Bad { "
    "optional uint32 id = 1 [default = 4294967295]; }"));
  auto badDefaultLog = log();
  ZuCheckRT(run(tool, cwd, badDefaultLog, "bad-default.proto", "-o",
    "keep") != 0 &&
    read(badDefaultLog).find("unsupported field schema") >= 0 &&
    read(path("keep.hh")) == "old header" &&
    read(path("keep.cc")) == "old source");
  ZuCheckRT(write(path("bad-escape.proto"),
    "syntax = \"proto2\"; message Bad { "
    "optional string label = 1 [default = \"\\q\"]; }"));
  auto badEscapeLog = log();
  ZuCheckRT(run(tool, cwd, badEscapeLog, "bad-escape.proto", "-o",
    "keep") != 0 &&
    read(badEscapeLog).find("invalid string default") >= 0 &&
    read(path("keep.hh")) == "old header" &&
    read(path("keep.cc")) == "old source");
  ZuCheckRT(write(path("blocked.hh"), "keep header") &&
    ZiFile::mkdir(path("blocked.cc")) == Zi::OK);
  auto blockedLog = log();
  ZuCheckRT(run(tool, cwd, blockedLog, "row.proto", "-o", "blocked") != 0 &&
    read(blockedLog).find("output destination is a directory") >= 0 &&
    read(path("blocked.hh")) == "keep header" &&
    ZiStat{path("blocked.cc")}.isdir());
  ZuCheckRT(!ZiTestResidue::glob(cwd, "blocked.hh.zprotoc.").length());
  ZuCheckRT(run(consumer, cwd, log()) == 0);
  ZuCheckRT(run(imported, cwd, log()) == 0);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZiTestResidue::init("zprotoctest");
  Zi::Path self{argv[0]};
  if (!ZiFile::absolute(self)) self = ZiFile::append(ZiFile::cwd(), self);
  auto dir = ZiFile::dirname(self);
  if (ZiFile::leafname(dir) == ".libs") dir = ZiFile::dirname(dir);
  ZuTestMain();
  ZuTestCall(cases, dir);
}
