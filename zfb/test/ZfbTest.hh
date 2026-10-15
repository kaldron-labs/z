//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Shared payload/reflection fixtures; each schema is declared once.

#ifndef ZfbTest_HH
#define ZfbTest_HH

#include <stdint.h>

#include <zlib/ZuAssert.hh>
#include <zlib/ZuBitmap.hh>
#include <zlib/ZuDateTime.hh>
#include <zlib/ZuDecimal.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuFixed.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuStruct.hh>
#include <zlib/ZuTime.hh>
#include <zlib/ZuUnion.hh>

#include <zlib/ZmBitmap.hh>

#include <zlib/ZtBitmap.hh>
#include <zlib/ZtString.hh>

#include <zlib/Zfb.hh>

ZuStructFacet(Wire);

ZuStructFacet(Prefixed);
ZfbConfig(Prefixed, Zfb_SizePrefixed<>);

namespace ValueTest {

struct Packed { unsigned word = 0; };
struct PackedMap {
  using T = unsigned;
  enum { Code = ZfFieldTC::UInt32 };
  static constexpr unsigned save(Packed v) { return v.word; }
  static Packed load(unsigned v) { return {v}; }
};
PackedMap ZfbTransform(Packed *);

struct Row {
  int128_t signedValue;
  uint128_t unsignedValue;
  ZuFixed fixed;
  ZuDecimal decimal;
  ZuTime time;
  ZuDateTime date;
  ZuSpan<const int128_t> values;
  ZuSpan<const ZuBSpan> bytes;
  Packed packed;
};
ZfStruct(, (Row, FB),
  (signedValue,,	Int128),
  (unsignedValue,,	UInt128),
  (fixed,,		Fixed),
  (decimal,,		Decimal),
  (time,,		Time),
  (date,,		DateTime),
  (values,,		Int128Vec),
  (bytes,,		BytesVec),
  (packed,,		UDT));
Zfb::AsTable<"test.Values"> Zfb_Fmt(Row *);

ZuAssert(Zfb::As<Zfb::Values::Int128>::template Handler<Zfb::Values::Int128>::Size == 16);
ZuAssert(Zfb::As<Zfb::Values::UInt128>::template Handler<Zfb::Values::UInt128>::Size == 16);
ZuAssert(Zfb::As<Zfb::Values::Fixed>::template Handler<Zfb::Values::Fixed>::Size == 16);
ZuAssert(Zfb::As<Zfb::Values::Decimal>::template Handler<Zfb::Values::Decimal>::Size == 16);
ZuAssert(Zfb::As<Zfb::Values::Time>::template Handler<Zfb::Values::Time>::Size == 16);
ZuAssert(Zfb::As<Zfb::Values::DateTime>::template Handler<Zfb::Values::DateTime>::Size == 12);
ZuAssert(Zfb::FieldWire<ZfField(Row, packed)>::Size == 4);

struct Bitmaps {
  ZuBitmap<65> fixed;
  ZtBitmap dynamic;
  ZmBitmap hw;
  ZuSpan<const ZuBSpan> bytes;
};
ZfStruct(, (Bitmaps, FB),
  (fixed,	(Keys<0>),	UDT),
  (dynamic,	(Keys<0>),	UDT),
  (hw,		(Keys<0>),	UDT),
  (bytes,	(Keys<0>),	BytesVec));
Zfb::AsTable<"test.Bitmaps"> Zfb_Fmt(Bitmaps *);

} // ValueTest

namespace UnionTest {

struct Record { unsigned value; };
ZfStruct(, (Record, FB), (value, (Ctor<0>, Deflt<0>), UInt32));
Zfb::AsTable<"test.Record"> Zfb_Fmt(Record *);
struct Empty { };
ZfStruct(, (Empty, FB));
Zfb::AsTable<"test.Empty"> Zfb_Fmt(Empty *);
struct Point { int value; };
ZfStruct(, (Point, FB), (value, (Ctor<0>, Deflt<0>), Int32));
Zfb::AsStruct<"test.Point"> Zfb_Fmt(Point *);
ZuDerive(Body, (ZuUnion<void, Record, Record, Empty, Point>));
Zfb::AsUnion<"test.Body", Zfb::Alt<"Replication", Record>,
  Zfb::Alt<"Recovery", Record>, Zfb::Alt<"Empty", Empty>,
  Zfb::Alt<"Point", Point>> Zfb_Fmt(Body *);
struct Message { unsigned seq; Body body; };
ZfStruct(, (Message, FB),
  (seq,		(Ctor<0>, Deflt<0>),	UInt32),
  (body,	(Ctor<1>),		UDT));
Zfb::AsTable<"test.Message"> Zfb_Fmt(Message *);
ZuAssert(Zfb::As<Message>::template Handler<Message>::N == 3);
ZuAssert(Zfb::As<Message>::template Handler<Message>::id<ZfField(Message, body)>() == 2);

} // UnionTest

namespace StreamTest {

struct Stamp { uint64_t value; };
ZfStruct(, (Stamp, FB), (value, (Ctor<0>, FB::Align<16>), UInt64));
Zfb::AsStruct<"test.Stamp"> Zfb_Fmt(Stamp *);
struct Row { unsigned seq; Stamp stamp; ZuBSpan bytes; };
ZfStruct(, (Row, FB),
  (seq,		(Ctor<0>, Deflt<7>),			UInt32),
  (stamp,	(Ctor<1>),				UDT),
  (bytes,	(Ctor<2>, Required, FB::Align<32>),	Bytes));
Zfb::AsTable<"test.Row"> Zfb_Fmt(Row *);
ZuStructReflect(Row, ZuFacet::Prefixed, ZuFacet::FB);
ZuStructReflect(Stamp, ZuFacet::Prefixed, ZuFacet::FB);
ZfStructRender(, Row, Wire,
  (seq, FB::ID<"sequence">), stamp, (bytes, FB::ID<"payload">));

struct Batch {
  ZuSpan<const Row *> rows;
  ZuSpan<const ZuCSpan> names;
  ZuSpan<const Stamp> stamps;
};
ZfStruct(, (Batch, FB),
  (rows,,	UDT),
  (names,,	StringVec),
  (stamps,,	UDT));
Zfb::AsTable<"test.Batch"> Zfb_Fmt(Batch *);

ZuDerive(Text, (ZtString<ZtStringHeapID<"ZfbTest.Stream.Text">>));
struct Stream {
  Text &out;
  Stream &operator <<(ZuCSpan bytes) { out << bytes; return *this; }
};

ZuAssert(Zfb::BufferAlign<Row>::N == 32);
ZuAssert(Zfb::As<Stamp>::template Handler<Stamp>::Size == 16);
ZuAssert(Zfb::As<Stamp>::template Handler<Stamp>::Fields::N == 1);

} // StreamTest

#endif /* ZfbTest_HH */
