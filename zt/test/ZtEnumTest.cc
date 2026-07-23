//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZtString.hh>
#include <zlib/ZtEnum.hh>

using namespace ZuTestUtil;

ZtEnumNS(Color, int8_t, Red, Green, Blue);

namespace Perm {
  ZtFlags(Perm, uint8_t, Read, Write, Exec);
}

void testEnumLookupAndName()
{
  ZuTestScope(testEnumLookupAndName);

  ZuCheck(Color::name(Color::Red) == "Red");
  ZuCheck(Color::lookup("Green") == Color::Green);
  ZuCheck(Color::lookup("GreenJunk") < 0);
  ZuCheck(Color::Map::s2v("GreenJunk") < 0);
  ZuCheck(Color::Map::match("GreenJunk") == Color::Green);
  ZuCheck(Color::lookup("missing") < 0);
  ZuCheck(Color::name(99) == "Unknown");

  unsigned count = 0;
  Color::Map::all([&count](ZuCSpan, unsigned) { ++count; });
  ZuCheck(count == Color::N);
}

void testFlagsScanAndPrint()
{
  ZuTestScope(testFlagsScanAndPrint);

  uint8_t rw = Perm::Read() | Perm::Write();

  ZtString<> printed;
  printed << Perm::Map::Print{rw};
  ZuCheck(printed == "Read|Write");

  auto scanPipe = Perm::Map::Scan{"Read | Write"};
  ZuCheck(uint8_t(scanPipe) == rw);

  auto scanComma = Perm::Map::Scan{"Read,Exec", ","};
  ZuCheck(uint8_t(scanComma) == (Perm::Read() | Perm::Exec()));

  auto scanPartial = Perm::Map::Scan{"Read|Unknown"};
  ZuCheck(uint8_t(scanPartial) == Perm::Read());

  auto partial = Perm::Map::Scan::eov("Read|Unknown");
  ZuCheck(partial.p<0>() == 4);
  ZuCheck(uint8_t(partial.p<1>()) == Perm::Read());

  auto invalid = Perm::Map::Scan::eov("Unknown");
  ZuCheck(invalid.p<0>() < 0);
  ZuCheck(!*invalid.p<1>());

  auto empty = Perm::Map::Scan::eov("   ");
  ZuCheck(empty.p<0>() == 3);
  ZuCheck(!uint8_t(empty.p<1>()));
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testEnumLookupAndName);
  ZuTestCall(testFlagsScanAndPrint);
  return 0;
}
