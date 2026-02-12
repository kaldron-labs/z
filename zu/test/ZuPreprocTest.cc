//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuPP.hh>
#include <zlib/ZuReserved.hh>

using namespace ZuTestUtil;

#define ZPP_ENUM_ITEM(x) x
#define ZPP_TRUE(word) 1
#define ZPP_FALSE(word) 0
#define ZPP_INC(x) ((x) + 1)

enum ZPPValues {
  ZuPP_Eval(ZuPP_MapComma(ZPP_ENUM_ITEM, ValueA, ValueB, ValueC))
};

void testPPMapAndStrip()
{
  ZuTestScope(testPPMapAndStrip);

  int ZuPP_Strip((strippedName)) = 7;
  ZuCheck(strippedName == 7);

  constexpr int deferred = ZuPP_Eval(ZuPP_Defer(ZPP_INC)(41));
  ZuCheck(deferred == 42);

  ZuCheck(ValueA == 0);
  ZuCheck(ValueB == 1);
  ZuCheck(ValueC == 2);
}

void testReservedWords()
{
  ZuTestScope(testReservedWords);

  constexpr int isClass = ZuIfReserved(class, ZPP_TRUE, ZPP_FALSE);
  constexpr int isStruct = ZuIfReserved(struct, ZPP_TRUE, ZPP_FALSE);
  constexpr int isWidget = ZuIfReserved(widget, ZPP_TRUE, ZPP_FALSE);

  static_assert(isClass == 1);
  static_assert(isStruct == 1);
  static_assert(isWidget == 0);

  ZuCheck(isClass == 1);
  ZuCheck(isStruct == 1);
  ZuCheck(isWidget == 0);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testPPMapAndStrip);
  ZuTestCall(testReservedWords);
  return 0;
}
