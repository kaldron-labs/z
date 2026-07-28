//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z HTTP/2 stream, flow-control, and scheduling test

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZhttpH2Session.hh>

using namespace ZuTestUtil;

namespace {

void testStreamStates()
{
  ZuTestScope(testStreamStates);

  Zhttp::H2::StreamStateMachine state;
  ZuCHECK(state.state() == Zhttp::H2::StreamState::Idle &&
      state.open() && state.localOpen() && state.remoteOpen(),
    "idle to open");
  ZuCHECK(state.localEnd() &&
      state.state() == Zhttp::H2::StreamState::HalfClosedLocal &&
      !state.localOpen() && state.remoteOpen(),
    "local half-close");
  ZuCHECK(!state.localEnd() && state.remoteEnd() &&
      state.state() == Zhttp::H2::StreamState::Closed &&
      !state.close(),
    "remote completion and exactly-once close");

  Zhttp::H2::StreamStateMachine peer;
  peer.open();
  ZuCHECK(peer.remoteEnd() &&
      peer.state() == Zhttp::H2::StreamState::HalfClosedRemote &&
      peer.localEnd() && peer.state() == Zhttp::H2::StreamState::Closed,
    "remote half-close");
}

void testFlowControl()
{
  ZuTestScope(testFlowControl);

  Zhttp::H2::FlowWindow window{100};
  ZuCHECK(window.consume(60) && window.value() == 40,
    "DATA consumes flow credit");
  ZuCHECK(!window.consume(41) && window.value() == 40,
    "insufficient credit does not mutate the window");
  ZuCHECK(window.update(10) && window.value() == 50 &&
      !window.update(0),
    "non-zero WINDOW_UPDATE");
  ZuCHECK(window.adjust(-100) && window.value() == -50 &&
      !window.consume(1) && window.adjust(75) && window.value() == 25,
    "initial-window reduction may make stream Tx credit negative");

  Zhttp::H2::FlowWindow overflow{Zhttp::H2::MaxWindow};
  ZuCHECK(!overflow.update(1), "window overflow rejected");

  Zhttp::H2::QueueAdmission admission;
  admission.init(2);
  ZuCHECK(admission.push() && admission.push() && !admission.push() &&
      admission.count() == 2,
    "queued buffer references are explicitly bounded");
  admission.pop(2);
  ZuCHECK(!admission.count(), "queued buffer admission drains exactly");
}

void testSessionAdmission()
{
  ZuTestScope(testSessionAdmission);

  Zhttp::H2::Session client;
  client.init(false, 2, 1, 2, 2);
  auto one = client.openLocal();
  auto three = client.openLocal();
  ZuCHECK(one && one->id == 1 && three && three->id == 3 &&
      !client.canOpenLocal() && !client.openLocal() &&
      client.localCount() == 2,
    "client IDs are odd, monotonic, and concurrency-bounded");
  ZuCHECK(client.peerInitialWindow(1024) &&
      one->tx.value() == 1024 && three->tx.value() == 1024,
    "SETTINGS_INITIAL_WINDOW_SIZE adjusts active Tx streams");
  ZuCHECK(client.queueLocal() && client.queueLocal() &&
      !client.queueLocal() && client.pending() == 2,
    "pending request admission is explicitly bounded");
  ZuCHECK(client.close(1) && client.recentlyClosed(1) &&
      client.admitQueued() && client.find(5) &&
      client.pending() == 1,
    "capacity release admits one queued request");
  ZuCHECK(client.close(3) && client.close(5) &&
      client.recentCount() == 2 && !client.recentlyClosed(1) &&
      client.closed(1) && !client.closed(7),
    "recently-closed knowledge is bounded");

  ZuCHECK(!client.openPeer(2),
    "client rejects peer-created push streams");
  client.final();

  Zhttp::H2::Session server;
  server.init(true, 0, 2, 0);
  ZuCHECK(!server.openLocal() && server.openPeer(1) &&
      !server.openPeer(2) &&
      server.openPeer(3) && !server.openPeer(3),
    "server accepts only increasing odd requests and never creates push");
  server.close(1);
  server.close(3);
  ZuCHECK(server.closed(1) && server.peerIdle(5),
    "closed and idle peer stream IDs remain distinguishable");
  server.refusePeer(5);
  ZuCHECK(server.closed(5) && server.peerIdle(7),
    "refused peer streams advance the monotonic peer boundary");
  server.final();
}

void testRegistryIndependence()
{
  ZuTestScope(testRegistryIndependence);

  Zhttp::H2::StreamRegistry rx;
  Zhttp::H2::StreamRegistry tx;
  auto rxStream = rx.add(1, 10, 20);
  auto txStream = tx.add(1, 30, 40);
  ZuCHECK(rxStream && txStream && rxStream != txStream &&
      rxStream->rx.value() == 10 && txStream->rx.value() == 30,
    "Rx and Tx registries own independent stream state");
  rx.remove(1);
  ZuCHECK(!rx.find(1) && tx.find(1),
    "removing Rx state does not remove Tx state");
  tx.remove(1);
  rx.final();
  tx.final();
}

void testScheduler()
{
  ZuTestScope(testScheduler);

  Zhttp::H2::Scheduler scheduler;
  scheduler.data(1);
  scheduler.data(3);
  scheduler.headers(5);
  scheduler.streamControl(7);
  scheduler.connectionControl();

  auto item = scheduler.next();
  ZuCHECK(item.type == Zhttp::H2::Work::ConnectionControl,
    "connection control precedes all stream work");
  scheduler.remove(0);
  item = scheduler.next();
  ZuCHECK(item.type == Zhttp::H2::Work::StreamControl && item.id == 7,
    "stream control precedes headers and DATA");
  scheduler.remove(7);
  item = scheduler.next();
  ZuCHECK(item.type == Zhttp::H2::Work::Headers && item.id == 5,
    "ready HEADERS precede DATA");
  scheduler.remove(5);

  auto first = scheduler.next();
  auto second = scheduler.next();
  auto third = scheduler.next();
  ZuCHECK(first.type == Zhttp::H2::Work::Data && first.id == 1 &&
      second.id == 3 && third.id == 1,
    "DATA scheduling is deterministic round-robin");

  unsigned calls = 0;
  bool more = scheduler.run(3, [&calls](Zhttp::H2::WorkItem) {
    ++calls;
  });
  ZuCHECK(more && calls == 3,
    "bounded scheduler turn reports continuation work");
  scheduler.remove(1);
  scheduler.remove(3);
  ZuCHECK(scheduler.empty(), "scheduler drains deterministically");
  scheduler.final();
}

} // namespace

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testStreamStates);
  ZuTestCall(testFlowControl);
  ZuTestCall(testSessionAdmission);
  ZuTestCall(testRegistryIndependence);
  ZuTestCall(testScheduler);
}
