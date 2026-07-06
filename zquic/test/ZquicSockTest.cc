//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zquic.hh>

using namespace ZuTestUtil;

void testPlans()
{
  ZuTestScope(testPlans);

  Zquic::SockConfig client;
  client.mode = Zquic::PathMode::ClientConnected;
  client.probe = true;
  auto c = Zquic::Sock::plan(client);
  ZuCHECK(c.noFragment, "client plan should disable fragmentation");
  ZuCHECK(c.probeMode, "client probe mode missing");
  ZuCHECK(c.pmtuQuery, "client PMTU query branch missing");

  Zquic::SockConfig server;
  server.mode = Zquic::PathMode::ServerUnconnected;
  auto s = Zquic::Sock::plan(server);
  ZuCHECK(s.noFragment, "server plan should disable fragmentation");
  ZuCHECK(!s.pmtuQuery, "server must not query connected-socket PMTU");

  Zquic::SockDiag diag;
  auto hint = Zquic::Sock::pathHint(Zi::nullSocket(), client, &diag);
  ZuCHECK(!hint, "invalid socket unexpectedly produced PMTU hint");
  ZuCHECK(diag.mtuQueryErrors == 1, "PMTU query error not counted");
}

void testECNTOS()
{
  ZuTestScope(testECNTOS);

  ZuCHECK(Zquic::ecnBits(Zquic::EcnMark::NotECT) == 0x00,
    "NotECT bits mismatch");
  ZuCHECK(Zquic::ecnBits(Zquic::EcnMark::ECT1) == 0x01,
    "ECT1 bits mismatch");
  ZuCHECK(Zquic::ecnBits(Zquic::EcnMark::ECT0) == 0x02,
    "ECT0 bits mismatch");
  ZuCHECK(Zquic::ecnBits(Zquic::EcnMark::CE) == 0x03,
    "CE bits mismatch");

  ZuCHECK(!Zquic::ecnTOS(Zquic::EcnMark::NotECT).template is<uint8_t>(),
    "NotECT should not force TOS ancillary data");
  ZuCHECK(Zquic::ecnTOS(Zquic::EcnMark::ECT0).template is<uint8_t>() &&
      Zquic::ecnTOS(Zquic::EcnMark::ECT0).template p<uint8_t>() == 0x02,
    "ECT0 TOS mismatch");

  ZuCHECK(Zquic::ecnMarkFromTOS({}) == Zquic::EcnMark::NotECT,
    "empty TOS should decode as NotECT");
  ZuCHECK(Zquic::ecnMarkFromTOS(uint8_t(0x00)) == Zquic::EcnMark::NotECT,
    "TOS NotECT decode mismatch");
  ZuCHECK(Zquic::ecnMarkFromTOS(uint8_t(0x2d)) == Zquic::EcnMark::ECT1,
    "TOS ECT1 decode mismatch");
  ZuCHECK(Zquic::ecnMarkFromTOS(uint8_t(0x2e)) == Zquic::EcnMark::ECT0,
    "TOS ECT0 decode mismatch");
  ZuCHECK(Zquic::ecnMarkFromTOS(uint8_t(0x2f)) == Zquic::EcnMark::CE,
    "TOS CE decode mismatch");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testPlans);
  ZuTestCall(testECNTOS);
}
