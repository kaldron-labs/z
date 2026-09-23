//  -*- mode:c++; indent-tabs-mode:t; tab-width=8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZdbusAdapter.hh>
#include <zlib/ZdbusEnvelope.hh>

using namespace ZuTestUtil;

static void empty()
{
  ZuTestScope(empty);
  uint8_t bytes[] = {
    'l', 5, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0
  };
  auto result = Zdbus_::frame(bytes);
  ZuCheck(bool(result));
  ZuCheck(result.info.total == Zdbus_::Wire::MinHeaderSize);
  ZuCheck(result.info.serial == 1);
  ZuCheck(result.info.bodyOffset == Zdbus_::Wire::MinHeaderSize);
  ZuCheck(Zdbus_::frame(ZuBSpan{bytes, 15}).error ==
    Zdbus_::FrameError::NeedMore);

  bytes[8] = 0;
  ZuCheck(Zdbus_::frame(bytes).error == Zdbus_::FrameError::Serial);
  bytes[8] = 1;
  bytes[3] = 2;
  ZuCheck(Zdbus_::frame(bytes).error == Zdbus_::FrameError::Version);
  bytes[3] = 1;
  bytes[0] = '?';
  ZuCheck(Zdbus_::frame(bytes).error == Zdbus_::FrameError::Order);
  bytes[0] = 'l';
  bytes[1] = 0;
  ZuCheck(Zdbus_::frame(bytes).error == Zdbus_::FrameError::Type);
  bytes[1] = 5;
  ZuCheck(bool(Zdbus_::frame(bytes)));
}

static void fields()
{
  ZuTestScope(fields);
  // One PATH header field, variant OBJECT_PATH "/x"; 5-byte body padding.
  uint8_t bytes[] = {
    'l', 5, 0, 1, 0, 0, 0, 0, 7, 0, 0, 0, 11, 0, 0, 0,
    1, 1, 'o', 0, 2, 0, 0, 0, '/', 'x', 0, 0, 0, 0, 0, 0
  };
  auto result = Zdbus_::frame(bytes);
  ZuCheck(bool(result));
  ZuCheck(result.info.fieldsLength == 11);
  ZuCheck(result.info.bodyOffset == 32 && result.info.total == 32);
  ZuCheck(Zdbus_::frame(bytes, 31).error == Zdbus_::FrameError::Size);

  // Accept interpretable zero padding counted inside the array length.
  bytes[12] = 16;
  result = Zdbus_::frame(bytes);
  ZuCheck(bool(result));
  unsigned fieldsSeen = 0;
  ZuCheck(Zdbus_::eachHeader(bytes, result.info,
    [&fieldsSeen](unsigned, ZfDBUS::Any) { ++fieldsSeen; }));
  ZuCheck(fieldsSeen == 1);
  bytes[28] = 1;
  ZuCheck(Zdbus_::frame(bytes).error == Zdbus_::FrameError::Header);
  bytes[28] = 0;
  bytes[12] = 11;

  bytes[24] = 'x';
  ZuCheck(Zdbus_::frame(bytes).error == Zdbus_::FrameError::Header);
  bytes[24] = '/';

  bytes[28] = 1;
  ZuCheck(Zdbus_::frame(bytes).error == Zdbus_::FrameError::Padding);
  bytes[28] = 0;
  bytes[18] = 'z';
  result = Zdbus_::frame(bytes);
  ZuCheck(!result && result.error == Zdbus_::FrameError::Header);
}

static void bigEndian()
{
  ZuTestScope(bigEndian);
  uint8_t bytes[] = {
    'B', 5, 0, 1, 0, 0, 0, 4, 0, 0, 0, 9, 0, 0, 0, 7,
    8, 1, 'g', 0, 1, 'u', 0, 0, 1, 2, 3, 4
  };
  auto result = Zdbus_::frame(bytes);
  ZuCheck(bool(result));
  ZuCheck(result.info.bodyLength == 4 && result.info.total == 28);
  ZuCheck(result.info.serial == 9);
  ZuCheck(result.info.headers.signature == "u");
  bytes[21] = 's';
  ZuCheck(Zdbus_::frame(bytes).error == Zdbus_::FrameError::Body);
}

static void required()
{
  ZuTestScope(required);
  uint8_t bytes[] = {
    'l', 1, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 28, 0, 0, 0,
    1, 1, 'o', 0, 2, 0, 0, 0, '/', 'x', 0,
    0, 0, 0, 0, 0,
    3, 1, 's', 0, 3, 0, 0, 0, 'F', 'o', 'o', 0,
    0, 0, 0, 0
  };
  auto parsed = Zdbus_::frame(bytes);
  ZuCheck(bool(parsed));
  ZuCheck(parsed.info.headers.path == "/x");
  ZuCheck(parsed.info.headers.member == "Foo");
  ZuCheck(parsed.info.bodyOffset == sizeof(bytes));

  bytes[32] = 1;
  ZuCheck(Zdbus_::frame(bytes).error == Zdbus_::FrameError::Header);
  bytes[32] = 3;
  bytes[34] = 'u';
  ZuCheck(Zdbus_::frame(bytes).error == Zdbus_::FrameError::Header);
  bytes[34] = 's';
  bytes[40] = '1';
  ZuCheck(Zdbus_::frame(bytes).error == Zdbus_::FrameError::Header);
  bytes[40] = 'F';
  bytes[32] = 2;
  ZuCheck(Zdbus_::frame(bytes).error == Zdbus_::FrameError::Header);
  bytes[32] = 3;
  bytes[1] = 4;
  ZuCheck(Zdbus_::frame(bytes).error == Zdbus_::FrameError::Required);
  bytes[1] = 1;
  bytes[12] = 11;
  ZuCheck(Zdbus_::frame(bytes).error == Zdbus_::FrameError::Required);
}

static void unknown()
{
  ZuTestScope(unknown);
  uint8_t bytes[] = {
    'l', 5, 0, 1, 0, 0, 0, 0, 3, 0, 0, 0, 8, 0, 0, 0,
    42, 1, 'u', 0, 7, 0, 0, 0
  };
  ZuCheck(bool(Zdbus_::frame(bytes)));
  bytes[16] = 0;
  ZuCheck(Zdbus_::frame(bytes).error == Zdbus_::FrameError::Header);
  bytes[16] = 42;
  bytes[18] = 'z';
  ZuCheck(Zdbus_::frame(bytes).error == Zdbus_::FrameError::Header);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(empty);
  ZuTestCall(fields);
  ZuTestCall(bigEndian);
  ZuTestCall(required);
  ZuTestCall(unknown);
  return 0;
}
