//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuInvoke.hh>
#include <zlib/ZuSwitch.hh>
#include <zlib/ZuSeq.hh>

using namespace ZuTestUtil;

struct Target {
  int base = 40;
  int calls = 0;

  int member(int v) { return base + v; }
  void memberVoid(int v) { calls += v; }
};

int bound(Target *t, int v)
{
  return t->base + (v * 2);
}

void boundVoid(Target *t, int v)
{
  t->calls += (v * 10);
}

int unbound(int v)
{
  return v * 3;
}

void unboundVoid(int v)
{
  (void)v;
}

void testInvoke()
{
  ZuTestScope(testInvoke);

  Target t;

  ZuCheck(ZuInvoke<&Target::member>(&t, 2) == 42);
  ZuCheck(ZuInvoke<&bound>(&t, 2) == 44);
  ZuCheck(ZuInvoke<&unbound>(&t, 2) == 6);

  ZuInvoke<&Target::memberVoid>(&t, 1);
  ZuInvoke<&boundVoid>(&t, 2);
  ZuInvoke<&unboundVoid>(&t, 3);
  ZuCheck(t.calls == 21);
}

void testSwitchDispatch()
{
  ZuTestScope(testSwitchDispatch);

  int v = ZuSwitch::dispatch<4>(2, [](auto I) {
    return int(I) + 10;
  }, -1);
  ZuCheck(v == 12);

  int deflt = ZuSwitch::dispatch<4>(9, [](auto I) {
    return int(I) + 10;
  }, -1);
  ZuCheck(deflt == -1);

  using Seq = ZuSeq<1, 3, 5>;
  int seqV = ZuSwitch::dispatch<Seq>(3, [](auto I) {
    return int(I) * 2;
  }, -7);
  ZuCheck(seqV == 6);

  int seqDeflt = ZuSwitch::dispatch<Seq>(2, [](auto I) {
    return int(I) * 2;
  }, -7);
  ZuCheck(seqDeflt == -7);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testInvoke);
  ZuTestCall(testSwitchDispatch);
  return 0;
}
