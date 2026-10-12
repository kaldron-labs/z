//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include ZFLATC_BYTES
#include ZFLATC_BITMAP
#include "zflatcfixture.hh"

using namespace ZuTestUtil;
using FlatcFixture::Text;
ZuAssert((ZuIsBase<Zf::Bytes, ZtArray<uint8_t, ZtArrayHeapID<"Zf.Bytes.data">>>{}));
ZuAssert((ZuIsBase<Zf::Bitmap, ZtBitmap>{}));
#ifdef ZFLATC_REF
ZuAssert((ZuIsBase<Zf::Bytes, ZFLATC_REF>{}));
ZuAssert((ZuIsBase<Zf::Bitmap, ZFLATC_REF>{}));
template <typename T> using Ptr = ZmRef<T>;
#else
template <typename T> using Ptr = ZuPtr<T>;
#endif

template <typename O, typename Map, typename Fill, typename Check>
static bool roundTrip(Fill fill, Check check)
{
  using Wire = typename Map::T;
  bool passed = true;
  O original;
  fill(original);
  ZiFB::Buffer buf;
  ZiFB::ArrayStorage<> storage{buf};
  ZiFB::Builder<decltype(storage)> builder{storage};
  auto source = Map::save(original);
  auto bytes = builder.finish(ZiFB::save(builder, source));
  bool valid = ZiFB::verify<O, ZuFacet::FB>(bytes);
  ZuCheckRT(valid, passed = false);
  if (!valid) return false;
  auto h = ZiFB::handler<O, ZuFacet::FB>(bytes);
  O value = h.ctor();
  Ptr<O> copy = h.alloc();
  h.load(*copy);
  h.update(*copy);
  buf.null();
  ZuCheckRT(check(*copy) && check(value), passed = false);
  ZiFB::Buffer reverse;
  ZiFB::ArrayStorage<> reverseStorage{reverse};
  ZiFB::Builder<decltype(reverseStorage)> writer{reverseStorage};
  bytes = writer.finish(ZiFB::save<ZuFacet::FB>(writer, *copy));
  valid = ZiFB::verify<Wire>(bytes);
  ZuCheckRT(valid, passed = false);
  if (!valid) return false;
  ZuCheckRT(check(Map::load(ZiFB::view<Wire>(bytes))), passed = false);
#ifdef ZFLATC_REF
  ZuCheckRT(copy->refCount() == 1, passed = false);
#endif
  Text native, wire;
  ZiFB::saveSchema<O, ZuFacet::FB>(native);
  ZiFB::saveSchema<Wire>(wire);
  ZuCheckRT(FlatcFixture::contract(ZuBSpan{native}) == FlatcFixture::contract(ZuBSpan{wire}), passed = false);
  ZuCheckRT((FlatcFixture::metadata<O, ZuFacet::FB>() == FlatcFixture::metadata<Wire>()), passed = false);
  return passed;
}

static void roots()
{
  ZuTestScopeRT(roots);
  roundTrip<Zf::Bytes, ZiFB::Values::BytesMap>([](auto &v) {
    v.push(9); v.push(0); v.push(255);
  }, [](const auto &v) { return v.length() == 3 && v[1] == 0 && v[2] == 255; });
  roundTrip<Zf::Bitmap, ZiFB::Values::BitmapMap<ZtBitmap>>([](auto &v) {
    v.set(137);
  }, [](const auto &v) { return v.get(137) && !v.get(136); });
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(roots);
}
