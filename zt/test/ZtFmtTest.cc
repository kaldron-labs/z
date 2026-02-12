//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZtFmt.hh>

using namespace ZuTestUtil;

static constexpr ZuCSpan customFlagsDelim() { return "/"; }
static constexpr ZuCSpan customVecPrefix() { return "<"; }
static constexpr ZuCSpan customVecDelim() { return ";"; }
static constexpr ZuCSpan customVecSuffix() { return ">"; }

using CustomFmt = ZtFmt::Vec<
  customVecPrefix,
  customVecDelim,
  customVecSuffix,
  ZtFmt::Flags<customFlagsDelim>>;

void testRuntimeDefaults()
{
  ZuTestScope(testRuntimeDefaults);

  ZtVFmt v;
  ZuCheck(v.flagsDelim == "|");
  ZuCheck(v.vecPrefix == "[");
  ZuCheck(v.vecDelim == ", ");
  ZuCheck(v.vecSuffix == "]");

  ZtVFmt vFromFmt{ZtFmt::Default{}};
  ZuCheck(vFromFmt.flagsDelim == "|");
  ZuCheck(vFromFmt.vecPrefix == "[");
  ZuCheck(vFromFmt.vecDelim == ",");
  ZuCheck(vFromFmt.vecSuffix == "]");
}

void testCustomFmtProjection()
{
  ZuTestScope(testCustomFmtProjection);

  ZtVFmt v{CustomFmt{}};
  ZuCheck(v.flagsDelim == "/");
  ZuCheck(v.vecPrefix == "<");
  ZuCheck(v.vecDelim == ";");
  ZuCheck(v.vecSuffix == ">");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testRuntimeDefaults);
  ZuTestCall(testCustomFmtProjection);
  return 0;
}
