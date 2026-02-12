//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZtScanBool.hh>

using namespace ZuTestUtil;

void testTruthyFalsy()
{
  ZuTestScope(testTruthyFalsy);

  ZuCheck(ZtScanBool("1"));
  ZuCheck(ZtScanBool("YES"));
  ZuCheck(ZtScanBool("TrUe"));

  ZuCheck(!ZtScanBool("0"));
  ZuCheck(!ZtScanBool("no"));
  ZuCheck(!ZtScanBool("false"));
}

void testValidateMode()
{
  ZuTestScope(testValidateMode);

  ZuCheck(ZtScanBool<true>("y"));
  ZuCheck(!ZtScanBool<true>("n"));

  bool threw = false;
  try {
    (void)ZtScanBool<true>("maybe");
  } catch (const ZtBadBool &) {
    threw = true;
  }
  ZuCheck(threw);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testTruthyFalsy);
  ZuTestCall(testValidateMode);
  return 0;
}
