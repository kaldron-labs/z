//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zutcpdpdkfixture_HH
#define zutcpdpdkfixture_HH

#include <zlib/Zutcp.hh>

#include "zpktdkfixture.hh"

namespace ZutcpDpdkTest {

using Result = ZpktDpdkTest::Result;

struct Link {
  const char *ring, *name, *selector, *mac;
  const char *ip, *route, *ip6, *route6;
  rte_ether_addr address;
};

inline constexpr Link Links[] = {
  {"loopa", "zdpdka", "_loop", "02:00:00:00:91:01",
    "198.18.91.1", "198.18.91.1/32", "2001:db8:91::1", "2001:db8:91::1/128",
    {{0x02, 0, 0, 0, 0x91, 1}}},
  {"zutcp1", "zdpdkb", "zdpdkb", "02:00:00:00:91:02",
    "198.18.91.2", "198.18.91.2/32", "2001:db8:91::2", "2001:db8:91::2/128",
    {{0x02, 0, 0, 0, 0x91, 2}}}
};

struct Fixture {
  ZpktDpdkTest::Runtime runtime;
  ZpktDpdkTest::RingPort ports[2];
  ZpktDpdkTest::Wire wire;
  unsigned ifindex[2]{};

  Result network();
  Result devices();
  Result startWire(Zi::IOBufPool *, Zi::IOBufPool *);
  void drain();
};


Zutcp::MxPlugin::Params tcpParams(unsigned routingIfindex);
void failAllocation(unsigned call);
bool allocationFailed();
bool allocationRolledBack();
void failActivation();
bool activationFailed();
bool portStopped(const char *);

int fatalHandoff(ZuCSpan scenario);
bool checksFatalHandoff(const char *scenario, const char *assertion);

} // namespace ZutcpDpdkTest
#endif /* zutcpdpdkfixture_HH */
