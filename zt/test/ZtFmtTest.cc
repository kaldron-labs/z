//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZtFmt.hh>
#include <zlib/ZtString.hh>

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

template <typename Fmt> void checkScalarDefaults()
{
  ZuTestScope(checkScalarDefaults);
  ZtVFmt v{Fmt{}};
  ZuCheck(v.flagsDelim == "|");
  ZuCheck(v.vecPrefix == "[" && v.vecDelim == "," && v.vecSuffix == "]");
  ZtString<> text;
  text << ZuDateTime{2026, 10, 1, 12, 34, 56, 123456789}.fmt(Fmt::DatePrint_());
  ZuCheck(text == "2026/10/01 12:34:56.123456789");
}

void testScalarDefaults()
{
  ZuTestScope(testScalarDefaults);
  ZuTestCall((checkScalarDefaults<ZtFmt::Left<20>>));
  ZuTestCall((checkScalarDefaults<ZtFmt::Right<20>>));
  ZuTestCall((checkScalarDefaults<ZtFmt::Frac<20, 3>>));
  ZuTestCall((checkScalarDefaults<ZtFmt::Hex<>>));
  ZuTestCall((checkScalarDefaults<ZtFmt::HexEnable<false>>));
  ZuTestCall((checkScalarDefaults<ZtFmt::Comma<>>));
  ZuTestCall((checkScalarDefaults<ZtFmt::Alt<>>));
  ZuTestCall((checkScalarDefaults<ZtFmt::AltEnable<false>>));
  ZuTestCall((checkScalarDefaults<ZtFmt::FP<3>>));

  const ZtVFmt custom{ZtFmt::Right<20, ' ', ZtFmt::Comma<';', CustomFmt>>{}};
  ZuCheck(custom.scalar.width() == 20 && custom.scalar.pad() == ' ' &&
    custom.scalar.comma() == ';');
  ZuCheck(custom.flagsDelim == "/" && custom.vecPrefix == "<" &&
    custom.vecDelim == ";" && custom.vecSuffix == ">");
  ZtString<> text;
  text << ZuBox<int64_t>{1234567}.fmt<ZtFmt::Comma<>>();
  ZuCheck(text == "1,234,567");
  text.length(0);
  text << ZuBox<int64_t>{1234567}.fmt<ZtFmt::Comma<'_'>>();
  ZuCheck(text == "1_234_567");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testRuntimeDefaults);
  ZuTestCall(testCustomFmtProjection);
  ZuTestCall(testScalarDefaults);
  return 0;
}
