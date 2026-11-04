//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Included after the shared Own fixture in session_io_test.cc.
#ifndef ZfixSessionPreauthTest_HH
#define ZfixSessionPreauthTest_HH

#ifdef __linux__
#include <sys/socket.h>
#include <unistd.h>

template <typename Hub, typename Link>
static bool preauthSnapshot(Hub &hub, ZmRef<Link> link, Progress &snapshot)
{
  return ZmBlock<bool>{}([&hub, link = ZuMv(link), &snapshot](auto wake) mutable {
    hub.rxRun([&hub, link = ZuMv(link), &snapshot, wake = ZuMv(wake)]() mutable {
      snapshot.epoch = link->session_().epoch_(); snapshot.rx = link->rxProgress_();
      hub.txRun([link = ZuMv(link), &snapshot, wake = ZuMv(wake)]() mutable {
	snapshot.tx = link->txProgress_(); wake(true);
      });
    });
  });
}

static void preauthReleaseOld(ZiMultiplex &mx, OwnServer &server, OwnState &state)
{
  ZmBlock<>{}([&mx, &server, &state](auto wake) {
    mx.rxRun([&mx, &server, &state, wake = ZuMv(wake)]() mutable {
      state.holdOld = false;
      auto close = ZuMv(state.oldClose); if (close) close();
      // Follow the native stale-close path through each actual owner. The
      // final observation follows A's resource/accounting retirement.
      server.rxRun([&mx, &server, wake = ZuMv(wake)]() mutable {
	server.txRun([&mx, &server, wake = ZuMv(wake)]() mutable {
	  mx.txRun([&server, wake = ZuMv(wake)]() mutable {
	    server.rxRun([wake = ZuMv(wake)]() mutable { wake(); });
	  });
	});
      });
    });
  });
}

#ifdef ZFIX_TEST_TLS
static void nativePreauth(unsigned mode, bool alert)
{
  ZuTestScopeRT(nativePreauth);
  OwnState state{mode, false, true}; OwnServer server{&state}; OwnClient client{&state};
  ZiMultiplex mx{ZiMxParams{}
    .tcpBufAllocator<Network::RxAllocator<>>()
    .scheduler([](auto &s) {
    s.nThreads(6)
      .thread(1, [](auto &t) { t.isolated(1); }).thread(2, [](auto &t) { t.isolated(1); })
      .thread(3, [](auto &t) { t.isolated(1); }).thread(4, [](auto &t) { t.isolated(1); })
      .thread(5, [](auto &t) { t.isolated(1); }).thread(6, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2)};
  bool started = mx.start(); ZuCheckRT(started); if (!started) return;
  bool si = server.init(srvParams(&mx)), ci = client.init(cliParams(&mx, "5", "6"));
  ZuCheckRT(si && ci);
  bool ss = si && server.start(), cs = ci && client.start(); ZuCheckRT(ss && cs);
  ZmRef<OwnCliLink> a, c;
  ZmRef<OwnSrvLink> owner;
  bool attemptedA = false, attemptedC = false, stoppedB = false, exchanged = false;
  int raw = -1;
  if (ss && cs) do {
    server.listen();
    bool listening = state.listening.timedwait(Zm::now(5)) == 0 && state.port;
    ZuCheckRT(listening); if (!listening) break;
    a = new OwnCliLink{&client, true}; c = new OwnCliLink{&client, false};
    attemptedA = true; a->connect("localhost", state.port, ZiIP{"127.0.0.1"});
    bool pending = state.admission.timedwait(Zm::now(5)) == 0;
    ZuCheckRT(pending); if (!pending) break;
    owner = state.serverLink;
    Progress peer, before;
    bool saved = preauthSnapshot(client, a, peer) && preauthSnapshot(server, owner, before);
    ZuCheckRT(saved && peer.tx.loaded == 2 && peer.rx.processed == 1 &&
      before.epoch == 1 && before.tx.loaded == 1 && before.rx.processed == 1);
    if (!saved) break;
    bool restored = ZmBlock<bool>{}([&client, c, &peer](auto wake) {
      client.rxRun([&client, c, &peer, wake = ZuMv(wake)]() mutable {
	bool ok = c->restoreRx_(peer.epoch, peer.rx);
	client.txRun([c, &peer, ok, wake = ZuMv(wake)]() mutable {
	  wake(c->restoreTx_(peer.epoch, peer.tx) && ok);
	});
      });
    });
    ZuCheckRT(restored); if (!restored) break;

    // B is a real bound TCP socket but sends no ClientHello. Association and
    // the stale A admission reply run while TLS/FIX opening remain incomplete.
    raw = socket(AF_INET, SOCK_STREAM, 0); ZuCheckRT(raw >= 0); if (raw < 0) break;
    sockaddr_in addr{};
    addr.sin_family = AF_INET; addr.sin_port = htons(state.port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bool connected = !::connect(raw, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
    ZuCheckRT(connected); if (!connected) break;
    ZuCheckRT(state.bound.timedwait(Zm::now(5)) == 0);
    ZuCheckRT(state.oldClosed.timedwait(Zm::now(5)) == 0);
    bool socketOnly = ZmBlock<bool>{}([&server, owner](auto wake) {
      server.rxRun([owner, wake = ZuMv(wake)]() mutable {
	const auto &s = owner->session_(); ZuTime at; s.nextTimer_(at);
	wake(s.connection_() == 2 && s.state_() == State::Disconnected &&
	  s.epoch_() == 1 && !s.ready_() && !s.admissionPending_() && !*at);
      });
    });
    Progress bound;
    ZuCheckRT(socketOnly && preauthSnapshot(server, owner, bound));
    ZuCheckRT(bound.rx.received == before.rx.received &&
      bound.rx.processed == before.rx.processed &&
      bound.tx.loaded == before.tx.loaded && bound.tx.sent == before.tx.sent &&
      !state.aReady.load_() && !state.bReady.load_() && !state.serverStops.load_() &&
      state.oldDecisions.load_() == 1 && state.authenticated.trywait() != 0);
    if (alert) {
      // A real fatal handshake_failure alert, before either side authenticates.
      constexpr auto Failure = "\x15\x03\x03\x00\x02\x02\x28"_z;
      ZuCheckRT(::send(raw, Failure.data(), Failure.length(), MSG_NOSIGNAL) ==
	ssize_t(Failure.length()));
    } else { ::close(raw); raw = -1; }
    stoppedB = state.stoppedServer.timedwait(Zm::now(5)) == 0;
    ZuCheckRT(stoppedB && state.serverStops.load_() == 1);
    ZuCheckRT(state.physicalOne.timedwait(Zm::now(5)) == 0 && state.physicalClosed.load_() == 1);
    ZuCheckRT(!state.bReady.load_() && !state.served.load_() && !state.received.load_());
    if (!stoppedB) break;
    if (raw >= 0) { ::close(raw); raw = -1; }

    attemptedC = true; c->connect("localhost", state.port, ZiIP{"127.0.0.1"});
    ZuCheckRT(state.bound.timedwait(Zm::now(5)) == 0);
    bool replied = state.business.timedwait(Zm::now(5)) == 0;
    ZuCheckRT(replied); if (!replied) break;
    bool same = ZmBlock<bool>{}([&server, owner](auto wake) {
      server.rxRun([owner, wake = ZuMv(wake)]() mutable {
	wake(owner->session_().connection_() == 3 && owner->session_().epoch_() == 1 &&
	  owner->session_().ready_());
      });
    });
    ZuCheckRT(same && state.logical.load_() == 1 && state.accepted.load_() == 3 &&
      state.physicalOpen.load_() == 3 && state.physicalClosed.load_() == 1 &&
      state.serverStops.load_() == 1 && state.bStarted.load_() == 1 &&
      state.bLogon == peer.tx.loaded);
    preauthReleaseOld(mx, server, state);
    ZuCheckRT(state.physicalClosed.load_() == 2 && state.serverStops.load_() == 1);
    client.rxRun([c] { c->business("after"); });
    ZuCheckRT(state.business.timedwait(Zm::now(5)) == 0);
    ZuCheckRT(state.received.load_() == 2 && state.served.load_() == 2);
    c->logout(); exchanged = true;
  } while (false);

  if (si) preauthReleaseOld(mx, server, state);
  if (raw >= 0) ::close(raw);
  if (!exchanged) {
    if (attemptedA) a->disconnect(); if (attemptedC) c->disconnect();
    if (owner) owner->disconnect();
  }
  bool drained = true;
  if (attemptedA) {
    bool done = state.stoppedA.timedwait(Zm::now(5)) == 0;
    ZuCheckRT(done); drained &= done;
  }
  if (attemptedC) {
    bool done = state.stoppedB.timedwait(Zm::now(5)) == 0;
    ZuCheckRT(done); drained &= done;
  }
  if ((!stoppedB && state.accepted.load_()) || state.accepted.load_() >= 3) {
    bool done = state.stoppedServer.timedwait(Zm::now(5)) == 0;
    ZuCheckRT(done); drained &= done;
  }
  if (!drained) { log_("Bail out! TLS preauth owner did not drain"); abort(); }
  bool cstop = client.stop(), sstop = server.stop(); ZuCheckRT(cstop && sstop);
  client.final(); server.final(); mx.stop();
  state.serverLink = nullptr; owner = nullptr; a = c = nullptr;
  ZuCheckRT(exchanged && !state.errors.load_() && !state.admit && !state.oldClose &&
    state.serverStops.load_() == 2 && state.physicalClosed.load_() == 3 &&
    state.received.load_() == 2 && state.served.load_() == 2);
}
#endif

static void nativeStopBeforeBind(unsigned mode)
{
  ZuTestScopeRT(nativeStopBeforeBind);
  OwnState state{mode, false, false, true}; OwnServer server{&state}; OwnClient client{&state};
  ZiMultiplex mx{ZiMxParams{}
    .tcpBufAllocator<Network::RxAllocator<>>()
    .scheduler([](auto &s) {
    s.nThreads(6)
      .thread(1, [](auto &t) { t.isolated(1); }).thread(2, [](auto &t) { t.isolated(1); })
      .thread(3, [](auto &t) { t.isolated(1); }).thread(4, [](auto &t) { t.isolated(1); })
      .thread(5, [](auto &t) { t.isolated(1); }).thread(6, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2)};
  bool started = mx.start(); ZuCheckRT(started); if (!started) return;
  bool si = server.init(srvParams(&mx)), ci = client.init(cliParams(&mx, "5", "6"));
  ZuCheckRT(si && ci);
  bool ss = si && server.start(), cs = ci && client.start(); ZuCheckRT(ss && cs);
  ZmRef<OwnCliLink> a, b;
  ZmSemaphore hubStopped;
  bool attemptedA = false, attemptedB = false, stopping = false, stopped = false;
  bool exercised = false;
  if (ss && cs) do {
    server.listen();
    bool listening = state.listening.timedwait(Zm::now(5)) == 0 && state.port;
    ZuCheckRT(listening); if (!listening) break;
    a = new OwnCliLink{&client, true}; b = new OwnCliLink{&client, false};
    attemptedA = true;
#ifdef ZFIX_TEST_TLS
    a->connect("localhost", state.port, ZiIP{"127.0.0.1"});
#else
    a->connect("127.0.0.1", state.port);
#endif
    bool pending = state.admission.timedwait(Zm::now(5)) == 0;
    ZuCheckRT(pending); if (!pending) break;
    Progress snapshot;
    bool saved = preauthSnapshot(client, a, snapshot);
    ZuCheckRT(saved); if (!saved) break;
    bool restored = ZmBlock<bool>{}([&client, b, &snapshot](auto wake) {
      client.rxRun([&client, b, &snapshot, wake = ZuMv(wake)]() mutable {
	bool ok = b->restoreRx_(snapshot.epoch, snapshot.rx);
	client.txRun([b, &snapshot, ok, wake = ZuMv(wake)]() mutable {
	  wake(b->restoreTx_(snapshot.epoch, snapshot.tx) && ok);
	});
      });
    });
    ZuCheckRT(restored); if (!restored) break;
    // OwnServer::stop_ queues this gate before Base queues actual stopRx_.
    // Once it enters, stopRx_ is already ahead of any later socket binding.
    server.stop([&state, &hubStopped](bool ok) {
      if (!ok) state.fail(); hubStopped.post();
    });
    stopping = true;
    bool gated = state.stopGate.entered.timedwait(Zm::now(5)) == 0;
    ZuCheckRT(gated); if (!gated) break;
    attemptedB = true;
#ifdef ZFIX_TEST_TLS
    b->connect("localhost", state.port, ZiIP{"127.0.0.1"});
#else
    b->connect("127.0.0.1", state.port);
#endif
    bool accepted = state.lateAccepted.timedwait(Zm::now(5)) == 0;
    ZuCheckRT(accepted); state.stopGate.release.post(); if (!accepted) break;
    // The I/O Rx accept callback posts B's bind before it can execute the
    // listener-stop barrier. Native stop therefore accounts for and closes B.
    stopped = state.stoppedServer.timedwait(Zm::now(5)) == 0;
    ZuCheckRT(stopped && state.serverStops.load_() == 1 && state.physicalOpen.load_() == 2);
    ZuCheckRT(state.oldClosed.timedwait(Zm::now(5)) == 0);
    ZuCheckRT(state.authenticated.trywait() != 0 && !state.aReady.load_() &&
      !state.bReady.load_() && !state.served.load_() && !state.received.load_());
    ZuCheckRT(hubStopped.trywait() != 0); // A still owns native physical accounting.
    exercised = stopped;
  } while (false);
  state.stopGate.release.post();
  if (si) preauthReleaseOld(mx, server, state);
  if (!exercised) {
    if (attemptedA) a->disconnect(); if (attemptedB) b->disconnect();
  }
  bool drained = true;
  if (attemptedA) {
    bool done = state.stoppedA.timedwait(Zm::now(5)) == 0;
    ZuCheckRT(done); drained &= done;
  }
  if (attemptedB) {
    bool done = state.stoppedB.timedwait(Zm::now(5)) == 0;
    ZuCheckRT(done); drained &= done;
  }
  if (!stopped && state.accepted.load_()) {
    bool done = state.stoppedServer.timedwait(Zm::now(5)) == 0;
    ZuCheckRT(done); drained &= done;
  }
  bool sstop = stopping ? hubStopped.timedwait(Zm::now(5)) == 0 : !ss || server.stop();
  ZuCheckRT(sstop); drained &= sstop;
  if (!drained) { log_("Bail out! late TLS binding did not drain"); abort(); }
  bool cstop = !cs || client.stop(); ZuCheckRT(cstop);
  client.final(); server.final(); mx.stop();
  state.serverLink = nullptr; a = b = nullptr;
  // A published B Logon always settles once. Physical failure may precede
  // logical close (Output) or follow it; neither may lose its false completion.
  ZuCheckRT(state.abortDone.load_() == unsigned(bool(state.bLogon)) &&
    state.abortFailed.load_() <= state.abortDone.load_() &&
    state.abortFaults.load_() <= state.abortFailed.load_() &&
    state.abortEvents.load_() <= state.abortFaults.load_());
  ZuCheckRT(exercised && !state.errors.load_() && !state.admit && !state.oldClose &&
    state.logical.load_() == 1 && state.accepted.load_() == 2 &&
    state.physicalOpen.load_() == 2 && state.physicalClosed.load_() == 2 &&
    state.serverStops.load_() == 1 && !state.aReady.load_() && !state.bReady.load_() &&
    !state.served.load_() && !state.received.load_(),
    log_("mode=", mode, " errors=", state.errors.load_(), " logical=", state.logical.load_(),
      " accepted=", state.accepted.load_(), " open=", state.physicalOpen.load_(),
      " closed=", state.physicalClosed.load_(), " stops=", state.serverStops.load_(),
      " aReady=", state.aReady.load_(), " bReady=", state.bReady.load_(),
      " served=", state.served.load_(), " received=", state.received.load_(),
      " admit=", bool(state.admit), " oldClose=", bool(state.oldClose)));
}
#endif

#endif /* ZfixSessionPreauthTest_HH */
