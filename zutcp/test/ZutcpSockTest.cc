//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zutcp.hh>

using namespace ZuTestUtil;

void testECNTOS()
{
  ZuTestScope(testECNTOS);

  ZuCHECK(Zutcp::ecnBits(Zutcp::EcnMark::NotECT) == 0x00,
    "NotECT bits mismatch");
  ZuCHECK(Zutcp::ecnBits(Zutcp::EcnMark::ECT1) == 0x01,
    "ECT1 bits mismatch");
  ZuCHECK(Zutcp::ecnBits(Zutcp::EcnMark::ECT0) == 0x02,
    "ECT0 bits mismatch");
  ZuCHECK(Zutcp::ecnBits(Zutcp::EcnMark::CE) == 0x03,
    "CE bits mismatch");

  ZuCHECK(!Zutcp::ecnTOS(Zutcp::EcnMark::NotECT).template is<uint8_t>(),
    "NotECT should not force TOS ancillary data");
  ZuCHECK(Zutcp::ecnTOS(Zutcp::EcnMark::ECT0).template is<uint8_t>() &&
      Zutcp::ecnTOS(Zutcp::EcnMark::ECT0).template p<uint8_t>() == 0x02,
    "ECT0 TOS mismatch");

  ZuCHECK(Zutcp::ecnMarkFromTOS({}) == Zutcp::EcnMark::NotECT,
    "empty TOS should decode as NotECT");
  ZuCHECK(Zutcp::ecnMarkFromTOS(uint8_t(0x00)) == Zutcp::EcnMark::NotECT,
    "TOS NotECT decode mismatch");
  ZuCHECK(Zutcp::ecnMarkFromTOS(uint8_t(0x2d)) == Zutcp::EcnMark::ECT1,
    "TOS ECT1 decode mismatch");
  ZuCHECK(Zutcp::ecnMarkFromTOS(uint8_t(0x2e)) == Zutcp::EcnMark::ECT0,
    "TOS ECT0 decode mismatch");
  ZuCHECK(Zutcp::ecnMarkFromTOS(uint8_t(0x2f)) == Zutcp::EcnMark::CE,
    "TOS CE decode mismatch");
}


int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testECNTOS);
}
