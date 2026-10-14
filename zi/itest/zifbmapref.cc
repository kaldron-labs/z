//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZiFB.hh>
#include "zflatcmapref.hh"

using namespace ZuTestUtil;

static void references()
{
  ZuTestScope(references);
  ZmRef<maps::Row> source = new maps::Row();
  using Ints = maps::Zflatc_::IntMapEntry::Map_<"maps.Row.ints">;
  using Texts = maps::Zflatc_::TextMapEntry::Map_<"maps.Row.texts">;
  source->ints = new Ints();
  source->texts = new Texts();
  source->ints->add(4, 40);
  source->texts->add("a", "ref");
  ZiFB::Buffer bytes;
  ZuCheck(ZiFB::save(bytes, *source) == ZiFB::Error::OK);
  ZuCheck(ZiFB::verify<maps::Row>(bytes));
  ZmRef<maps::Row> out = ZiFB::handler<maps::Row>(bytes).alloc();
  ZuCheck(out->ints && out->ints->findVal(4) == 40);
  ZuCheck(out->texts && out->texts->findVal("a") == "ref");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(references);
}
