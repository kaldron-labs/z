//  -*- mode:c++; indent-tabs-mode:t; tab-width=8; c-basic-offset=2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZpbReflect.hh>

#include <proto2.hh>

using namespace ZuTestUtil;

ZuDerive(Output,
  (ZtArray<uint8_t, ZtArrayHeapID<"zprotocproto2.Output">>));

static void exchange()
{
  ZuTestScope(exchange);
  legacy::Row fresh;
  ZuCheck(ZuNull(fresh.id) &&
    fresh.state == legacy::State::STATE_READY && !fresh.active &&
    fresh.retry == 9 && fresh.visible && fresh.offset == -7 &&
    fresh.gain == 1.25f && fresh.factor == -2.5 &&
    fresh.label == "a\n\"b" && fresh.raw.length() == 2 &&
    fresh.raw[0] == 1 && fresh.raw[1] == 255 &&
    fresh.icon == "\xe2\x98\xba" && fresh.nulled.length() == 3 &&
    !memcmp(fresh.nulled.data(), "a\0b", 3) &&
    ZuFP<float>::inf(fresh.ceiling) && fresh.floor == -ZuFP<double>::inf());
  auto rawDefault = legacy::ZfField_Row_raw::deflt();
  ZuCheck(legacy::ZfField_Row_label::deflt() == "a\n\"b" &&
    rawDefault.length() == 2 && rawDefault[0] == 1 &&
    rawDefault[1] == 255);
  Output freshOutput;
  Zpb::ArrayStorage freshStorage{freshOutput};
  Zpb::Builder freshBuilder{freshStorage};
  ZuCheck(!Zpb::save(freshBuilder, fresh).length() &&
    freshBuilder.error() == Zpb::Error::Required);
  const uint8_t missing[] = {0x10, 1};
  ZuCheck(!Zpb::verify<legacy::Row>({missing, sizeof(missing)}));
  const uint8_t wire[] = {0x08, 7, 0x10, 1, 0x18, 3, 0x18, 0x04, 0x20, 3};
  ZuCheck(Zpb::verify<legacy::Row>({wire, sizeof(wire)}));
  auto row = Zpb::load<legacy::Row>({wire, sizeof(wire)});
  ZuCheck(row.id == 7 && row.active && row.samples.length() == 2 &&
    row.samples[0] == -2 && row.samples[1] == 2 &&
    row.state == legacy::State::STATE_READY && row.retry == 9 &&
    row.visible && row.offset == -7 && row.gain == 1.25f &&
    row.factor == -2.5 && row.label == "a\n\"b" &&
    row.raw.length() == 2 && row.raw[0] == 1 && row.raw[1] == 255 &&
    row.icon == "\xe2\x98\xba" && row.nulled.length() == 3 &&
    !memcmp(row.nulled.data(), "a\0b", 3) &&
    ZuFP<float>::inf(row.ceiling) && row.floor == -ZuFP<double>::inf());
  Output output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto encoded = Zpb::save(builder, row);
  const uint8_t expected[] = {
    0x08, 7, 0x10, 1, 0x1a, 2, 3, 0x04, 0x20, 3,
    0x28, 9, 0x30, 1, 0x38, 13,
    0x45, 0, 0, 0xa0, 0x3f,
    0x49, 0, 0, 0, 0, 0, 0, 0x04, 0xc0,
    0x52, 4, 'a', '\n', '"', 'b', 0x5a, 2, 1, 255,
    0x62, 3, 0xe2, 0x98, 0xba,
    0x6a, 3, 'a', 0, 'b',
    0x75, 0, 0, 0x80, 0x7f,
    0x79, 0, 0, 0, 0, 0, 0, 0xf0, 0xff};
  ZuCheck(!builder.failed() && encoded.length() == sizeof(expected) &&
    !memcmp(encoded.data(), expected, sizeof(expected)));
  row.id = ZuCmp<uint32_t>::null();
  Output absent;
  Zpb::ArrayStorage absentStorage{absent};
  Zpb::Builder absentBuilder{absentStorage};
  ZuCheck(!Zpb::save(absentBuilder, row).length() &&
    absentBuilder.error() == Zpb::Error::Required);
}

static void reservedExport()
{
  ZuTestScope(reservedExport);
  using namespace Zpb::Reflect;
  DescSet descriptor;
  ZuCheck(exportSchema<legacy::Retained>(descriptor,
    "retained.proto", "legacy", Syntax::Proto2));
  const auto &message = descriptor.files[0].messages[0];
  ZuCheck(message.fields.length() == 2 &&
    message.fields[0].number == 1 && message.fields[1].number == 5 &&
    message.reservedRanges.length() == 1 &&
    message.reservedRanges[0].start == 2 &&
    message.reservedRanges[0].end == 5 &&
    message.reservedNames.length() == 1 &&
    message.reservedNames[0] == "former");
  Output output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto wire = Zpb::save(builder, descriptor);
  ZuCheck(!builder.failed() && verifySchema(wire));
  Schema schema{wire};
  ProtoText proto;
  ZuCheck(emitProto(proto, schema, *schema.file("retained.proto"),
      fileSyntax(*schema.file("retained.proto"))) &&
    strstr(proto.data(), "reserved 2 to 4;") &&
    strstr(proto.data(), "reserved \"former\";"));
}

static void rowExport()
{
  ZuTestScope(rowExport);
  using namespace Zpb::Reflect;
  DescSet descriptor;
  ZuCheck(exportSchema<legacy::Row>(descriptor, "row-export.proto", "legacy",
    Syntax::Proto2));
  const auto &file = descriptor.files[0];
  ZuCheck(file.enums.length() == 1 && file.enums[0].name == "State" &&
    file.messages[0].reservedRanges.length() == 1 &&
    file.messages[0].reservedNames.length() == 1);
  const auto &state = file.messages[0].fields[3];
  ZuCheck(state.type == Type::Enum && state.typeName == ".legacy.State" &&
    state.defaultValue == "STATE_READY");
  ZuCheck(packedOption(file.messages[0].fields[2]) == 1);
  Output output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto wire = Zpb::save(builder, descriptor);
  ZuCheck(!builder.failed() && verifySchema(wire));
  Schema schema{wire};
  ProtoText proto;
  ZuCheck(emitProto(proto, schema, *schema.file("row-export.proto"),
      fileSyntax(*schema.file("row-export.proto"))) &&
    strstr(proto.data(), "enum State {") &&
    strstr(proto.data(), "default = STATE_READY") &&
    strstr(proto.data(), "samples = 3 [packed = true]") &&
    strstr(proto.data(), "reserved 20 to 22;"));
}

static void choice()
{
  ZuTestScope(choice);
  const uint8_t malformed[] = {
    0x22, 0x02, 0x08, 0x80, 0x4a, 0x02, 0x08, 0x09};
  const uint8_t absentRequired[] = {
    0x22, 0x00, 0x4a, 0x02, 0x08, 0x09};
  ZuCheck(!Zpb::verify<legacy::ChoiceRow>(
    {malformed, sizeof(malformed)}));
  ZuCheck(Zpb::verify<legacy::ChoiceRow>(
    {absentRequired, sizeof(absentRequired)}));
  auto row = Zpb::load<legacy::ChoiceRow>(
    {absentRequired, sizeof(absentRequired)});
  ZuCheck(row.choice.type() == 2 && row.choice.p<2>().id == 9);
  Output output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto saved = Zpb::save(builder, row);
  const uint8_t expected[] = {0x4a, 0x02, 0x08, 0x09};
  ZuCheck(!builder.failed() && saved.length() == sizeof(expected) &&
    !memcmp(saved.data(), expected, sizeof(expected)));
  const uint8_t mapWire[] = {
    0x4a, 0x02, 0x08, 0x09,
    0x52, 0x05, 0x0a, 0x01, 'x', 0x10, 0x03,
    0x52, 0x05, 0x0a, 0x01, 'x', 0x10, 0x07};
  ZuCheck(Zpb::verify<legacy::ChoiceRow>({mapWire, sizeof(mapWire)}));
  auto mapped = Zpb::load<legacy::ChoiceRow>({mapWire, sizeof(mapWire)});
  ZuCheck(mapped.choice.type() == 2 && mapped.choice.p<2>().id == 9 &&
    mapped.counts.count_() == 1 && mapped.counts.findVal("x") == 7);
  Output mappedOutput;
  Zpb::ArrayStorage mappedStorage{mappedOutput};
  Zpb::Builder mappedBuilder{mappedStorage};
  auto mappedWire = Zpb::save(mappedBuilder, mapped);
  ZuCheck(!mappedBuilder.failed() &&
    Zpb::verify<legacy::ChoiceRow>(mappedWire));
  auto round = Zpb::load<legacy::ChoiceRow>(mappedWire);
  ZuCheck(round.choice.type() == 2 && round.choice.p<2>().id == 9 &&
    round.counts.count_() == 1 && round.counts.findVal("x") == 7);
  Zpb::Reflect::DescSet descriptor;
  ZuCheck(Zpb::Reflect::exportSchema<legacy::ChoiceRow>(descriptor,
    "choice.proto", "legacy", Zpb::Reflect::Syntax::Proto2));
  const auto &schemaRow = descriptor.files[0].messages[0];
  ZuCheck(schemaRow.fields.length() == 3 &&
    schemaRow.oneofs.length() == 1 && schemaRow.oneofs[0].name == "choice" &&
    schemaRow.fields[2].name == "counts" &&
    schemaRow.fields[2].number == 10 && schemaRow.fields[2].label == 3 &&
    schemaRow.fields[2].type == Zpb::Reflect::Type::Message &&
    schemaRow.nested.length() == 1 && schemaRow.nested[0]->options &&
    schemaRow.nested[0]->options->mapEntry);
  Output schemaOutput;
  Zpb::ArrayStorage schemaStorage{schemaOutput};
  Zpb::Builder schemaBuilder{schemaStorage};
  auto schemaWire = Zpb::save(schemaBuilder, descriptor);
  ZuCheck(!schemaBuilder.failed() &&
    Zpb::Reflect::verifySchema(schemaWire));
  Zpb::Reflect::Schema schema{schemaWire};
  auto file = schema.file("choice.proto");
  Zpb::Reflect::ProtoText proto;
  ZuCheck(file && Zpb::Reflect::emitProto(proto, schema, *file,
      Zpb::Reflect::Syntax::Proto2) &&
    strstr(proto.data(), "oneof choice {") &&
    strstr(proto.data(), "map<string, uint32> counts = 10;"));
}

int main(int argc, char **argv)
{
  ZuTestMain();
  ZuTestCall(exchange);
  ZuTestCall(reservedExport);
  ZuTestCall(rowExport);
  ZuTestCall(choice);
}
