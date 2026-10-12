//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Shared payload/reflection fixtures; each schema is declared once.

#ifndef ZiFBTest_HH
#define ZiFBTest_HH

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

#include <zlib/ZiFB.hh>

ZuStructFacet(Wire);

ZuStructFacet(Prefixed);
ZiFBConfig(Prefixed, ZiFB_SizePrefixed<>);

namespace ValueTest {

struct Packed { unsigned word = 0; };
struct PackedMap {
  using T = unsigned;
  enum { Code = ZfFieldTC::UInt32 };
  static constexpr unsigned save(Packed v) { return v.word; }
  static Packed load(unsigned v) { return {v}; }
};
PackedMap ZiFBTransform(Packed *);

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
ZiFB::AsTable<"test.Values"> ZiFB_Fmt(Row *);

ZuAssert(ZiFB::As<ZiFB::Values::Int128>::template Handler<ZiFB::Values::Int128>::Size == 16);
ZuAssert(ZiFB::As<ZiFB::Values::UInt128>::template Handler<ZiFB::Values::UInt128>::Size == 16);
ZuAssert(ZiFB::As<ZiFB::Values::Fixed>::template Handler<ZiFB::Values::Fixed>::Size == 16);
ZuAssert(ZiFB::As<ZiFB::Values::Decimal>::template Handler<ZiFB::Values::Decimal>::Size == 16);
ZuAssert(ZiFB::As<ZiFB::Values::Time>::template Handler<ZiFB::Values::Time>::Size == 16);
ZuAssert(ZiFB::As<ZiFB::Values::DateTime>::template Handler<ZiFB::Values::DateTime>::Size == 12);
ZuAssert(ZiFB::FieldWire<ZfField(Row, packed)>::Size == 4);

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
ZiFB::AsTable<"test.Bitmaps"> ZiFB_Fmt(Bitmaps *);

} // ValueTest

namespace UnionTest {

struct Record { unsigned value; };
ZfStruct(, (Record, FB), (value, (Ctor<0>, Deflt<0>), UInt32));
ZiFB::AsTable<"test.Record"> ZiFB_Fmt(Record *);
struct Empty { };
ZfStruct(, (Empty, FB));
ZiFB::AsTable<"test.Empty"> ZiFB_Fmt(Empty *);
struct Point { int value; };
ZfStruct(, (Point, FB), (value, (Ctor<0>, Deflt<0>), Int32));
ZiFB::AsStruct<"test.Point"> ZiFB_Fmt(Point *);
ZuDerive(Body, (ZuUnion<void, Record, Record, Empty, Point>));
ZiFB::AsUnion<"test.Body", ZiFB::Alt<"Replication", Record>,
  ZiFB::Alt<"Recovery", Record>, ZiFB::Alt<"Empty", Empty>,
  ZiFB::Alt<"Point", Point>> ZiFB_Fmt(Body *);
struct Message { unsigned seq; Body body; };
ZfStruct(, (Message, FB),
  (seq,		(Ctor<0>, Deflt<0>),	UInt32),
  (body,	(Ctor<1>),		UDT));
ZiFB::AsTable<"test.Message"> ZiFB_Fmt(Message *);
ZuAssert(ZiFB::As<Message>::template Handler<Message>::N == 3);
ZuAssert(ZiFB::As<Message>::template Handler<Message>::id<ZfField(Message, body)>() == 2);

} // UnionTest

namespace StreamTest {

struct Stamp { uint64_t value; };
ZfStruct(, (Stamp, FB), (value, (Ctor<0>, FB::Align<16>), UInt64));
ZiFB::AsStruct<"test.Stamp"> ZiFB_Fmt(Stamp *);
struct Row { unsigned seq; Stamp stamp; ZuBSpan bytes; };
ZfStruct(, (Row, FB),
  (seq,		(Ctor<0>, Deflt<7>),			UInt32),
  (stamp,	(Ctor<1>),				UDT),
  (bytes,	(Ctor<2>, Required, FB::Align<32>),	Bytes));
ZiFB::AsTable<"test.Row"> ZiFB_Fmt(Row *);
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
ZiFB::AsTable<"test.Batch"> ZiFB_Fmt(Batch *);

ZuDerive(Text, (ZtString<ZtStringHeapID<"ZiFBTest.Stream.Text">>));
struct Stream {
  Text &out;
  Stream &operator <<(ZuCSpan bytes) { out << bytes; return *this; }
};

ZuAssert(ZiFB::BufferAlign<Row>::N == 32);
ZuAssert(ZiFB::As<Stamp>::template Handler<Stamp>::Size == 16);
ZuAssert(ZiFB::As<Stamp>::template Handler<Stamp>::Fields::N == 1);

} // StreamTest

#endif /* ZiFBTest_HH */
