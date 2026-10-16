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
  using Wire = Map::T;
  bool passed = true;
  O original;
  fill(original);
  Zfb::Buffer buf;
  Zfb::ArrayStorage<> storage{buf};
  Zfb::Builder<decltype(storage)> builder{storage};
  auto source = Map::save(original);
  auto bytes = builder.finish(Zfb::save(builder, source));
  bool valid = Zfb::verify<O, ZuFacet::FB>(bytes);
  ZuCheckRT(valid, passed = false);
  if (!valid) return false;
  auto h = Zfb::handler<O, ZuFacet::FB>(bytes);
  O value = h.ctor();
  Ptr<O> copy = h.alloc();
  h.load(*copy);
  h.update(*copy);
  buf.null();
  ZuCheckRT(check(*copy) && check(value), passed = false);
  Zfb::Buffer reverse;
  Zfb::ArrayStorage<> reverseStorage{reverse};
  Zfb::Builder<decltype(reverseStorage)> writer{reverseStorage};
  bytes = writer.finish(Zfb::save<ZuFacet::FB>(writer, *copy));
  valid = Zfb::verify<Wire>(bytes);
  ZuCheckRT(valid, passed = false);
  if (!valid) return false;
  ZuCheckRT(check(Map::load(Zfb::view<Wire>(bytes))), passed = false);
#ifdef ZFLATC_REF
  ZuCheckRT(copy->refCount() == 1, passed = false);
#endif
  Text native, wire;
  Zfb::saveSchema<O, ZuFacet::FB>(native);
  Zfb::saveSchema<Wire>(wire);
  ZuCheckRT(FlatcFixture::contract(ZuBSpan{native}) == FlatcFixture::contract(ZuBSpan{wire}), passed = false);
  ZuCheckRT((FlatcFixture::metadata<O, ZuFacet::FB>() == FlatcFixture::metadata<Wire>()), passed = false);
  return passed;
}

static void roots()
{
  ZuTestScopeRT(roots);
  roundTrip<Zf::Bytes, Zfb::Values::BytesMap>([](auto &v) {
    v.push(9); v.push(0); v.push(255);
  }, [](const auto &v) { return v.length() == 3 && v[1] == 0 && v[2] == 255; });
  roundTrip<Zf::Bitmap, Zfb::Values::BitmapMap<ZtBitmap>>([](auto &v) {
    v.set(137);
  }, [](const auto &v) { return v.get(137) && !v.get(136); });
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(roots);
}
