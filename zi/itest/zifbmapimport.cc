//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZiFBReflect.hh>
#include "zifbmap_base.hh"
#include "zifbmap_user.hh"
#include "zifbmap_base.hh"

using namespace ZuTestUtil;

bool mapConsume(const ZuBSpan &);

static void imported()
{
  ZuTestScope(imported);
  map_import::Holder source;
  source.values.add("base", new map_import::Item());
  source.next = new map_import::Holder();
  source.next->values.add("nested", new map_import::Item());
  ZiFB::Buffer bytes;
  ZuCheck(ZiFB::save(bytes, source) == ZiFB::Error::OK);
  ZuCheck(mapConsume(bytes));
  ZiFB::Buffer bfbs;
  ZiFB::saveSchema<map_import::Holder>(bfbs);
  ZuCheck(ZiFB::Reflect::verifySchema(bfbs));
  ZtString<> fbs;
  ZiFB::saveFBS<map_import::Holder>(fbs);
  ZuCheck(strstr(fbs.data(), "table ValuesEntry") != nullptr);
  ZuCheck(strstr(fbs.data(), "table Holder") != nullptr);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(imported);
}
