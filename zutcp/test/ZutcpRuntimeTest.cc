//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/Zutcp.hh>
#include "ZutcpTestAlloc.hh"

using namespace ZuTestUtil;

struct Endpoint : Zutcp_::Endpoint_<Endpoint> {
  enum { EndpointRef = false };
  void endpointReady(Endpoint *) { }
  void endpointDown_(Endpoint *) { }
  void endpointFailed(bool) { }
  void endpointDatagram(Zutcp::Datagram) { }
};

static void testNativeRequired()
{
  ZuTestScope(testNativeRequired);
  ZiMultiplex mx;
  auto rx = ZutcpTest::pktRxAllocator(&mx);
  auto tx = ZutcpTest::pktTxAllocator(&mx);
  Endpoint endpoint;
  ZuCHECK(!endpoint.init(&mx), "no native Stack refuses endpoint initialization");
  ZuCHECK(!endpoint.connected() && !endpoint.listening(), "no kernel endpoint is created");
  ZuCHECK(rx->type == Zi::IOBufType::RxTCP && tx->type == Zi::IOBufType::TxTCP,
    "native protocol requests TCP network provider roles");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testNativeRequired);
}
