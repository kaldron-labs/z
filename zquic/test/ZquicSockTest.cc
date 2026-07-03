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

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testPlans);
}
