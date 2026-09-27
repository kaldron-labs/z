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

ZuDerive(IntScratch, (ZtArray<int, ZtArrayHeapID<"ZtScratch.IntArray",
  ZtArraySharded<true>>>));
using CharArray = ZtArray<char, ZtArrayHeapID<"ZtScratch.CharArray">>;
ZuDerive(StringsScratch, (ZtArray<ZtString<>,
  ZtArrayHeapID<"ZtScratch.StringArray", ZtArraySharded<true>>>));
ZuDerive(TextScratch, (ZtString<ZtStringSharded<true>>));

struct NoInitScratch : public IntScratch {
  using IntScratch::IntScratch;
private:
  using IntScratch::initElems;
};

ZuDerive(BuiltinBuf, (ZtBuiltin<CharArray, 8>));

void testScratch()
{
  ZuTestScope(testScratch);

  auto ints = ZtScratch(IntScratch, 6);
  auto intsData = ints.data();
  ints.push(1);
  ints.push(2);
  ZuCheck(ints.length() == 2);
  ZuCheck(ints[0] == 1);
  ZuCheck(ints[1] == 2);

  int values[] = { 3, 4, 5 };
  ints = ZuSpan(values);
  ZuCheck(ints.data() == intsData);
  ZuCheck(ints.size() == 6);
  ZuCheck(!ints.vallocd());
  ZuCheck(ints.length() == 3 && ints[2] == 5);

  int copiedValues[] = { 6, 7 };
  ints.copy(ZuSpan(copiedValues));
  ZuCheck(ints.data() == intsData);
  ZuCheck(ints.length() == 2 && ints[1] == 7);

  auto ints2 = ZtScratch(IntScratch, 3, 6);
  ZuCheck(ints2.length() == 3);
  ints2[0] = 7;
  ints2[1] = 8;
  ints2[2] = 9;
  ZuCheck(ints2[2] == 9);

  auto ints3 = ZtScratch(NoInitScratch, 3, 6);
  ZuCheck(ints3.length() == 3);
  ints3[0] = 10;
  ints3[1] = 11;
  ints3[2] = 12;
  ZuCheck(ints3[2] == 12);

  auto s = ZtScratch(TextScratch, 16);
  auto sData = s.data();
  s << "abc";
  ZuCheck(s == "abc");

  const char text[] = "defg";
  s = ZuSpan<const char>{text, 4};
  ZuCheck(s.data() == sData);
  ZuCheck(s.size() == 16);
  ZuCheck(!s.vallocd());
  ZuCheck(s == "defg");

  const char copiedText[] = "hij";
  s.copy(ZuSpan<const char>{copiedText, 3});
  ZuCheck(s.data() == sData);
  ZuCheck(s == "hij");

  for (unsigned i = 0; i < 40; i++) s << 'x';
  ZuCheck(s.length() > 16);

  auto strings = ZtScratch(StringsScratch, 4);
  auto stringsData = strings.data();
  strings.push(ZtString<>{"old"});
  ZtString<> stringValues[] = { "one", "two" };
  const ZuSpan<const ZtString<>> stringSpan(
    static_cast<const ZtString<> *>(stringValues), 2);
  strings = stringSpan;
  ZuCheck(strings.data() == stringsData);
  ZuCheck(!strings.vallocd());
  ZuCheck(strings.length() == 2);
  ZuCheck(strings[0] == "one" && strings[1] == "two");
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
  ZuCheck(b.size() == BuiltinBuf::BuiltinSize);
  ZuCheck(!b.vallocd());
  ZuCheck(b[0] == 'a');
  ZuCheck(b[3] == 'd');

  b.length(16);
  b[8] = 'z';
  ZuCheck(b.length() == 16);
  ZuCheck(b[8] == 'z');

  BuiltinBuf c;
  auto cData = c.data();
  c = a;
  ZuCheck(c.data() == cData);
  ZuCheck(c.size() == BuiltinBuf::BuiltinSize);
  ZuCheck(!c.vallocd());

  BuiltinBuf large;
  large.length(16);
  large[0] = 'x';
  large[15] = 'y';
  BuiltinBuf largeCopy{large};
  ZuCheck(largeCopy.length() == 16);
  ZuCheck(largeCopy.vallocd());
  ZuCheck(largeCopy[0] == 'x' && largeCopy[15] == 'y');
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testScratch);
  ZuTestCall(testBuiltinBufferCopyMoveAndGrowth);
  return 0;
}
