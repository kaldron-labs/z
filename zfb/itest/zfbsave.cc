//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdint.h>
#include <zlib/ZuPtr.hh>
#include <zlib/ZuUnion.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZtEnum.hh>
#include <zlib/ZtBitmap.hh>

#include "zflatcfixture.hh"
#include "zflatcvec.hh"

using namespace ZuTestUtil;
using FlatcFixture::Text;

ZuStructFacet(Wire);
namespace Zfb::Values {
  ZfStructRender(, Int128, Wire, h, l);
}

template <typename T>
struct Array : public ZtArray<T, ZtArrayHeapID<"zfbsave.Array">> {
  ZuDerive_(Array, (ZtArray<T, ZtArrayHeapID<"zfbsave.Array">>));
};
struct Blob : public Array<uint8_t> {
  ZuDerive_(Blob, (Array<uint8_t>));
  friend Zfb::Values::BytesMap ZfbTransform(Blob *);
};

namespace flatctest {

namespace Kind {
  ZtEnum(, Kind, int8_t, First, Second);
  Zfb::AsEnum<"flatctest.Kind"> Zfb_Fmt(Map *);
}
namespace Mode {
  ZtFlags(, Mode, uint32_t, Read, Write);
  Zfb::AsEnum<"flatctest.Mode"> Zfb_Fmt(Map *);
}
struct Point { int32_t x = 0; ZuArray<uint16_t, 3> axes; int32_t v = 5; };
ZfStruct(, (Point, FB),
  (x, (FB::Align<16>), Int32),
  (axes,, UInt16Vec), (v,, Int32));
ZfStructRender(, Point, Wire, (x, FB::ID<"axis_x">), axes, v);
Zfb::AsStruct<"flatctest.Point"> Zfb_Fmt(Point *);

struct Scalars {
  int8_t i8 = -128;
  uint8_t u8 = 255;
  int16_t i16 = -32768;
  uint16_t u16 = 65535;
  int32_t i32 = INT32_MIN;
  uint32_t u32 = UINT32_MAX;
  int64_t i64 = INT64_MIN;
  uint64_t u64 = UINT64_MAX;
  float f32 = 0x1p-149f;
  double f64 = 0x1p-1074;
};
ZfStruct(, (Scalars, FB),
  (i8,, Int8), (u8,, UInt8), (i16,, Int16), (u16,, UInt16),
  (i32,, Int32), (u32,, UInt32), (i64,, Int64), (u64,, UInt64),
  (f32,, Float), (f64,, Float));
ZfStructRender(, Scalars, Wire, i8, u8, i16, u16, i32, u32, i64, u64, f32, f64);
Zfb::AsStruct<"flatctest.Scalars"> Zfb_Fmt(Scalars *);

struct Wide {
  int128_t signedValue = -123;
  uint128_t unsignedValue = uint128_t(1) << 100;
  ZuFixed fixed{123.25};
  ZuDecimal decimal{"456.125"};
  ZuTime time{123, 456};
  ZuDateTime date{ZuDateTime::Julian{2461000}, 123, 456};
  Array<int128_t> values;
  Array<Array<uint8_t>> parts;
  Blob blob;
  ZtBitmap bitmap;
  ZiIP ip4{"192.0.2.37"}, ip6{"2001:db8::19"}, noIP;
#define FlatcMember(T, C, M, V) Array<T> M;
  FlatcVecs(FlatcMember)
#undef FlatcMember
};
#define FlatcField(T, C, M, V) , (M,, C##Vec)
ZfStruct(, (Wide, FB),
  (signedValue,, Int128), (unsignedValue,, UInt128),
  (fixed,, Fixed), (decimal,, Decimal), (time,, Time), (date,, DateTime),
  (values,, Int128Vec), (parts,, BytesVec), (blob,, UDT), (bitmap,, UDT),
  (ip4,, UDT), (ip6,, UDT), (noIP,, UDT)
  FlatcVecs(FlatcField));
#undef FlatcField
#define FlatcRender(T, C, M, V) , M
ZfStructRender(, Wide, Wire,
  signedValue, unsignedValue, fixed, decimal, time, date, values, parts, blob, bitmap,
  ip4, ip6, noIP
  FlatcVecs(FlatcRender));
#undef FlatcRender
Zfb::AsTable<"flatctest.Wide"> Zfb_Fmt(Wide *);

struct Child;
struct Peer;
Zfb::AsTable<"flatctest.Peer"> Zfb_Fmt(Peer *);
struct Child : public ZmNHeap<Child> {
friend inline ZuStringT<"zfbsave.Child"> ZmNHeapID(Child *);

  uint32_t value = 0;
  Text label;
  ZuPtr<Child> next;
  ZuPtr<Peer> peer;
};

ZfStruct(, (Child, FB), (value, (Deflt<0>), UInt32), (label,, String), (next,, UDT), (peer,, UDT));
ZfStructRender(, Child, Wire, value, (label, FB::ID<"child_text">), next, peer);
Zfb::AsTable<"flatctest.Child"> Zfb_Fmt(Child *);

struct Peer : public ZmNHeap<Peer> {
friend inline ZuStringT<"zfbsave.Peer"> ZmNHeapID(Peer *);
 ZuPtr<Child> child; };

ZfStruct(, (Peer, FB), (child,, UDT));
ZfStructRender(, Peer, Wire, child);

ZuDerive(Body, (ZuUnion<void, ZuPtr<Child>, Point, Zfb::Values::Int128, Blob>));
Zfb::AsUnion<"flatctest.Body", Zfb::Alt<"Child", ZuPtr<Child>>,
  Zfb::Alt<"Point", Point>, Zfb::Alt<"Wide", Zfb::Values::Int128>,
  Zfb::Alt<"Bytes", Blob>> Zfb_Fmt(Body *);

struct Row : public ZmNHeap<Row> {
friend inline ZuStringT<"zfbsave.Row"> ZmNHeapID(Row *);

  bool enabled = true;
  int64_t minValue = INT64_MIN;
  uint64_t maxValue = UINT64_MAX;
  double negativeZero = -0.0, realValue = 0x1p-1074;
  double infinity = ZuFP<double>::inf(), notNumber = ZuFP<double>::nan();
  Text displayName, emptyName;
  Array<uint8_t> bytes;
  Array<Text> names;
  Scalars scalars;
  Wide wide;
  Point point;
  Array<Point> points;
  ZuPtr<Child> child;
  Array<ZuPtr<Child>> children;
  Body body;
  Body otherBody;
  int8_t kind = Kind::Second;
  uint32_t mode = Mode::Read() | Mode::Write();
  uint32_t spare = 105;
};

ZfStruct(, (Row, FB),
  (enabled, (Deflt<true>), Bool),
  (minValue, (Deflt<INT64_MIN>), Int64),
  (maxValue, (Deflt<UINT64_MAX>), UInt64),
  (negativeZero, (Deflt<-0.0>), Float),
  (realValue, (Deflt<0x1p-1074>), Float),
  (infinity, (Deflt<ZuFP<double>::inf()>), Float),
  (notNumber, (Deflt<ZuFP<double>::nan()>), Float),
  (displayName, (Required), String), (emptyName,, String),
  (bytes, (FB::Align<32>), Bytes), (names,, StringVec),
  (scalars,, UDT), (wide,, UDT), (point,, UDT), (points,, UDT),
  (child,, UDT), (children,, UDT), (body,, UDT), (otherBody,, UDT),
  (kind, (Deflt<Kind::Second>, Enum<Kind::Map>), Int8),
  (mode, (Deflt<0>, Flags<Mode::Map>), UInt32),
  (spare, (Deflt<0>, FB::ID<"wire_name">), UInt32),
  ((retired, Skip, uint32_t), (Deflt<0>), UInt32));
ZfStructRender(, Row, Wire,
  enabled, minValue, maxValue, negativeZero, realValue, infinity, notNumber,
  displayName, emptyName, bytes, names, scalars, wide, point, points,
  child, children, body, otherBody, kind, mode, (spare, FB::ID<"wire_title">), retired);
Zfb::AsTable<"flatctest.Row"> Zfb_Fmt(Row *);

} // flatctest

static bool payload(const flatctest::Row &row)
{
  bool body = false;
  switch (row.body.type()) {
    case 0: body = true; break;
    case 1: body = row.body.p<1>() && row.body.p<1>()->value == 99; break;
    case 2: body = row.body.p<2>().x == -37 && row.body.p<2>().axes[2] == 8; break;
    case 3: body = row.body.p<3>().h == UINT64_MAX && row.body.p<3>().l == uint64_t(-789); break;
    case 4: body = row.body.p<4>().length() == 3 && row.body.p<4>()[1] == 0 && row.body.p<4>()[2] == 255; break;
  }
  return row.enabled && row.minValue == INT64_MIN && row.maxValue == UINT64_MAX &&
    row.displayName == ZuCSpan{"root\0name", 9} && !row.emptyName &&
    row.bytes.length() == 3 && row.bytes[2] == 255 &&
    row.names.length() == 2 && row.names[0] == "one" && !row.names[1] &&
    row.scalars.i8 == -128 && row.scalars.u64 == UINT64_MAX &&
    row.scalars.f32 == 0x1p-149f && row.scalars.f64 == 0x1p-1074 &&
    row.point.x == -17 && row.point.axes[2] == 8 && row.point.v == 5 &&
    row.points.length() == 1 && row.points[0].axes[1] == 4 &&
    row.child && row.child->value == 42 && row.child->label == "child" &&
    row.child->peer && row.child->peer->child && row.child->peer->child->value == 47 &&
    !row.child->peer->child->peer &&
    !row.child->next && row.children.length() == 1 && row.children[0]->value == 43 &&
    body && row.otherBody.type() == 4 && row.otherBody.p<4>().length() == 2 &&
    row.otherBody.p<4>()[1] == 81 &&
    row.kind == 1 && row.mode == 3 && row.spare == 105 &&
    row.wide.signedValue == -123 && row.wide.unsignedValue == uint128_t(1) << 100 &&
    row.wide.values.length() == 2 && row.wide.values[1] == -456 &&
    row.wide.parts.length() == 2 && row.wide.parts[0].length() == 3 &&
    row.wide.parts[0][1] == 0 && !row.wide.parts[1] &&
    row.wide.blob.length() == 3 && row.wide.blob[2] == 7 &&
    row.wide.bitmap.get(137) && !row.wide.bitmap.get(136) && flatcVecs(row.wide) &&
    row.wide.ip4 == ZiIP{"192.0.2.37"} && row.wide.ip6 == ZiIP{"2001:db8::19"} &&
    row.wide.noIP.type() == 0;
}

template <typename Facet = ZuFacet::FB>
static bool export_(ZuCSpan stem)
{
  flatctest::Row row;
  row.displayName = ZuCSpan{"root\0name", 9};
  row.bytes = {1, 0, 255};
  row.names.push(Text{"one"});
  row.names.push(Text{});
  row.point.x = -17;
  row.point.axes = {2, 4, 8};
  row.points.push(row.point);
  row.child = new flatctest::Child;
  row.child->value = 42; row.child->label = "child";
  row.child->peer = new flatctest::Peer;
  row.child->peer->child = new flatctest::Child;
  row.child->peer->child->value = 47;
  ZuPtr<flatctest::Child> child = new flatctest::Child;
  child->value = 43;
  row.children.push(ZuMv(child));
  row.body.p<1>(ZuPtr<flatctest::Child>{new flatctest::Child});
  row.body.p<1>()->value = 99;
  Blob other;
  other.push(80); other.push(81);
  row.otherBody.p<4>(ZuMv(other));
  row.wide.values = {-123, -456};
  row.wide.parts.push(Array<uint8_t>{1, 0, 255});
  row.wide.parts.push(Array<uint8_t>{});
  row.wide.blob = {6, 0, 7};
  row.wide.bitmap.set(137);
  flatcFill(row.wide);
  if (!payload(row)) return false;
  Text rootFBS, rootName{stem};
  Zfb::saveFBS<Zfb::Values::Bytes>(rootFBS);
  rootName << ".rootbytes.fbs";
  if (!FlatcFixture::write(rootName, rootFBS)) return false;
  rootFBS.null(); rootName = stem;
  Zfb::saveFBS<Zfb::Values::Bitmap>(rootFBS);
  rootName << ".rootbitmap.fbs";
  if (!FlatcFixture::write(rootName, rootFBS)) return false;
  Text fbs, bfbs;
  Zfb::saveFBS<flatctest::Row, Facet>(fbs);
  Zfb::saveSchema<flatctest::Row, Facet>(bfbs);
  Text contract = FlatcFixture::contract(ZuBSpan{bfbs});
  Text manifest = FlatcFixture::metadata<flatctest::Row, Facet>();
  Zfb::Buffer buf;
  constexpr unsigned Align = Zfb::BufferAlign<flatctest::Row>::N;
  Zfb::ArrayStorage<Align> storage{buf};
  Zfb::Builder<decltype(storage)> builder{storage, Align};
  using H = Zfb::As<flatctest::Row>::Handler<flatctest::Row, Facet>;
  auto bytes = builder.finish(H::template save<ZfFieldFilter::All>(builder, row));
  if (!Zfb::verify<flatctest::Row, Facet>(bytes) || !contract) return false;
  Text name;
  auto write = [&](ZuCSpan ext, ZuCSpan data) {
    name = stem; name << ext;
    return FlatcFixture::write(name, data);
  };
  if (!(write(".fbs", fbs) && write(".bfbs", bfbs) &&
    write(".contract", contract) && write(".manifest", manifest) &&
    write(".bin", bytes))) return false;
  for (unsigned i = 0; i < 4; ++i) {
    ZuCSpan ext;
    switch (i) {
      case 0: {
	flatctest::Point point = row.point;
	point.x = -37;
	row.body.p<2>(ZuMv(point));
	ext = ".point.bin";
	break;
      }
      case 1:
	row.body.p<3>(Zfb::Values::Int128{UINT64_MAX, uint64_t(-789)});
	ext = ".wide.bin";
	break;
      case 2: {
	Blob blob;
	blob = {9, 0, 255};
	row.body.p<4>(ZuMv(blob));
	ext = ".bytes.bin";
	break;
      }
      default: row.body.null(); ext = ".none.bin"; break;
    }
    if (!payload(row)) return false;
    builder.clear();
    bytes = builder.finish(H::template save<ZfFieldFilter::All>(builder, row));
    if (!Zfb::verify<flatctest::Row, Facet>(bytes) || !write(ext, bytes)) return false;
  }
  return true;
}

template <typename Facet = ZuFacet::FB>
static bool verify_(ZuCSpan path)
{
  Text data = FlatcFixture::read(path);
  if (!Zfb::verify<flatctest::Row, Facet>(ZuBSpan{data})) return false;
  ZuPtr<flatctest::Row> row = Zfb::handler<flatctest::Row, Facet>(ZuBSpan{data}).alloc();
  return row && payload(*row);
}

int main(int argc, char **argv)
{
  bool ok = false;
  if (argc == 2) ok = export_(argv[1]);
  else if (argc == 3 && ZuCSpan{argv[1]} == "--verify") ok = verify_(argv[2]);
  else if (argc == 3 && ZuCSpan{argv[1]} == "--facet") ok = export_<ZuFacet::Wire>(argv[2]);
  else if (argc == 3 && ZuCSpan{argv[1]} == "--verify-facet") ok = verify_<ZuFacet::Wire>(argv[2]);
  parse(1, argv);
  ZuTestMain();
  ZuCheck(ok);
  return ok ? 0 : 1;
}
