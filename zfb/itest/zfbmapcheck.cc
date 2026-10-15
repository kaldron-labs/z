//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZfbReflect.hh>
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
  Zfb::Buffer bytes;
  ZuCheck(Zfb::save(bytes, source) == Zfb::Error::OK);
  ZuCheck(Zfb::verify<maps::Row>(bytes));
  auto view = Zfb::view<maps::Row>(bytes);
  auto ints = view.get<ZfField(Row, ints)>();
  ZuCheck(ints.length() == 2 && ints.key(0) == -3 && ints.key(1) == 7);
  ZuCheck(ints.find(7));
  auto output = Zfb::load<maps::Row>(bytes);
  ZuCheck(output.ints.findVal(7) == 70 && output.ints.findVal(-3) == -11);
  ZuCheck(output.texts.findVal("") == "empty" && output.texts.findVal("z") == "last");
  ZuCheck(output.nested.find(12)->val().find(9)->val()->value == 17);

  Zfb::Buffer bfbs;
  Zfb::saveSchema<maps::Row>(bfbs);
  ZuCheck(Zfb::Reflect::verifySchema(bfbs));
  ZtString<> text;
  Zfb::saveFBS<maps::Row>(text);
  ZuCheck(strstr(text.data(), "key:int = 0 (id: 0, key)") != nullptr);
  ZuCheck(strstr(text.data(), "key:string (id: 0, required, key)") != nullptr);
}

static void root()
{
  ZuTestScope(root);
  maps::IntMap source;
  source.entries.add(8, 80);
  Zfb::Buffer bytes;
  ZuCheck(Zfb::save(bytes, source) == Zfb::Error::OK);
  ZuCheck(Zfb::verify<maps::IntMap>(bytes));
  auto output = Zfb::load<maps::IntMap>(bytes);
  ZuCheck(output.entries.findVal(8) == 80);
  using Map = maps::IntMap::Entries;
  Map map;
  map.add(9, 90);
  bytes.null();
  ZuCheck(Zfb::save(bytes, map) == Zfb::Error::OK);
  ZuCheck(Zfb::verify<Map>(bytes));
  auto entries = Zfb::handler<Map>(bytes).entries();
  ZuCheck(entries.length() == 1 && entries.find(9));
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(row);
  ZuTestCall(root);
}
