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
ZfStruct(, Row,
  (signedValue,,	Int128),
  (unsignedValue,,	UInt128),
  (fixed,,		Fixed),
  (decimal,,		Decimal),
  (time,,		Time),
  (date,,		DateTime),
  (values,,		Int128Vec),
  (bytes,,		BytesVec),
  (packed,,		UDT));
ZiFB::Table<"test.Values"> ZiFBType(Row *);

ZuAssert(ZiFB::Layout<ZiFB::Values::Int128>::Size == 16);
ZuAssert(ZiFB::Layout<ZiFB::Values::UInt128>::Size == 16);
ZuAssert(ZiFB::Layout<ZiFB::Values::Fixed>::Size == 16);
ZuAssert(ZiFB::Layout<ZiFB::Values::Decimal>::Size == 16);
ZuAssert(ZiFB::Layout<ZiFB::Values::Time>::Size == 16);
ZuAssert(ZiFB::Layout<ZiFB::Values::DateTime>::Size == 12);
ZuAssert(ZiFB::FieldWire<ZfField(Row, packed)>::Size == 4);

struct Bitmaps {
  ZuBitmap<65> fixed;
  ZtBitmap dynamic;
  ZmBitmap hw;
  ZuSpan<const ZuBSpan> bytes;
};
ZfStruct(, Bitmaps,
  (fixed,	(Keys<0>),	UDT),
  (dynamic,	(Keys<0>),	UDT),
  (hw,		(Keys<0>),	UDT),
  (bytes,	(Keys<0>),	BytesVec));
ZiFB::Table<"test.Bitmaps"> ZiFBType(Bitmaps *);

} // ValueTest

namespace UnionTest {

struct Record { unsigned value; };
ZfStruct(, Record, (value, (Ctor<0>, Deflt<0>), UInt32));
ZiFB::Table<"test.Record"> ZiFBType(Record *);
struct Empty { };
ZfStruct(, Empty);
ZiFB::Table<"test.Empty"> ZiFBType(Empty *);
struct Point { int value; };
ZfStruct(, Point, (value, (Ctor<0>, Deflt<0>), Int32));
ZiFB::Inline<"test.Point"> ZiFBType(Point *);
ZuDerive(Body, (ZuUnion<void, Record, Record, Empty, Point>));
ZiFB::Union<"test.Body", ZiFB::Alt<"Replication", Record>,
  ZiFB::Alt<"Recovery", Record>, ZiFB::Alt<"Empty", Empty>,
  ZiFB::Alt<"Point", Point>> ZiFBType(Body *);
struct Message { unsigned seq; Body body; };
ZfStruct(, Message,
  (seq,		(Ctor<0>, Deflt<0>),	UInt32),
  (body,	(Ctor<1>),		UDT));
ZiFB::Table<"test.Message"> ZiFBType(Message *);
ZuAssert(ZiFB::Meta<Message>::N == 3);
ZuAssert(ZiFB::Meta<Message>::id<ZfField(Message, body)>() == 2);

} // UnionTest

namespace StreamTest {

struct Stamp { uint64_t value; };
ZfStruct(, Stamp, (value, (Ctor<0>, FB::Align<16>), UInt64));
ZiFB::Inline<"test.Stamp"> ZiFBType(Stamp *);
struct Row { unsigned seq; Stamp stamp; ZuBSpan bytes; };
ZfStruct(, Row,
  (seq,		(Ctor<0>, Deflt<7>),			UInt32),
  (stamp,	(Ctor<1>),				UDT),
  (bytes,	(Ctor<2>, Required, FB::Align<32>),	Bytes));
ZiFB::Table<"test.Row"> ZiFBType(Row *);
ZfStructRender(, Row, Wire,
  (seq, FB::ID<"sequence">), stamp, (bytes, FB::ID<"payload">));

struct Batch {
  ZuSpan<const Row *> rows;
  ZuSpan<const ZuCSpan> names;
  ZuSpan<const Stamp> stamps;
};
ZfStruct(, Batch,
  (rows,,	UDT),
  (names,,	StringVec),
  (stamps,,	UDT));
ZiFB::Table<"test.Batch"> ZiFBType(Batch *);

ZuDerive(Text, (ZtString<ZtStringHeapID<"ZiFBTest.Stream.Text">>));
struct Stream {
  Text &out;
  Stream &operator <<(ZuCSpan bytes) { out << bytes; return *this; }
};

ZuAssert(ZiFB::BufferAlign<Row>::N == 32);
ZuAssert(ZiFB::Layout<Stamp>::Size == 16);

} // StreamTest

#endif /* ZiFBTest_HH */
