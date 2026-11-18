//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmBlock.hh>

#include <zlib/Zutcp.hh>

#include "ZutcpTestAlloc.hh"
#include "ZutcpTestInstance.hh"

using namespace ZuTestUtil;

struct Endpoint : Zutcp_::Endpoint_<Endpoint> {
  enum { EndpointRef = false };
  void endpointReady(Endpoint *) { }
  void endpointDown_(Endpoint *) { }
  void endpointFailed(bool) { }
  void endpointDatagram(Zutcp::Datagram) { }
};

static void testStoppedInstance()
{
  ZuTestScope(testStoppedInstance);
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) { s.nThreads(4); })
    .rxThread(1).txThread(2)};
  {
    Zutcp::Instance cold{&mx};
    auto before = ZutcpTest::Instance::parameters(cold);
    auto invalid = before;
    invalid.maxCxns = 0;
    auto result = cold.init(ZuMv(invalid));
    auto admission = cold.txAdmission();
    auto after = ZutcpTest::Instance::parameters(cold);
    ZuCHECK(result.is<ZeException>() && !cold.ready() && !admission.capacity &&
        !admission.controlCount && !after.interface && after.maxCxns == before.maxCxns,
      "failed initial configuration leaves cold resources and policy untouched");
  }
  auto rx = ZutcpTest::pktRxAllocator(&mx);
  auto tx = ZutcpTest::pktTxAllocator(&mx);
  ZutcpTest::Instance stack{&mx};
  auto config = ZutcpTest::Instance::parameters(stack);
  auto first = stack.Zutcp::Instance::init(config);
  auto capacity = stack.txAdmission().capacity;
  auto allocator = stack.dataTxAllocator();
  Zutcp::ClosedTuple record{.key = {ZiSockAddr{ZiIP{"198.18.0.1"}, 18191},
    ZiSockAddr{ZiIP{"198.18.0.2"}, 18192}}, .txNext = 20, .rxNext = 30, .fin = true};
  record = stack.rememberClosed(record);
  auto repeated = stack.Zutcp::Instance::init(config);
  Zutcp::ClosedTuple retained;
  ZuCHECK(first.is<void>() && repeated.is<void>() &&
      stack.txAdmission().capacity == capacity && stack.dataTxAllocator() == allocator &&
      stack.closedTuple(record.key, retained) && retained.id == record.id,
    "repeated stopped init preserves allocator identity, capacity and closed tuple evidence");
  Endpoint endpoint;
  ZuCHECK(endpoint.init(&stack), "endpoint uses its known owning Instance directly");
  ZuCHECK(!stack.ready() && !endpoint.connected() && !endpoint.listening(),
    "binding the known owner creates no transport endpoint or kernel fallback");
  ZuCHECK(rx->type == Zi::IOBufType::RxTCP && tx->type == Zi::IOBufType::TxTCP,
    "native protocol requests TCP network provider roles");
  stack.final();
  stack.final();
  ZuCHECK(stack.state() == ZmEngineState::Stopped && !stack.ready() &&
      !stack.txAdmission().capacity && !stack.closedTupleStats().count,
    "repeated final releases stopped configuration, pools and history");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testStoppedInstance);
}
