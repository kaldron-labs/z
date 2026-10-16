//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZFLATC_HEADER
#define ZFLATC_HEADER <zflatcgen.hh>
#endif
#include ZFLATC_HEADER

#include "zflatcfixture.hh"
#include "zflatcvec.hh"

using namespace ZuTestUtil;
using FlatcFixture::Text;
using flatctest::Row;
using flatctest::Child;

#ifdef ZFLATC_WIRE
#define ZFLATC_LABEL childText
#define ZFLATC_X axisX
#define ZFLATC_SPARE wireTitle
#else
#define ZFLATC_LABEL label
#define ZFLATC_X x
#define ZFLATC_SPARE wireName
#endif

#ifdef ZFLATC_REF
template <typename T> using Ptr = ZmRef<T>;
ZuAssert((ZuIsBase<Row, ZFLATC_REF>{}));
ZuAssert((ZuIsBase<Child, ZFLATC_REF>{}));
ZuAssert((ZuIsBase<flatctest::Peer, ZFLATC_REF>{}));
ZuAssert((ZuIsBase<flatctest::Point, ZFLATC_REF>{}));
#else
template <typename T> using Ptr = ZuPtr<T>;
#endif

ZuAssert((ZuIsSame<decltype(Row::child), Ptr<Child>>{}));
ZuAssert((ZuIsSame<decltype(Child::peer), Ptr<flatctest::Peer>>{}));
ZuAssert((ZuIsSame<decltype(flatctest::Peer::child), Ptr<Child>>{}));
ZuAssert((ZuIsSame<ZuTraits<decltype(Row::children)>::Elem, Ptr<Child>>{}));
ZuAssert((ZuIsSame<decltype(Row::minValue), int64_t>{}));
ZuAssert((ZuIsSame<decltype(flatctest::Scalars::i8), int8_t>{}));
ZuAssert((ZuIsSame<decltype(flatctest::Scalars::u64), uint64_t>{}));
ZuAssert((ZuIsSame<decltype(flatctest::Scalars::f32), float>{}));
ZuAssert((ZuIsSame<decltype(flatctest::Scalars::f64), double>{}));
ZuAssert((ZuIsSame<decltype(flatctest::Wide::signedValue), int128_t>{}));
ZuAssert((ZuIsSame<decltype(flatctest::Wide::fixed), ZuFixed>{}));
ZuAssert((ZuIsSame<decltype(flatctest::Wide::decimal), ZuDecimal>{}));
ZuAssert((ZuIsSame<decltype(flatctest::Wide::time), ZuTime>{}));
ZuAssert((ZuIsSame<decltype(flatctest::Wide::date), ZuDateTime>{}));
ZuAssert((Row::DisplayName::HeapID{}() == "flatctest.Row.displayName"_z));
ZuAssert((Row::Names::HeapID{}() == "flatctest.Row.names"_z));
using Name = ZuTraits<Row::Names>::Elem;
ZuAssert((Name::HeapID{}() == "flatctest.Row.names"_z));
ZuAssert((Row::Bytes::HeapID{}() == "flatctest.Row.bytes"_z));
using Part = ZuTraits<flatctest::Wide::Parts>::Elem;
ZuAssert((Part::HeapID{}() == "flatctest.Wide.parts"_z));
using Body = decltype(Row::body);
using OtherBody = decltype(Row::otherBody);
ZuAssert((OtherBody::Type<4>::HeapID{}() == "flatctest.Row.otherBody"_z));
ZuAssert((ZuIsSame<Body::Type<3>, int128_t>{}));
ZuAssert((Body::Type<4>::HeapID{}() == "flatctest.Row.body"_z));
ZuAssert((flatctest::Wide::Blob::HeapID{}() == "flatctest.Wide.blob"_z));
ZuAssert((ZuIsSame<decltype(flatctest::Wide::bitmap), ZtBitmap>{}));
ZuAssert((ZuIsSame<decltype(flatctest::Wide::ip4), ZiIP>{}));
ZuAssert((ZuIsSame<decltype(flatctest::Wide::ip6), ZiIP>{}));
#define FlatcType(T, C, M, V) \
  ZuAssert((ZuIsSame<ZuTraits<decltype(flatctest::Wide::M)>::Elem, T>{})); \
  ZuAssert((ZuIsBase<decltype(flatctest::Wide::M), \
    ZtArray<T, ZtArrayHeapID<"flatctest.Wide." #M>>>{})); \
  ZuAssert((decltype(flatctest::Wide::M)::HeapID{}() == ZuString{"flatctest.Wide." #M}));
FlatcVecs(FlatcType)
#undef FlatcType
ZuAssert((Zfb::ArrayCount<flatctest::Point::Axes>{} == 3));
using PH = Zfb::As<flatctest::Point>::Handler<flatctest::Point>;
ZuAssert(PH::Align == 16 && PH::Size == 16);

unsigned flatcFields();

int main(int argc, char **argv)
{
  if (argc != 3) return 1;
  parse(1, argv);
  ZuTestMain();
  bool passed = true;
  {
    ZuTest(generated);
    Text stem{argv[1]}, name;
    auto read = [&](ZuCSpan ext) {
      name = stem; name << ext;
      return FlatcFixture::read(name);
    };
    Text data = read(".bin");
    using H = Zfb::As<Row>::Handler<Row, ZuFacet::FB>;
    bool valid = Zfb::verify<Row, ZuFacet::FB>(ZuBSpan{data});
    ZuCheck(valid, passed = false);
    if (!valid) return 1;
    auto h = Zfb::handler<Row, ZuFacet::FB>(ZuBSpan{data});
    Ptr<Row> row = h.alloc();
    Ptr<Child> previous = ZuMv(row->child);
    h.load(*row);
    h.load(*row);
    ZuCheck(previous && row->child && previous.ptr() != row->child.ptr(), passed = false);
    auto childView = h.view.template get<flatctest::ZfField(Row, child)>();
    auto ch = Zfb::handler(Zfb::View<Child, ZuFacet::FB>{childView.data()});
    row->child->next = new Child;
    ch.load(*row->child);
    ch.load(*row->child);
    ZuCheck(!row->child->next && previous->value == 42, passed = false);
    data.null(); // All generated data must own its storage after materialization.
    ZuCheck(row->enabled && row->minValue == INT64_MIN && row->maxValue == UINT64_MAX, passed = false);
    ZuCheck((row->displayName == ZuCSpan{"root\0name", 9}), passed = false);
    ZuCheck(!row->emptyName, passed = false);
    ZuCheck(row->bytes.length() == 3 && row->bytes[2] == 255, passed = false);
    ZuCheck(row->names.length() == 2 && row->names[0] == "one" && !row->names[1], passed = false);
    ZuCheck(row->point.ZFLATC_X == -17 && row->point.axes.length() == 3 && row->point.axes[2] == 8, passed = false);
    ZuCheck(row->points.length() == 1 && row->points[0].axes[1] == 4, passed = false);
    ZuCheck(row->scalars.i8 == -128 && row->scalars.u64 == UINT64_MAX, passed = false);
    ZuCheck(row->scalars.f32 == 0x1p-149f && row->scalars.f64 == 0x1p-1074, passed = false);
    ZuCheck(row->child && row->child->value == 42 && row->child->ZFLATC_LABEL == "child", passed = false);
    ZuCheck(!row->child->next, passed = false);
    ZuCheck(row->child->peer && row->child->peer->child &&
      row->child->peer->child->value == 47 && !row->child->peer->child->peer, passed = false);
    ZuCheck(row->children.length() == 1 && row->children[0]->value == 43, passed = false);
    ZuCheck(row->otherBody.type() == 4 && row->otherBody.p<4>().length() == 2 &&
      row->otherBody.p<4>()[1] == 81, passed = false);
    ZuCheck(row->body.type() == 1 && row->body.p<1>()->value == 99, passed = false);
    ZuCheck(row->kind == 1 && row->mode == 3 && row->ZFLATC_SPARE == 105, passed = false);
    ZuCheck(row->wide && row->wide->signedValue == -123, passed = false);
    ZuCheck(row->wide->unsignedValue == uint128_t(1) << 100, passed = false);
    ZuCheck(row->wide->fixed == ZuFixed{123.25} && row->wide->decimal == ZuDecimal{"456.125"}, passed = false);
    ZuCheck((row->wide->time == ZuTime{123, 456}), passed = false);
    ZuCheck(row->wide->values.length() == 2 && row->wide->values[1] == -456, passed = false);
    ZuCheck(row->wide->parts.length() == 2 && row->wide->parts[0].length() == 3 && row->wide->parts[0][1] == 0 && !row->wide->parts[1], passed = false);
    ZuCheck(row->wide->blob.length() == 3 && row->wide->blob[1] == 0 && row->wide->blob[2] == 7, passed = false);
    ZuCheck(row->wide->bitmap.get(137) && !row->wide->bitmap.get(136), passed = false);
    ZuCheck(flatcVecs(*row->wide), passed = false);
    ZuCheck(row->wide->ip4 == ZiIP{"192.0.2.37"} &&
      row->wide->ip6 == ZiIP{"2001:db8::19"} && !row->wide->noIP.type(), passed = false);
#ifdef ZFLATC_REF
    ZuCheck(row->refCount() == 1 && row->child->refCount() == 1 && row->body.p<1>()->refCount() == 1, passed = false);
    ZuCheck(row->child->peer->refCount() == 1 && row->child->peer->child->refCount() == 1, passed = false);
#endif
    ZuCheck(flatcFields() == H::SchemaFields::N, passed = false);
    Text bfbs;
    Zfb::saveSchema<Row, ZuFacet::FB>(bfbs);
    ZuCheck(FlatcFixture::contract(ZuBSpan{bfbs}) == read(".contract"), passed = false);
    ZuCheck((FlatcFixture::metadata<Row, ZuFacet::FB>() == read(".manifest")), passed = false);
    Text original = read(".bfbs");
    ZuCheck(FlatcFixture::contract(ZuBSpan{original}) == FlatcFixture::contract(ZuBSpan{bfbs}), passed = false);
    Zfb::Buffer buf;
    constexpr unsigned Align = Zfb::BufferAlign<Row>::N;
    Zfb::ArrayStorage<Align> storage{buf};
    Zfb::Builder<decltype(storage)> builder{storage, Align};
    auto bytes = builder.finish(H::save<ZfFieldFilter::All>(builder, *row));
    ZuCheck((Zfb::verify<Row, ZuFacet::FB>(bytes)), passed = false);
    ZuCheck(FlatcFixture::write(argv[2], bytes), passed = false);
    for (unsigned i = 0; i < 4; ++i) {
      ZuCSpan ext = i == 0 ? ".point.bin" : i == 1 ? ".wide.bin" :
	i == 2 ? ".bytes.bin" : ".none.bin";
      data = read(ext);
      valid = Zfb::verify<Row, ZuFacet::FB>(ZuBSpan{data});
      ZuCheck(valid, passed = false);
      if (!valid) return 1;
      Ptr<Row> alt = Zfb::handler<Row, ZuFacet::FB>(ZuBSpan{data}).alloc();
      data.null();
      switch (i) {
	case 0:
	  ZuCheck(alt->body.type() == 2 && alt->body.p<2>().ZFLATC_X == -37 &&
	    alt->body.p<2>().axes.length() == 3 && alt->body.p<2>().axes[2] == 8, passed = false);
	  break;
	case 1:
	  ZuCheck(alt->body.type() == 3 && alt->body.p<3>() == -789, passed = false);
	  break;
	case 2:
	  ZuCheck(alt->body.type() == 4 && alt->body.p<4>().length() == 3 &&
	    alt->body.p<4>()[1] == 0 && alt->body.p<4>()[2] == 255, passed = false);
	  break;
	default: ZuCheck(!alt->body.type(), passed = false); break;
      }
      builder.clear();
      bytes = builder.finish(H::save<ZfFieldFilter::All>(builder, *alt));
      Text output{argv[2]}; output << ext;
      ZuCheck(FlatcFixture::write(output, bytes), passed = false);
    }
  }
  return passed ? 0 : 1;
}
