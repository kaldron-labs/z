//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuDerive.hh>
#include <zlib/ZuTestUtil.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZtBuiltin.hh>
#include <zlib/ZtScratch.hh>

using namespace ZuTestUtil;

using IntArray = ZtArray<int, ZtArrayHeapID<"ZtScratch.IntArray">>;
using CharArray = ZtArray<char, ZtArrayHeapID<"ZtScratch.CharArray">>;

struct NoInitArray : public IntArray {
  using IntArray::IntArray;
private:
  using IntArray::initElems;
};

ZuDerive(BuiltinBuf, (ZtBuiltin<CharArray, 8>));

void testScratch()
{
  ZuTestScope(testScratch);

  auto ints = ZtScratch(IntArray, 6);
  ints.push(1);
  ints.push(2);
  ZuCheck(ints.length() == 2);
  ZuCheck(ints[0] == 1);
  ZuCheck(ints[1] == 2);

  auto ints2 = ZtScratch(IntArray, 3, 6);
  ZuCheck(ints2.length() == 3);
  ints2[0] = 7;
  ints2[1] = 8;
  ints2[2] = 9;
  ZuCheck(ints2[2] == 9);

  auto ints3 = ZtScratch(NoInitArray, 3, 6);
  ZuCheck(ints3.length() == 3);
  ints3[0] = 10;
  ints3[1] = 11;
  ints3[2] = 12;
  ZuCheck(ints3[2] == 12);

  auto s = ZtScratch(ZtString<>, 16);
  s << "abc";
  ZuCheck(s == "abc");

  for (unsigned i = 0; i < 40; i++) s << 'x';
  ZuCheck(s.length() > 16);
}

void testBuiltinBufferCopyMoveAndGrowth()
{
  ZuTestScope(testBuiltinBufferCopyMoveAndGrowth);

  BuiltinBuf a;
  a.length(4);
  a[0] = 'a';
  a[1] = 'b';
  a[2] = 'c';
  a[3] = 'd';

  BuiltinBuf b{a};
  ZuCheck(b.length() == 4);
  ZuCheck(b[0] == 'a');
  ZuCheck(b[3] == 'd');

  b.length(16);
  b[8] = 'z';
  ZuCheck(b.length() == 16);
  ZuCheck(b[8] == 'z');
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testScratch);
  ZuTestCall(testBuiltinBufferCopyMoveAndGrowth);
  return 0;
}
