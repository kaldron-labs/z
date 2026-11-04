//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdio.h>

#include <zlib/ZmRef.hh>
#include <zlib/ZiFile.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiResolver.hh>
#include "load_main.hh"
#include "load_native.hh"

namespace ZfixLoad {

static ZiMxBackend::T backend(const Options &options) {
  if (options.backend == "epoll") return ZiMxBackend::EPoll;
  if (options.backend == "uring") return ZiMxBackend::URing;
  return ZiMxBackend::Auto;
}
static ZuCSpan backendName(ZiMxBackend::T backend) {
  switch (backend) {
    case ZiMxBackend::URing: return "uring";
    case ZiMxBackend::EPoll: return "epoll";
    default: return "auto";
  }
}

static ZiMxParams nativeMxParams(const Options &options, bool server) {
  // One request, two reports and a retiring send can overlap per in-flight
  // order. Rx is shared by all sessions; each server hub has its own Tx pools.
  uint64_t burst = 4 * uint64_t(options.inFlight);
  auto slots = [](unsigned requested, unsigned deflt, uint64_t needed) {
    if (requested) return requested;
    if (needed > ZiMxParams::MaxRingSize) needed = ZiMxParams::MaxRingSize;
    if (needed < deflt) needed = deflt;
    return unsigned(needed);
  };
  return ZiMxParams{}.backend(backend(options))
    .rxRingSize(slots(options.rxRingSize, ZiIOBuf_DefltRxRingSize,
      burst * options.sessions))
    .txRingSize(slots(options.txRingSize, ZiIOBuf_DefltTxRingSize,
      burst * (server ? 1 : options.sessions)));
}

// Fixture-local checkpoint. Main alone reads/writes the file, with owner
// workers stopped; fields are serialized individually without process pointers.
template <typename ...T> static bool put(ZiFile &f, const T &...v) {
  return ((f.write(&v, sizeof(v)) == Zi::OK) && ...);
}
template <typename ...T> static bool get(ZiFile &f, T &...v) {
  return ((f.read(&v, sizeof(v)) == sizeof(v)) && ...);
}
static bool putText(ZiFile &f, ZuCSpan text) {
  uint32_t length = text.length();
  return put(f, length) && (!length || f.write(text.data(), length) == Zi::OK);
}
static bool getText(ZiFile &f, Zfix::SessionString &text) {
  uint32_t length;
  if (!get(f, length) || length > Zfix::Fmt::Limits::FrameBytes) return false;
  text.length(length); return !length || f.read(text.data(), length) == length;
}
static bool putState(ZiFile &f, const NativeCheckpoint &s) {
  const auto &r = s.rx; const auto &t = s.tx;
  return put(f, s.epoch, r.received, r.processed, r.gapBegin, r.gapEnd,
    t.loaded, t.sent, t.acknowledged, t.replayBegin, t.replayEnd);
}
static bool getState(ZiFile &f, NativeCheckpoint &s) {
  auto &r = s.rx; auto &t = s.tx;
  return get(f, s.epoch, r.received, r.processed, r.gapBegin, r.gapEnd,
    t.loaded, t.sent, t.acknowledged, t.replayBegin, t.replayEnd);
}
static bool save(const Options &o, const NativeCheckpoint &s) {
  ZiFile f{o.stateOut, ZiFile::Write | ZiFile::NoFollow | ZiFile::GC, 0600};
  uint32_t magic = 0x5a464958, version = 4, count = s.history.length();
  if (!f || !put(f, magic, version, s.businessKey, count) ||
      !putState(f, s)) return false;
  for (const auto &h : s.history) {
    const auto &dt = h.originalTime;
    uint64_t id = h.body->id(); bool admin = h.body->administrative();
    Zfix::Fmt::Error error;
    auto body = h.body->archive(error);
    if (error || !body) return false;
    if (!put(f, h.epoch, h.seqNo, id, admin, dt.julian(), dt.sec(), dt.nsec()) ||
      !putText(f, h.body->msgType()) || !putText(f, body.cspan())) return false;
  }
  return f.sync() == Zi::OK;
}
static bool load(const Options &o, NativeCheckpoint &s) {
  ZiFile f{o.stateIn, ZiFile::ReadOnly | ZiFile::NoFollow | ZiFile::GC};
  uint32_t magic, version, count;
  if (!f || !get(f, magic, version, s.businessKey, count) ||
      magic != 0x5a464958 || version != 4 || count > o.retain ||
      s.businessKey != o.keyOffset || !getState(f, s)) return false;
  if (!s.epoch) return false;
  // Cursor and range validation runs on each selected link owner during prepare.
  uint64_t last = 0, bytes = 0;
  for (unsigned i = 0; i < count; ++i) {
    Zfix::TxHistory h; uint64_t id; bool admin;
    Zfix::SessionString type, body;
    if (!get(f, h.epoch, h.seqNo, id, admin, h.originalTime.julian(),
	h.originalTime.sec(), h.originalTime.nsec()) || !getText(f, type) || !getText(f, body) ||
	h.epoch != s.epoch || h.seqNo <= last || h.seqNo >= s.tx.loaded || admin ||
	type != NewOrder::MsgType || !*h.originalTime) return false;
    Zfix::Fmt::Error error;
    h.body = Zfix::TxBody::restore<NewOrder>(body, id, error);
    if (error || !h.body || h.body->memory() > o.cacheBytes - bytes) return false;
    Order order;
    if (!Zfix::Fmt::read<Order, ZuFacet::FIX, Zfix::ArchiveField>(order, body, error) ||
	!id || order.key != id || id > s.businessKey)
      return false;
    bytes += h.body->memory(); last = h.seqNo; s.history.push(ZuMv(h));
  }
  return f.offset() == f.size();
}
template <typename Net> static int server(const Options &options, Ports ports) {
  NativeState state{options, ZuMv(ports), true};
  auto mxParams = nativeMxParams(options, true);
  mxParams.tcpBufAllocator(new typename Net::template RxAllocator<>{
    mxParams.rxRingSize()})
    .scheduler([](auto &s) {
    s.nThreads(4)
      .thread(1, [](auto &t) { t.isolated(1); }).thread(2, [](auto &t) { t.isolated(1); })
      .thread(3, [](auto &t) { t.isolated(1); }).thread(4, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2);
#ifdef ZiMultiplex_BENCH
  if (state.diag) {
    state.diag->prepare(mxParams.scheduler().nThreads());
    mxParams.benchFn(state.diag->observer());
  }
#endif
  ZiMultiplex mx{ZuMv(mxParams)};
  auto deadline = Zm::now(options.deadline); auto start = monotonic();
  if (!mx.start()) return 1;
  for (auto &result : state.results) {
    result.backend = backendName(mx.backend());
    result.rxRingSize = mx.rxRingSize(); result.txRingSize = mx.txRingSize();
  }
  using Hub = NativeHub<Net, false>;
  Rows<ZuPtr<Hub>> hubs;
  for (unsigned i = 0; i < options.sessions; ++i) {
    ZuPtr<Hub> hub{new Hub{&state, i}};
    typename Net::ServerParams params{&mx, "3", "4"};
    if constexpr (Net::Encrypted) params.certPath(options.cert).keyPath(options.key);
    hub->initialized = hub->init(ZuMv(params)); hub->active = hub->initialized && hub->start();
    if (!hub->active) state.fail();
    else hub->listen();
    hubs.push(ZuMv(hub));
  }
  if (!state.errors.load_()) {
    for (unsigned i = 0; i < options.sessions; ++i)
      if (state.listening.timedwait(deadline)) { state.fail(); break; }
    if (!state.errors.load_()) {
      Ready ready; ready.backend = backendName(mx.backend());
      ready.rxRingSize = mx.rxRingSize(); ready.txRingSize = mx.txRingSize();
      ready.role = "acceptor"; ready.ports = state.ports; output(ready);
      if (state.done.timedwait(deadline)) state.fail();
    }
  }
  // Stop admission on AppRx, then cross a network Rx barrier before sampling
  // created links. Wait for each logical stop before retiring hub owners.
  auto closing = Zm::now(options.deadline); ZmSemaphore listeners;
  unsigned active = 0;
  for (auto &hub : hubs) if (hub->active) {
    ++active; auto p = hub.ptr();
    p->rxRun([p, &listeners] { p->stopListening(); listeners.post(); });
  }
  for (unsigned i = 0; i < active; ++i) if (listeners.timedwait(closing)) state.fail();
  for (auto &hub : hubs) if (hub->active) hub->close();
  unsigned expected = 0; ZmSemaphore sampled;
  // Network Rx publishes its exact construction count through this barrier.
  mx.rxRun([&state, &expected, &sampled] { expected = state.created; sampled.post(); });
  if (sampled.timedwait(closing)) {
    state.fail(); sampled.wait(); // retain/drain the publication even on timeout
  }
  for (unsigned i = 0; i < expected; ++i) if (state.stopped.timedwait(closing)) {
    state.fail(); for (auto &hub : hubs) if (hub->active) hub->close();
    state.stopped.wait();
  }
  heapEvent();
  for (auto &hub : hubs) if (hub->active && !hub->stop()) state.fail();
  for (auto &hub : hubs) if (hub->initialized) hub->final();
  mx.stop(); hubs.length(0);
#ifndef _WIN32
  if (state.serverWindow && !state.serverWindow->finish("zfix", options.sessions)) state.fail();
#endif
#ifdef ZiMultiplex_BENCH
  if (state.diag) state.diag->report(backendName(mx.backend()));
#endif
  if (state.closed != state.expected || state.ready != state.expected) state.fail();
  const unsigned errors = state.errors.load_(); bool failed = errors;
  for (auto &r : state.results) {
    r.role = "acceptor"; r.errors += errors; r.elapsedNs = monotonic() - start;
    r.targetRate = options.rate; r.seed = options.seed; r.cap = options.count;
    r.inFlightLimit = options.inFlight; r.warmup = options.warmup;
    if ((!options.duration && r.progress.admitted != uint64_t(options.warmup) + options.count) ||
	r.progress.complete != r.progress.admitted) ++r.errors;
    failed |= bool(r.errors); output(r);
  }
  return failed ? 1 : 0;
}
template <typename Net> static int client(const Options &options, Ports ports) {
  NativeState state{options, ZuMv(ports)};
  if (options.stateIn.length() && !load(options, state.checkpoint)) {
    ZiLOG(Error, "zfix", "invalid native workload checkpoint"); return 1;
  }
  NativeHub<Net, true> hub{&state};
  auto mxParams = nativeMxParams(options, false);
  mxParams.tcpBufAllocator(new typename Net::template RxAllocator<>{
    mxParams.rxRingSize()})
    .scheduler([](auto &s) {
    s.nThreads(4)
      .thread(1, [](auto &t) { t.isolated(1); }).thread(2, [](auto &t) { t.isolated(1); })
      .thread(3, [](auto &t) { t.isolated(1); }).thread(4, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2);
#ifdef ZiMultiplex_BENCH
  if (state.diag) {
    state.diag->prepare(mxParams.scheduler().nThreads());
    mxParams.benchFn(state.diag->observer());
  }
#endif
  ZiMultiplex mx{ZuMv(mxParams)};
  auto deadline = Zm::now(options.deadline);
  if (!mx.start()) return 1;
  for (auto &result : state.results) {
    result.backend = backendName(mx.backend());
    result.rxRingSize = mx.rxRingSize(); result.txRingSize = mx.txRingSize();
  }
  typename Net::ClientParams params{&mx, "3", "4"};
  if constexpr (Net::Encrypted) params.caPath(options.ca);
  bool initialized = hub.init(ZuMv(params)), active = initialized && hub.start();
#ifndef _WIN32
  if (state.window) state.window->configure(
    WindowClient::Post{[&hub](WindowClient::Fn fn) { hub.rxRun([fn = ZuMv(fn)] { fn(); }); }},
    WindowClient::Fn{[&state] { state.fail(); }});
#endif
  Rows<ZmRef<NativeLink<Net, true>>> links;
  if (!active) state.fail();
  else {
    for (unsigned i = 0; i < options.sessions; ++i) {
      ZmRef<NativeLink<Net, true>> link = new NativeLink<Net, true>{&hub, i};
      if ((options.stateIn.length() || options.resetLogon) && !link->prepare()) state.fail();
      link->connect(options.host, state.ports[i]); links.push(ZuMv(link));
    }
    if (state.readySem.timedwait(deadline)) state.fail();
    else if (!state.errors.load_()) { Ready ready; ready.backend = backendName(mx.backend()); ready.role = "initiator";
      ready.rxRingSize = mx.rxRingSize(); ready.txRingSize = mx.txRingSize();
      ready.ports = state.ports; output(ready); }
    if (!state.errors.load_() && state.done.timedwait(deadline)) state.fail();
#ifndef _WIN32
    if (!state.errors.load_() && state.window && !state.window->finish("zfix", deadline)) state.fail();
#endif
    for (auto &link : links) {
      if (state.errors.load_()) link->disconnect(); else link->logout();
    }
    auto closing = Zm::now(options.deadline);
    for (unsigned i = 0, n = links.length(); i < n; ++i)
      if (state.stopped.timedwait(closing)) {
	state.fail(); for (auto &link : links) link->disconnect();
	// Forced local retirement no longer depends on a peer Session::Logout. Preserve
	// owners until the normal framework stop barrier has actually drained.
	state.stopped.wait();
      }
  }
#ifndef _WIN32
  if (state.window) state.window->shutdown();
#endif
  heapEvent();
  if (active && !hub.stop()) state.fail();
  if (initialized) hub.final();
  if (!ZiResolver::stopSync()) state.fail();
  ZiResolver::final();
  mx.stop(); links.length(0);
#ifdef ZiMultiplex_BENCH
  if (state.diag) state.diag->report(backendName(mx.backend()));
#endif
  if (state.closed != options.sessions || state.ready != options.sessions) state.fail();
  const unsigned errors = state.errors.load_(); bool failed = errors;
  for (auto &r : state.results) {
    state.measurements[r.session - 1].finish(r);
    r.role = "initiator"; r.errors += errors;
    if (r.progress.inFlight || r.progress.invalid || r.progress.refused ||
	(!options.duration && r.progress.complete != options.count)
	  || r.ackNs.count != r.progress.complete ||
	r.terminalNs.count != r.progress.complete) ++r.errors;
    failed |= bool(r.errors);
  }
  if (!failed && options.stateOut.length()) {
    state.checkpoint.businessKey = options.keyOffset + state.results[0].progress.complete;
    if (!save(options, state.checkpoint)) {
      ZiLOG(Error, "zfix", "cannot save native workload checkpoint");
      ++state.results[0].errors; failed = true;
    }
  }
  for (const auto &r : state.results) output(r);
  return failed ? 1 : 0;
}
int native(bool acceptor, int argc, char **argv) {
  Options o; Ports ports;
  if (!options(o, acceptor, argc, argv, ports)) { usage(acceptor); return 2; }
  if (o.help) { usage(acceptor); return 0; }
  if ((o.rxRingSize && (o.rxRingSize < ZiMxParams::MinRingSize ||
      o.rxRingSize > ZiMxParams::MaxRingSize)) ||
      (o.txRingSize && (o.txRingSize < ZiMxParams::MinRingSize ||
      o.txRingSize > ZiMxParams::MaxRingSize))) {
    fprintf(stderr, "ring sizes must be zero or in [%u, %u]\n",
      unsigned(ZiMxParams::MinRingSize), unsigned(ZiMxParams::MaxRingSize));
    return 2;
  }
#ifndef ZiMultiplex_BENCH
  if (o.observeIO) {
    fprintf(stderr, "--observe-io requires a ZiMultiplex_BENCH build\n");
    return 2;
  }
#endif
  ZiLog::init(acceptor ? "zfixd" : "zfix");
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  int result;
#ifdef ZiMultiplex_BENCH
  if (o.observeIO) {
    if (acceptor) result = o.tcp ? server<DiagnosticNet<Zfix::TCP>>(o, ZuMv(ports)) :
      server<DiagnosticNet<Zfix::TLS>>(o, ZuMv(ports));
    else result = o.tcp ? client<DiagnosticNet<Zfix::TCP>>(o, ZuMv(ports)) :
      client<DiagnosticNet<Zfix::TLS>>(o, ZuMv(ports));
  } else
#endif
  if (acceptor) result = o.tcp ? server<Zfix::TCP>(o, ZuMv(ports)) :
    server<Zfix::TLS>(o, ZuMv(ports));
  else result = o.tcp ? client<Zfix::TCP>(o, ZuMv(ports)) : client<Zfix::TLS>(o, ZuMv(ports));
  ZiLog::stop();
  return result;
}
} // ZfixLoad
