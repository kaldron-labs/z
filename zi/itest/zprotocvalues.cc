//  -*- mode:c++; indent-tabs-mode:t; tab-width=8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZiPB.hh>

#include <zprotoc_values_row.hh>

using namespace ZuTestUtil;
ZuDerive(Output,
  (ZtArray<uint8_t, ZtArrayHeapID<"zprotocvalues.Output">>));
ZuDerive(SignedValues,
  (ZtArray<int128_t, ZtArrayHeapID<"zprotocvalues.Signed">>));
ZuDerive(UnsignedValues,
  (ZtArray<uint128_t, ZtArrayHeapID<"zprotocvalues.Unsigned">>));
ZuDerive(FixedValues,
  (ZtArray<ZuFixed, ZtArrayHeapID<"zprotocvalues.Fixed">>));
ZuDerive(DecimalValues,
  (ZtArray<ZuDecimal, ZtArrayHeapID<"zprotocvalues.Decimal">>));
ZuDerive(TimeValues,
  (ZtArray<ZuTime, ZtArrayHeapID<"zprotocvalues.Time">>));
ZuDerive(DateValues,
  (ZtArray<ZuDateTime, ZtArrayHeapID<"zprotocvalues.Date">>));
ZuDerive(IPValues,
  (ZtArray<ZiIP, ZtArrayHeapID<"zprotocvalues.IP">>));
ZuDerive(BitmapValues,
  (ZtArray<ZtBitmap, ZtArrayHeapID<"zprotocvalues.Bitmap">>));
ZuDerive(HwBitmapValues,
  (ZtArray<ZmBitmap, ZtArrayHeapID<"zprotocvalues.HwBitmap">>));
ZiPB::AsArray<"demo.ValueRow.ips"> ZiPB_Fmt(IPValues *);
ZiPB::AsArray<"demo.ValueRow.bitmaps"> ZiPB_Fmt(BitmapValues *);
ZiPB::AsArray<"demo.ValueRow.hwBitmaps"> ZiPB_Fmt(HwBitmapValues *);

struct HandRow {
  int128_t signedWide;
  uint128_t unsignedWide;
  ZuFixed fixed;
  ZuDecimal decimal;
  ZuTime time;
  ZuDateTime dateTime;
  SignedValues signedValues;
  UnsignedValues unsignedValues;
  FixedValues fixedValues;
  DecimalValues decimalValues;
  TimeValues timeValues;
  DateValues dateValues;
};
ZfStruct(, (HandRow, PB),
  (signedWide, (PB::FieldID<1>),	Int128),
  (unsignedWide, (PB::FieldID<2>),	UInt128),
  (fixed, (PB::FieldID<3>),		Fixed),
  (decimal, (PB::FieldID<4>),		Decimal),
  (time, (PB::FieldID<5>),		Time),
  (dateTime, (PB::FieldID<6>),		DateTime),
  (signedValues, (PB::FieldID<7>),	Int128Vec),
  (unsignedValues, (PB::FieldID<8>),	UInt128Vec),
  (fixedValues, (PB::FieldID<9>),	FixedVec),
  (decimalValues, (PB::FieldID<10>),	DecimalVec),
  (timeValues, (PB::FieldID<11>),	TimeVec),
  (dateValues, (PB::FieldID<12>),	DateTimeVec));
ZiPB::AsMessage<"demo.ValueRow"> ZiPB_Fmt(HandRow *);

struct HandExtra {
  ZiIP ip;
  IPValues ips;
  ZtBitmap bitmap;
  BitmapValues bitmaps;
  ZmBitmap hwBitmap;
  HwBitmapValues hwBitmaps;
};
ZfStruct(, (HandExtra, PB),
  (ip, (PB::FieldID<14>),		UDT),
  (ips, (PB::FieldID<15>),		UDT),
  (bitmap, (PB::FieldID<16>),		UDT),
  (bitmaps, (PB::FieldID<17>),		UDT),
  (hwBitmap, (PB::FieldID<18>),		UDT),
  (hwBitmaps, (PB::FieldID<19>),	UDT));
ZiPB::AsMessage<"demo.ValueRow"> ZiPB_Fmt(HandExtra *);

template <typename A, typename B>
static bool same(const A &a, const B &b)
{
  return a.signedWide == b.signedWide &&
    a.unsignedWide == b.unsignedWide && a.fixed == b.fixed &&
    a.decimal == b.decimal && a.time == b.time &&
    a.dateTime == b.dateTime &&
    a.signedValues.length() == 1 && b.signedValues.length() == 1 &&
    a.signedValues[0] == b.signedValues[0] &&
    a.unsignedValues.length() == 1 && b.unsignedValues.length() == 1 &&
    a.unsignedValues[0] == b.unsignedValues[0] &&
    a.fixedValues.length() == 2 && b.fixedValues.length() == 2 &&
    a.fixedValues[0] == b.fixedValues[0] &&
    a.fixedValues[1] == b.fixedValues[1] &&
    a.decimalValues.length() == 1 && b.decimalValues.length() == 1 &&
    a.decimalValues[0] == b.decimalValues[0] &&
    a.timeValues.length() == 1 && b.timeValues.length() == 1 &&
    a.timeValues[0] == b.timeValues[0] &&
    a.dateValues.length() == 1 && b.dateValues.length() == 1 &&
    a.dateValues[0] == b.dateValues[0];
}

template <typename A, typename B>
static bool sameExtra(const A &a, const B &b)
{
  return a.ip == b.ip && a.ips.length() == 1 && b.ips.length() == 1 &&
    a.ips[0] == b.ips[0] && a.bitmap == b.bitmap &&
    a.bitmaps.length() == 1 && b.bitmaps.length() == 1 &&
    a.bitmaps[0] == b.bitmaps[0] && a.hwBitmap == b.hwBitmap &&
    a.hwBitmaps.length() == 1 && b.hwBitmaps.length() == 1 &&
    a.hwBitmaps[0] == b.hwBitmaps[0];
}

static void values()
{
  ZuTestScope(values);
  demo::ValueRow input;
  input.signedWide = -42;
  input.unsignedWide = (uint128_t(uint64_t(-1)) << 64) | 2;
  input.fixed = ZuFixed{-12345, 2};
  input.decimal = ZuDecimal{"123.456"};
  input.time = ZuTime{123, 456};
  input.dateTime = ZuDateTime{2024, 2, 29, 12, 34, 56, 789};
  input.signedValues.push(-123);
  input.unsignedValues.push((uint128_t(1) << 80) | 7);
  input.fixedValues.push(ZuFixed{17, 1});
  input.fixedValues.push(ZuFixed{-52, 2});
  input.decimalValues.push(ZuDecimal{"-12.25"});
  input.timeValues.push(ZuTime{456, 789});
  input.dateValues.push(ZuDateTime{2025, 1, 2, 3, 4, 5, 6});
  input.indexed.add("one", ZuFixed{123, 2});
  input.ip = ZiIP{"127.0.0.1"};
  input.ips.push(ZiIP{"::1"});
  input.bitmap = ZtBitmap{128};
  input.bitmap.set(0).set(65);
  input.bitmaps.push(ZtBitmap{128});
  input.bitmaps[0].set(1);
  input.hwBitmap = ZmBitmap{"3-5,7,9-"};
  input.hwBitmaps.push(ZmBitmap{"2,4"});
  input.ipsByName.add("loop", input.ip);
  input.bitsByName.add("bits", input.bitmap);
  input.hwByName.add("hw", input.hwBitmap);
  Output output;
  ZiPB::ArrayStorage storage{output};
  ZiPB::Builder builder{storage};
  auto wire = ZiPB::save(builder, input);
  ZuCheck(!builder.failed() && ZiPB::verify<demo::ValueRow>(wire));
  auto loaded = ZiPB::load<demo::ValueRow>(wire);
  ZuCheck(same(loaded, input));
  ZuFixed expected{123, 2};
  ZuCheck(loaded.indexed.count_() == 1 &&
    loaded.indexed.findVal("one") == expected);
  ZuCheck(sameExtra(loaded, input));
  ZuCheck(loaded.ipsByName.count_() == 1 &&
    loaded.ipsByName.findVal("loop") == input.ip &&
    loaded.bitsByName.count_() == 1 &&
    loaded.bitsByName.findVal("bits") == input.bitmap &&
    loaded.hwByName.count_() == 1 &&
    loaded.hwByName.findVal("hw") == input.hwBitmap);
  ZuCheck(ZiPB::verify<HandRow>(wire));
  auto hand = ZiPB::load<HandRow>(wire);
  ZuCheck(same(hand, input));
  Output handOutput;
  ZiPB::ArrayStorage handStorage{handOutput};
  ZiPB::Builder handBuilder{handStorage};
  auto handWire = ZiPB::save(handBuilder, hand);
  ZuCheck(!handBuilder.failed() &&
    ZiPB::verify<demo::ValueRow>(handWire));
  auto fromHand = ZiPB::load<demo::ValueRow>(handWire);
  ZuCheck(same(fromHand, hand));
  ZuCheck(ZiPB::verify<HandExtra>(wire));
  auto handExtra = ZiPB::load<HandExtra>(wire);
  ZuCheck(sameExtra(handExtra, input));
  Output extraOutput;
  ZiPB::ArrayStorage extraStorage{extraOutput};
  ZiPB::Builder extraBuilder{extraStorage};
  auto extraWire = ZiPB::save(extraBuilder, handExtra);
  ZuCheck(!extraBuilder.failed() &&
    ZiPB::verify<demo::ValueRow>(extraWire));
  auto fromExtra = ZiPB::load<demo::ValueRow>(extraWire);
  ZuCheck(sameExtra(fromExtra, handExtra));
}

int main(int argc, char **argv)
{
  ZuTestMain();
  ZuTestCall(values);
}
