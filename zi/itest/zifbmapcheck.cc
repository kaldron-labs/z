//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZiFBReflect.hh>
#include "zflatcmap.hh"

using namespace ZuTestUtil;

ZuAssert((ZuIsBase<maps::Row::Ints,
  ZmRBTreeKV<int32_t, int32_t, ZmRBTreeHeapID<"maps.Row.ints">>>{}));
ZuAssert((ZuIsSame<maps::Row::Texts::Key,
  ZtString<ZtStringHeapID<"maps.Row.texts.key">>>{}));

static void row()
{
  ZuTestScope(row);
  using namespace maps;
  maps::Row source;
  source.ints.add(7, 70);
  source.ints.add(-3, -11);
  source.texts.add("z", "last");
  source.texts.add("", "empty");
  maps::Row::Nested::Val inner;
  inner.add(9, new maps::Item());
  source.nested.add(12, ZuMv(inner));
  ZiFB::Buffer bytes;
  ZuCheck(ZiFB::save(bytes, source) == ZiFB::Error::OK);
  ZuCheck(ZiFB::verify<maps::Row>(bytes));
  auto view = ZiFB::view<maps::Row>(bytes);
  auto ints = view.get<ZfField(Row, ints)>();
  ZuCheck(ints.length() == 2 && ints.key(0) == -3 && ints.key(1) == 7);
  ZuCheck(ints.find(7));
  auto output = ZiFB::load<maps::Row>(bytes);
  ZuCheck(output.ints.findVal(7) == 70 && output.ints.findVal(-3) == -11);
  ZuCheck(output.texts.findVal("") == "empty" && output.texts.findVal("z") == "last");
  ZuCheck(output.nested.find(12)->val().find(9)->val()->value == 17);

  ZiFB::Buffer bfbs;
  ZiFB::saveSchema<maps::Row>(bfbs);
  ZuCheck(ZiFB::Reflect::verifySchema(bfbs));
  ZtString<> text;
  ZiFB::saveFBS<maps::Row>(text);
  ZuCheck(strstr(text.data(), "key:int = 0 (id: 0, key)") != nullptr);
  ZuCheck(strstr(text.data(), "key:string (id: 0, required, key)") != nullptr);
}

static void root()
{
  ZuTestScope(root);
  maps::IntMap source;
  source.entries.add(8, 80);
  ZiFB::Buffer bytes;
  ZuCheck(ZiFB::save(bytes, source) == ZiFB::Error::OK);
  ZuCheck(ZiFB::verify<maps::IntMap>(bytes));
  auto output = ZiFB::load<maps::IntMap>(bytes);
  ZuCheck(output.entries.findVal(8) == 80);
  using Map = maps::IntMap::Entries;
  Map map;
  map.add(9, 90);
  bytes.null();
  ZuCheck(ZiFB::save(bytes, map) == ZiFB::Error::OK);
  ZuCheck(ZiFB::verify<Map>(bytes));
  auto entries = ZiFB::handler<Map>(bytes).entries();
  ZuCheck(entries.length() == 1 && entries.find(9));
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(row);
  ZuTestCall(root);
}
