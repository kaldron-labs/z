//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZmRef.hh>
#include <zlib/ZiFile.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiResolver.hh>
#include "load_main.hh"
#include "load_native.hh"

namespace ZfixLoad {

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
    if (!put(f, h.epoch, h.seqNo, id, admin, dt.julian(), dt.sec(), dt.nsec()) ||
      !putText(f, h.body->msgType()) || !putText(f, h.body->body())) return false;
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
    if (Zfix::Fmt::load(order, body) || !id || order.key != id || id > s.businessKey)
      return false;
    bytes += h.body->memory(); last = h.seqNo; s.history.push(ZuMv(h));
  }
  return f.offset() == f.size();
}
template <typename Net> static int server(const Options &options, Ports ports) {
  NativeState state{options, ZuMv(ports), true};
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(4)
      .thread(1, [](auto &t) { t.isolated(1); }).thread(2, [](auto &t) { t.isolated(1); })
      .thread(3, [](auto &t) { t.isolated(1); }).thread(4, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2)};
  auto deadline = Zm::now(options.deadline); auto start = monotonic();
  if (!mx.start()) return 1;
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
      Ready ready; ready.role = "acceptor"; ready.ports = state.ports; output(ready);
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
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(4)
      .thread(1, [](auto &t) { t.isolated(1); }).thread(2, [](auto &t) { t.isolated(1); })
      .thread(3, [](auto &t) { t.isolated(1); }).thread(4, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2)};
  auto deadline = Zm::now(options.deadline);
  if (!mx.start()) return 1;
  typename Net::ClientParams params{&mx, "3", "4"};
  if constexpr (Net::Encrypted) params.caPath(options.ca);
  bool initialized = hub.init(ZuMv(params)), active = initialized && hub.start();
  Rows<ZmRef<NativeLink<Net, true>>> links;
  if (!active) state.fail();
  else {
    for (unsigned i = 0; i < options.sessions; ++i) {
      ZmRef<NativeLink<Net, true>> link = new NativeLink<Net, true>{&hub, i};
      if ((options.stateIn.length() || options.resetLogon) && !link->prepare()) state.fail();
      link->connect(options.host, state.ports[i]); links.push(ZuMv(link));
    }
    if (state.readySem.timedwait(deadline)) state.fail();
    else if (!state.errors.load_()) { Ready ready; ready.role = "initiator";
      ready.ports = state.ports; output(ready); }
    if (!state.errors.load_() && state.done.timedwait(deadline)) state.fail();
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
  heapEvent();
  if (active && !hub.stop()) state.fail();
  if (initialized) hub.final();
  if (!ZiResolver::stop()) state.fail();
  ZiResolver::final();
  mx.stop(); links.length(0);
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
  ZiLog::init(acceptor ? "zfixd" : "zfix");
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  int result;
  if (acceptor) result = o.tcp ? server<Zfix::TCP>(o, ZuMv(ports)) :
    server<Zfix::TLS>(o, ZuMv(ports));
  else result = o.tcp ? client<Zfix::TCP>(o, ZuMv(ports)) : client<Zfix::TLS>(o, ZuMv(ports));
  ZiLog::stop();
  return result;
}
} // ZfixLoad
