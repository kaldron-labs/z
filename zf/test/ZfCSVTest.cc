//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <cstring>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZuBase32.hh>
#include <zlib/ZuBase64.hh>
#include <zlib/ZuHex.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZfCSV.hh>
#include <zlib/ZtQuote.hh>
#include <zlib/ZtScanBool.hh>
#include <zlib/ZtBytesFmt.hh>

using namespace ZuTestUtil;

struct RangeData {
  int value = 42;
};

ZfStruct((RangeData, CSV),
  (((value), (Ctor<0>, (Range<0, 100>))), (Int32, 42)));

struct RealRangeData {
  double float_ = 0.5;
  ZuFixed fixed;
  ZuDecimal decimal;
};

ZfStruct((RealRangeData, CSV),
  (((float_), (Ctor<0>, (Range<0.0, 1.0>))), (Float, 0.5)),
  (((fixed), (Ctor<1>,
      (Range<ZuDecimal{0}, ZuDecimal{1}>))), (Fixed)),
  (((decimal), (Ctor<2>,
      (Range<ZuDecimal{0}, ZuDecimal{1}>))), (Decimal)));

struct CSVText {
  ZuCArray<8> text;
};

ZfStruct((CSVText, CSV),
  (((text), (Ctor<0>)), (String)));

void testHeaderSplitAndUnquote()
{
  ZuTestScope(testHeaderSplitAndUnquote);

  char line[] = "id,\"display,name\",state\r\n";
  ZfCSV::Header header;
  int n = ZfCSV::split(ZuSpan<char>(line, sizeof(line) - 1), header);

  ZuCheck(n > 0);
  ZuCheck(header.length() == 3);
  ZuCheck(header[0] == "id");

  ZuSpan<char> display = header[1];
  ZfCSV::unquote(display);
  ZuCheck(display == "display,name");

  // unquote() should be idempotent when called repeatedly.
  ZfCSV::unquote(display);
  ZuCheck(display == "display,name");
}

void testRowSplitArrayFormsAndMalformed()
{
  ZuTestScope(testRowSplitArrayFormsAndMalformed);

  char rowLine[] = "a,={1;\"two\";3},=@{\"x\";\"y\"},\"z,z\"\n";
  auto row = ZmScratch(ZfCSV::Cell, 4);
  unsigned length;
  int n = ZfCSV::split(
    ZuSpan<char>(rowLine, sizeof(rowLine) - 1), row.span(), length);
  row.template length<false>(length);

  ZuCheck(n > 0);
  ZuCheck(row.length() == 4);

  ZuCheck(row[0].is<ZuSpan<char>>());
  ZuCheck(row[1].is<ZfCSV::ArrayCell>());
  ZuCheck(row[2].is<ZfCSV::ArrayCell>());
  ZuCheck(row[3].is<ZuSpan<char>>());

  ZuSpan<char> c0 = row[0].p<ZuSpan<char>>();
  ZuCheck(c0 == "a");

  auto a1 = row[1].p<ZfCSV::ArrayCell>();
  ZuCheck(a1.length() == 3);
  ZuSpan<char> a11 = a1[1];
  ZfCSV::unquote(a11);
  ZuCheck(a11 == "two");

  auto a2 = row[2].p<ZfCSV::ArrayCell>();
  ZuCheck(a2.length() == 2);
  ZuSpan<char> a20 = a2[0];
  ZfCSV::unquote(a20);
  ZuCheck(a20 == "x");

  ZuSpan<char> c3 = row[3].p<ZuSpan<char>>();
  ZfCSV::unquote(c3);
  ZuCheck(c3 == "z,z");

  char bad[] = "\"unterminated\n";
  auto malformed = ZmScratch(ZfCSV::Cell, 1);
  ZuCheck(ZfCSV::split(
    ZuSpan<char>(bad, sizeof(bad) - 1), malformed.span(), length) == -1);
  malformed.template length<false>(length);
}

void testQuoteAndCodecWrappers()
{
  ZuTestScope(testQuoteAndCodecWrappers);

  ZtString<> quoted;
  quoted << ZtQuote::CString{"a\"b"};
  ZuCheck(quoted == "\"a\\\"b\"");

  quoted.null();
  quoted << ZtQuote::String{ZuCSpan{"x\"y"}};
  ZuCheck(quoted == "\"x\\\"y\"");

  uint8_t raw[] = { 0x01, 0x02, 0x7f, 0xff };
  ZuBSpan input(raw, sizeof(raw));

  ZtString<> b64;
  b64 << ZtQuote::Base64{input};
  ZtArray<uint8_t> d64;
  d64.length(ZuBase64::declen(b64.length()));
  unsigned l64 = ZuBase64::decode(
    ZuSpan<uint8_t>(d64.data(), d64.length()), b64.cspan());
  ZuCheck(l64 == sizeof(raw));
  ZuCheck(!std::memcmp(d64.data(), raw, sizeof(raw)));

  ZtString<> b32;
  b32 << ZtQuote::Base32{input};
  ZtArray<uint8_t> d32;
  d32.length(ZuBase32::declen(b32.length()));
  unsigned l32 = ZuBase32::decode(
    ZuSpan<uint8_t>(d32.data(), d32.length()), b32.cspan());
  ZuCheck(l32 == sizeof(raw));
  ZuCheck(!std::memcmp(d32.data(), raw, sizeof(raw)));

  ZtString<> hex;
  hex << ZtQuote::Hex{input};
  ZtArray<uint8_t> dhex;
  dhex.length(ZuHex::declen(hex.length()));
  unsigned lhex = ZuHex::decode(
    ZuSpan<uint8_t>(dhex.data(), dhex.length()), hex.cspan());
  ZuCheck(lhex == sizeof(raw));
  ZuCheck(!std::memcmp(dhex.data(), raw, sizeof(raw)));
}

void testScanBoolAndBytesFmt()
{
  ZuTestScope(testScanBoolAndBytesFmt);

  ZuCheck(ZtScanBool("YES"));
  ZuCheck(ZtScanBool("TrUe"));
  ZuCheck(!ZtScanBool("off"));

  bool threw = false;
  try {
    (void)ZtScanBool<true>("off");
  } catch (const ZtBadBool &) {
    threw = true;
  }
  ZuCheck(threw);

  ZuCheck(ZtBytesFmt::Base64 == ZfCSV::Base64);
  ZuCheck(ZtBytesFmt::Base64URL == ZfCSV::Base64URL);
  ZuCheck(ZtBytesFmt::Base32 == ZfCSV::Base32);
  ZuCheck(ZtBytesFmt::Hex == ZfCSV::Hex);
  ZuCheck(ZtBytesFmt::Raw == ZfCSV::Raw);
}

void testWriterBoundaries()
{
  ZuTestScope(testWriterBoundaries);

  {
    ZtString<> out;
    auto writer = ZfCSV::write<CSVText, ZuFacet::CSV, 8>(out);
    CSVText row{"1234"};
    ZuCheck(writer(row));
    ZuCheck(!writer.overflow);
    ZuCheck(out == "text\n\"1234\"\n");
  }
  {
    ZtString<> out;
    auto writer = ZfCSV::write<CSVText, ZuFacet::CSV, 8>(out);
    CSVText row{"12345"};
    ZuCheck(writer(row));
    ZuCheck(!writer.overflow);
    ZuCheck(out == "text\n\"12345\"\n");
  }
  {
    ZtString<> out;
    auto writer = ZfCSV::write<CSVText, ZuFacet::CSV, 8>(out);
    CSVText row{"123456"};
    ZuCheck(!writer(row));
    ZuCheck(writer.overflow);
    ZuCheck(out == "text\n");
  }
}

void testIntegerRange()
{
  ZuTestScope(testIntegerRange);

  char csv[] = "value\n101\n";
  auto reader = ZfCSV::reader<RangeData>();
  RangeData value;
  unsigned rows = 0;
  reader.read({csv, sizeof(csv) - 1}, [&](const auto &row) {
    value = row.ctor();
    ++rows;
  });
  ZuCheck(rows == 1);
  ZuCheck(value.value == ZuCmp<int>::null());

  char unknownCSV[] = "value-junk\n99\n";
  auto unknownReader = ZfCSV::reader<RangeData>();
  RangeData unknown;
  rows = 0;
  unknownReader.read(
    {unknownCSV, sizeof(unknownCSV) - 1}, [&](const auto &row) {
      unknown = row.ctor();
      ++rows;
    });
  ZuCheck(rows == 1);
  ZuCheck(unknown.value == 42);
}

void testRealRange()
{
  ZuTestScope(testRealRange);

  char csv[] = "float_,fixed,decimal\n1.1,-0.1,1.1\n";
  auto reader = ZfCSV::reader<RealRangeData>();
  RealRangeData value;
  unsigned rows = 0;
  reader.read({csv, sizeof(csv) - 1}, [&](const auto &row) {
    value = row.ctor();
    ++rows;
  });
  ZuCheck(rows == 1);
  ZuCheck(ZuCmp<double>::null(value.float_));
  ZuCheck(ZuCmp<ZuFixed>::null(value.fixed));
  ZuCheck(ZuCmp<ZuDecimal>::null(value.decimal));
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testHeaderSplitAndUnquote);
  ZuTestCall(testRowSplitArrayFormsAndMalformed);
  ZuTestCall(testQuoteAndCodecWrappers);
  ZuTestCall(testScanBoolAndBytesFmt);
  ZuTestCall(testWriterBoundaries);
  ZuTestCall(testIntegerRange);
  ZuTestCall(testRealRange);
  return 0;
}
