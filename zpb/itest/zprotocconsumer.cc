//  -*- mode:c++; indent-tabs-mode:t; tab-width=8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zpb.hh>
#include <zlib/ZpbReflect.hh>

#include <simple.hh>

ZuAssert((ZuIsSame<ZmHeapID<demo::Row>, ZuStringT<"demo.Row">>{}));
ZuAssert((ZuIsSame<ZmHeapID<demo::Extra>, ZuStringT<"demo.Extra">>{}));

using namespace ZuTestUtil;

ZuDerive(Output,
  (ZtArray<uint8_t, ZtArrayHeapID<"zprotocconsumer.Output">>));
ZuDerive(Samples,
  (ZtArray<int32_t, ZtArrayHeapID<"zprotocconsumer.Samples">>));
ZuDerive(Tags,
  (ZtArray<ZuCSpan, ZtArrayHeapID<"zprotocconsumer.Tags">>));
ZuDerive(Chunks,
  (ZtArray<ZuBSpan, ZtArrayHeapID<"zprotocconsumer.Chunks">>));

struct HandRow {
  uint32_t	id = 0;
  int64_t	change = 0;
  bool		enabled = false;
  double	ratio = 0;
  uint32_t	stamp = 0;
  ZuCSpan	label;
  ZuBSpan	payload;
  float		score = 0;
  Samples	samples;
  Tags		tags;
  Chunks	chunks;
};
ZfStruct(, (HandRow, PB),
  (id, (PB::FieldID<1>),			UInt32),
  (change, (PB::FieldID<3>, PB::ZigZag),	Int64),
  (enabled, (PB::FieldID<4>),			Bool),
  (ratio, (PB::FieldID<5>),			Float),
  (stamp, (PB::FieldID<6>, PB::Fixed),		UInt32),
  (label, (PB::FieldID<7>),			String),
  (payload, (PB::FieldID<8>),			Bytes),
  (score, (PB::FieldID<9>),			Float),
  (samples, (PB::FieldID<10>, PB::ZigZag),	Int32Vec),
  (tags, (PB::FieldID<11>),			StringVec),
  (chunks, (PB::FieldID<12>),			BytesVec));
Zpb::AsMessage<"demo.Row"> Zpb_Fmt(HandRow *);

struct HandExtra { uint64_t value = 0; };
ZfStruct(, (HandExtra, PB),
  (value, (PB::FieldID<1>), UInt64));
Zpb::AsMessage<"demo.Extra"> Zpb_Fmt(HandExtra *);

static void exchange()
{
  ZuTestScope(exchange);
  uint8_t generatedBytes[] = {0, 0x7f, 0xff};
  demo::Row generated;
  generated.id = 17;
  generated.change = -5;
  generated.enabled = true;
  generated.ratio = 1.25;
  generated.stamp = 0x12345678;
  generated.label = "generated";
  generated.payload = ZuBSpan{generatedBytes, sizeof(generatedBytes)};
  generated.score = 3.5f;
  generated.samples.push(-2);
  generated.samples.push(5);
  generated.flags.push(0);
  generated.flags.push(1);
  generated.flags.push(2);
  generated.tags.push("red");
  generated.tags.push("blue");
  generated.chunks.push(ZuBSpan{generatedBytes, sizeof(generatedBytes)});
  generated.peer = new demo::Extra;
  generated.peer->value = 31;
  generated.peer->back = new demo::Row;
  generated.peer->back->id = 88;
  generated.peers.push(new demo::Extra);
  generated.peers[0]->value = 41;
  generated.peers.push(new demo::Extra);
  generated.peers[1]->value = 42;
  generated.shade = demo::Shade::SHADE_BLUE;
  generated.shades.push(demo::Shade::SHADE_RED);
  generated.shades.push(demo::Shade::SHADE_BLUE);
  generated.shades.push(demo::Shade::SHADE_NEG);
  generated.child = new demo::Row_Child;
  generated.child->value = 51;
  generated.children.push(new demo::Row_Child);
  generated.children[0]->value = 52;
  generated.kind = demo::Row_Kind::KIND_TWO;
  demo::OptionA first;
  first.value = 53;
  generated.choice.p<1>(ZuMv(first));
  generated.labels.add(7, "seven");
  demo::OptionA item;
  item.value = 61;
  generated.items.add("one", ZuMv(item));
  generated.totals.add(-2, 0x12345678ULL);
  generated.shadesByName.add("warm", demo::Shade::SHADE_RED);
  const uint8_t blob[] = {'b', 0, 'y'};
  decltype(generated.blobsById)::Val blobValue;
  for (auto byte : blob) blobValue.push(byte);
  generated.blobsById.add(7, ZuMv(blobValue));
  Output output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto wire = Zpb::save(builder, generated);
  ZuCheck(!builder.failed() && Zpb::verify<HandRow>(wire));
  ZuCheck(Zpb::verify<demo::Row>(wire));
  auto generatedCopy = Zpb::load<demo::Row>(wire);
  ZuCheck(generatedCopy.peer && generatedCopy.peer->value == 31 &&
    generatedCopy.peer->back && generatedCopy.peer->back->id == 88);
  ZuCheck(generatedCopy.peers.length() == 2 &&
    generatedCopy.peers[0]->value == 41 &&
    generatedCopy.peers[1]->value == 42);
  ZuCheck(generatedCopy.shade == demo::Shade::SHADE_BLUE &&
    generatedCopy.shades.length() == 3 &&
    generatedCopy.shades[0] == demo::Shade::SHADE_RED &&
    generatedCopy.shades[1] == demo::Shade::SHADE_BLUE &&
    generatedCopy.shades[2] == demo::Shade::SHADE_NEG);
  ZuCheck(generatedCopy.flags.length() == 3 &&
    generatedCopy.flags[0] == 0 && generatedCopy.flags[1] == 1 &&
    generatedCopy.flags[2] == 1);
  ZuCheck(generatedCopy.child && generatedCopy.child->value == 51 &&
    generatedCopy.children.length() == 1 &&
    generatedCopy.children[0]->value == 52 &&
    generatedCopy.kind == demo::Row_Kind::KIND_TWO);
  ZuCheck(generatedCopy.choice.type() == 1 &&
    generatedCopy.choice.p<1>().value == 53);
  ZuCheck(generatedCopy.labels.count_() == 1 &&
    generatedCopy.labels.findVal(7) == "seven" &&
    generatedCopy.items.count_() == 1 &&
    generatedCopy.items.findVal("one").value == 61 &&
    generatedCopy.totals.count_() == 1 &&
    generatedCopy.totals.findVal(-2) == 0x12345678ULL &&
    generatedCopy.shadesByName.findVal("warm") == demo::Shade::SHADE_RED);
  auto savedBlob = generatedCopy.blobsById.find(7);
  ZuCheck(generatedCopy.blobsById.count_() == 1 && savedBlob &&
    savedBlob->val().length() == sizeof(blob) &&
    !memcmp(savedBlob->val().data(), blob, sizeof(blob)));
  const uint8_t duplicateLabels[] = {
    0xb2, 0x01, 5, 0x08, 7, 0x12, 1, 'a',
    0xb2, 0x01, 5, 0x08, 7, 0x12, 1, 'b'};
  ZuCheck(Zpb::verify<demo::Row>(
    {duplicateLabels, sizeof(duplicateLabels)}));
  auto duplicateCopy = Zpb::load<demo::Row>(
    {duplicateLabels, sizeof(duplicateLabels)});
  ZuCheck(duplicateCopy.labels.count_() == 1 &&
    duplicateCopy.labels.findVal(7) == "b");
  const uint8_t duplicateBlobs[] = {
    0xda, 0x01, 5, 0x08, 7, 0x12, 1, 'a',
    0xda, 0x01, 5, 0x08, 7, 0x12, 1, 'b'};
  ZuCheck(Zpb::verify<demo::Row>(
    {duplicateBlobs, sizeof(duplicateBlobs)}));
  auto duplicateBlobCopy = Zpb::load<demo::Row>(
    {duplicateBlobs, sizeof(duplicateBlobs)});
  auto duplicateBlob = duplicateBlobCopy.blobsById.find(7);
  ZuCheck(duplicateBlobCopy.blobsById.count_() == 1 &&
    duplicateBlob && duplicateBlob->val().length() == 1 &&
    duplicateBlob->val()[0] == 'b');
  demo::OptionB choiceSecond;
  choiceSecond.label = "replacement";
  generatedCopy.choice.p<2>(ZuMv(choiceSecond));
  Output choiceOutput;
  Zpb::ArrayStorage choiceStorage{choiceOutput};
  Zpb::Builder choiceBuilder{choiceStorage};
  auto choiceWire = Zpb::save(choiceBuilder, generatedCopy);
  auto choiceCopy = Zpb::load<demo::Row>(choiceWire);
  ZuCheck(!choiceBuilder.failed() && choiceCopy.choice.type() == 2 &&
    choiceCopy.choice.p<2>().label == "replacement");
  ZuCheck(demo::Shade::Map::s2v("SHADE_BLUE") == 5 &&
    demo::Shade::Map::s2v("SHADE_NEG") == -5 &&
    demo::Shade::Map::s2v("SHADE_UNKNOWN") == -1 &&
    demo::Shade::Map::v2s(-5) == "SHADE_NEG");
  auto handwritten = Zpb::load<HandRow>(wire);
  ZuCheck(handwritten.id == 17 && handwritten.change == -5 &&
    handwritten.enabled && handwritten.ratio == 1.25 &&
    handwritten.stamp == 0x12345678);
  ZuCheck(handwritten.label == "generated");
  ZuCheck(handwritten.payload.length() == sizeof(generatedBytes) &&
    !memcmp(handwritten.payload.data(), generatedBytes, sizeof(generatedBytes)));
  ZuCheck(handwritten.score == 3.5f);
  ZuCheck(handwritten.samples.length() == 2 &&
    handwritten.samples[0] == -2 && handwritten.samples[1] == 5);
  ZuCheck(handwritten.tags.length() == 2 &&
    handwritten.tags[0] == "red" && handwritten.tags[1] == "blue");
  ZuCheck(handwritten.chunks.length() == 1 &&
    handwritten.chunks[0].length() == sizeof(generatedBytes) &&
    !memcmp(handwritten.chunks[0].data(), generatedBytes, sizeof(generatedBytes)));

  uint8_t handwrittenBytes[] = {0xfe, 0, 0x41};
  HandRow reverse{29, 7, true, 2.5, 0x87654321, "handwritten",
    {handwrittenBytes, sizeof(handwrittenBytes)}, 4.5f};
  reverse.samples.push(11);
  reverse.samples.push(-3);
  reverse.tags.push("east");
  reverse.tags.push("west");
  reverse.chunks.push(ZuBSpan{handwrittenBytes, sizeof(handwrittenBytes)});
  demo::Row round;
  {
    Output second;
    Zpb::ArrayStorage secondStorage{second};
    Zpb::Builder secondBuilder{secondStorage};
    auto reverseWire = Zpb::save(secondBuilder, reverse);
    ZuCheck(!secondBuilder.failed() && Zpb::verify<demo::Row>(reverseWire));
    round = Zpb::load<demo::Row>(reverseWire);
    memset(const_cast<uint8_t *>(reverseWire.data()), 0xa5,
      reverseWire.length());
  }
  ZuCheck(round.id == 29 && round.change == 7 && round.enabled &&
    round.ratio == 2.5 && round.stamp == 0x87654321);
  ZuCheck(round.label == "handwritten");
  ZuCheck(round.payload.length() == sizeof(handwrittenBytes) &&
    !memcmp(round.payload.data(), handwrittenBytes, sizeof(handwrittenBytes)));
  ZuCheck(round.score == 4.5f);
  ZuCheck(round.samples.length() == 2 && round.samples[0] == 11 &&
    round.samples[1] == -3);
  ZuCheck(round.tags.length() == 2 && round.tags[0] == "east" &&
    round.tags[1] == "west");
  ZuCheck(round.chunks.length() == 1 &&
    round.chunks[0].length() == sizeof(handwrittenBytes) &&
    !memcmp(round.chunks[0].data(), handwrittenBytes, sizeof(handwrittenBytes)));
}

static void exchangeExtra()
{
  ZuTestScope(exchangeExtra);
  demo::Extra generated;
  generated.value = 12345;
  Output output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto wire = Zpb::save(builder, generated);
  ZuCheck(!builder.failed() && Zpb::verify<HandExtra>(wire));
  ZuCheck(Zpb::load<HandExtra>(wire).value == 12345);
  HandExtra handwritten{67890};
  Output second;
  Zpb::ArrayStorage secondStorage{second};
  Zpb::Builder secondBuilder{secondStorage};
  auto reverse = Zpb::save(secondBuilder, handwritten);
  ZuCheck(!secondBuilder.failed() && Zpb::verify<demo::Extra>(reverse));
  ZuCheck(Zpb::load<demo::Extra>(reverse).value == 67890);
}

static void exchangeChoice()
{
  ZuTestScope(exchangeChoice);
  demo::ChoiceOnly input;
  demo::OptionB option;
  option.label = "selected";
  input.selection.p<2>(ZuMv(option));
  Output output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto wire = Zpb::save(builder, input);
  ZuCheck(!builder.failed() && Zpb::verify<demo::ChoiceOnly>(wire));
  auto loaded = Zpb::load<demo::ChoiceOnly>(wire);
  ZuCheck(loaded.selection.type() == 2 &&
    loaded.selection.p<2>().label == "selected");
}

static void exportFloatVec()
{
  ZuTestScope(exportFloatVec);
  Zpb::Reflect::DescSet descriptor;
  ZuCheck(Zpb::Reflect::exportSchema<demo::FloatRow>(descriptor,
    "float.proto", "demo", Zpb::Reflect::Syntax::Proto3));
  ZuCheck(descriptor.files.length() == 1 &&
    descriptor.files[0].messages.length() == 1 &&
    descriptor.files[0].messages[0].fields.length() == 2 &&
    descriptor.files[0].messages[0].fields[0].type ==
      Zpb::Reflect::Type::Float &&
    descriptor.files[0].messages[0].fields[1].type ==
      Zpb::Reflect::Type::Double);
  demo::FloatRow input;
  input.values.push(1.5f);
  input.precise.push(2.25);
  Output output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto wire = Zpb::save(builder, input);
  ZuCheck(!builder.failed() && Zpb::verify<demo::FloatRow>(wire));
  auto loaded = Zpb::load<demo::FloatRow>(wire);
  ZuCheck(loaded.values.length() == 1 && loaded.values[0] == 1.5f &&
    loaded.precise.length() == 1 && loaded.precise[0] == 2.25);
}

static void numeric()
{
  ZuTestScope(numeric);
  demo::NumericRow input;
  input.i32 = -37;
  input.u32 = 4000000000U;
  input.i64 = -1234567890123LL;
  input.u64 = 0xfedcba9876543210ULL;
  input.s32 = -73;
  input.s64 = -9876543210LL;
  input.f32 = 0x89abcdefU;
  input.f64 = 0x0123456789abcdefULL;
  input.sf32 = -1234567;
  input.sf64 = -1234567890123LL;
  input.r32 = 1.25f;
  input.r64 = -2.5;
  input.i32s.push(-19);
  input.u64s.push(0x123456789abcdef0ULL);
  input.s64s.push(-12345);
  input.f32s.push(0x87654321U);
  input.sf64s.push(-9876543210LL);
  input.r64s.push(3.75);
  Output output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto wire = Zpb::save(builder, input);
  ZuCheck(!builder.failed() && Zpb::verify<demo::NumericRow>(wire));
  auto result = Zpb::load<demo::NumericRow>(wire);
  ZuCheck(result.i32 == input.i32 && result.u32 == input.u32 &&
    result.i64 == input.i64 && result.u64 == input.u64 &&
    result.s32 == input.s32 && result.s64 == input.s64 &&
    result.f32 == input.f32 && result.f64 == input.f64 &&
    result.sf32 == input.sf32 && result.sf64 == input.sf64 &&
    result.r32 == input.r32 && result.r64 == input.r64);
  ZuCheck(result.i32s.length() == 1 && result.i32s[0] == input.i32s[0] &&
    result.u64s.length() == 1 && result.u64s[0] == input.u64s[0] &&
    result.s64s.length() == 1 && result.s64s[0] == input.s64s[0] &&
    result.f32s.length() == 1 && result.f32s[0] == input.f32s[0] &&
    result.sf64s.length() == 1 && result.sf64s[0] == input.sf64s[0] &&
    result.r64s.length() == 1 && result.r64s[0] == input.r64s[0]);
}

static void multiChoice()
{
  ZuTestScope(multiChoice);
  demo::MultiChoice input;
  demo::OptionA left;
  left.value = 7;
  input.left.p<1>(ZuMv(left));
  demo::OptionB right;
  right.label = "right";
  input.right.p<1>(ZuMv(right));
  Output output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto wire = Zpb::save(builder, input);
  ZuCheck(!builder.failed() && Zpb::verify<demo::MultiChoice>(wire));
  auto round = Zpb::load<demo::MultiChoice>(wire);
  ZuCheck(round.left.type() == 1 && round.left.p<1>().value == 7 &&
    round.right.type() == 1 && round.right.p<1>().label == "right");

  const uint8_t interleaved[] = {
    0x0a, 0x02, 0x08, 0x01,
    0x1a, 0x03, 0x0a, 0x01, 'R',
    0x12, 0x03, 0x0a, 0x01, 'L',
    0x22, 0x02, 0x08, 0x02};
  ZuCheck(Zpb::verify<demo::MultiChoice>(
    {interleaved, sizeof(interleaved)}));
  auto switched = Zpb::load<demo::MultiChoice>(
    {interleaved, sizeof(interleaved)});
  ZuCheck(switched.left.type() == 2 &&
    switched.left.p<2>().label == "L" &&
    switched.right.type() == 2 &&
    switched.right.p<2>().value == 2);
}

int main(int argc, char **argv)
{
  ZuTestMain();
  ZuTestCall(exchange);
  ZuTestCall(exchangeExtra);
  ZuTestCall(exchangeChoice);
  ZuTestCall(exportFloatVec);
  ZuTestCall(numeric);
  ZuTestCall(multiChoice);
}
