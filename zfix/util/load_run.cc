//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>
#include <stdlib.h>
#include <string.h>
#include <zlib/ZuBox.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZtcHeap.hh>
#include <zlib/ZePlatform.hh>
#include "load_run.hh"

namespace ZfixLoad {

ZfStructImpl(Options); ZfStructImpl(Options, CLI);
ZfStructImpl(Ready); ZfStructImpl(Ready, JSON);
ZfStructImpl(ReconnectReady); ZfStructImpl(ReconnectReady, JSON);
ZfStructImpl(Result); ZfStructImpl(Result, JSON);
ZfStructImpl(HeapEvent); ZfStructImpl(HeapEvent, JSON);
void heapEvent() {
  auto enabled = getenv("ZFIX_HEAPSTATS");
  if (!enabled || !*enabled || !strcmp(enabled, "0")) return;
  HeapEvent record;
  Ztc::HeapMgr::all(Ztc::HeapMgr::AllFn{&record, [](HeapEvent *sum, Ztc::Heap *heap) {
    Ztc::HeapTelemetry data; heap->telemetry(data);
    sum->frameworkAllocCalls += data.cacheAllocs + data.heapAllocs;
    sum->frameworkLiveBytes += data.allocated() * data.size;
  }});
  output(record);
}
void usage(bool server) {
  std::cerr << "usage: " << (server ? "zfixd" : "zfix") <<
    " --port N [--sessions N] [--ports N,N,...] [--tcp]\n"
    "  [--host NAME] [--bind IP] [--cert PEM --key PEM --ca PEM]\n"
    "  [--count N --warmup N --duration SECONDS --rate ORDERS_PER_SECOND]\n"
    "  [--inflight N --deadline SECONDS --heartbeat SECONDS]\n"
    "  [--scenario orders|groups|cancel|replace --parties N --subs N --text N]\n"
    "  [--sender ID --target ID --seed N --mode 4|44A|T11]\n"
    "  [--version FIX.4.0|FIX.4.1|FIX.4.2|FIX.4.3|FIX.4.4|FIXT.1.1]\n"
    "  [--retain N --cache-bytes N --queue-bytes N]\n"
    "  [--state-in FILE --state-out FILE --key-offset N --connections N --reset-logon]\n"
    "  [--recover-rx N]\n";
}
bool options(Options &o, bool server, int argc, char **argv, Ports &ports) {
  ports.length(0);
  try { argc = ZfCLI::load(o, argc, argv); }
  catch (const ZeException &e) { std::cerr << e << '\n';
    return false; }
  if (o.help) return true;
  if (argc != 1 || !o.sessions || !o.count || !o.inFlight || !o.deadline || !o.retain ||
      !o.cacheBytes || !o.queueBytes || o.queueBytes > UINT64_MAX - (1U<<20) || o.port > 65535
	|| o.sessions > 65535 ||
      o.duration >= o.deadline || o.queueBytes > UINT64_MAX/o.sessions ||
      o.count > UINT_MAX - o.warmup ||
      !o.sender.length() || !o.target.length() ||
      o.mode < 0 || o.mode >= Mode::N || o.version < 0 || o.version >= Version::N ||
      o.scenario < 0 || o.scenario >= Scenario::N) return false;
  if (o.scenario == Scenario::Groups) {
    if (!o.parties) o.parties = 32;
    if (!o.subs) o.subs = 8;
  }
  bool lifecycle = o.stateIn.length() || o.stateOut.length() || o.keyOffset ||
    o.connections != 1 || o.resetLogon || o.recoverRx;
  if (!o.connections || o.connections > UINT_MAX/o.sessions ||
      o.keyOffset > (uint64_t(1)<<48) - 1 - uint64_t(o.count) - o.warmup ||
      (lifecycle && (o.sessions != 1 || o.warmup || workflow(o.scenario) != Workflow::Orders)) ||
      (server ? o.stateIn.length() || o.stateOut.length() || o.keyOffset ||
	(o.resetLogon && o.connections < 2) : o.connections != 1 ||
	(o.resetLogon && o.stateIn.length())) ||
      (o.recoverRx && (!server || o.connections != 2 || o.resetLogon ||
	o.recoverRx > INT_MAX))) return false;
  // Fixture bounds, matching the native default codec budgets.
  if (o.text > (1U<<18) ||
      uint64_t(o.parties) * (uint64_t(o.subs) + 1) > (1U<<16)) return false;
  if (!o.tcp && (server ? !o.cert.length() || !o.key.length() : !o.ca.length())) return false;
  if (o.ports.length()) {
    ZuCSpan input{o.ports}; unsigned begin = 0;
    for (unsigned i = 0, n = input.length(); i <= n; ++i) if (i == n || input[i] == ',') {
      ZuBox<unsigned> value;
      auto n = value.scan(ZuCSpan{input.data() + begin, i - begin});
      if (!n || n != i - begin || !value.val() || value.val() > 65535) return false;
      ports.push(value.val()); begin = i + 1;
    }
    if (ports.length() != o.sessions) return false;
  } else {
    if ((!server && !o.port) || (o.port && o.sessions > 65536 - o.port)) return false;
    for (unsigned i = 0; i < o.sessions; ++i) ports.push(o.port ? o.port + i : 0);
  }
  return true;
}
// The caller checks the outer count before visiting entries. Byte validation
// is separate so hot consumers can compare their retained model directly.
static bool groups(const Order &o, unsigned subs) {
  for (unsigned i = 0, n = o.parties.length(); i < n; ++i) {
    const auto &party = o.parties[i];
    if (party.id != "PARTY" || party.source != "D" ||
	party.role != 1 + i % 2 || party.subs.length() != subs) return false;
    for (unsigned j = 0; j < subs; ++j) {
      const auto &sub = party.subs[j];
      if (sub.id != "DESK" || sub.type != 1 + j % 2) return false;
    }
  }
  return true;
}
Prepared Broker::prepare(const Order &o, bool replay) {
  if (!valid(o) || o.parties.length() != m_options.parties ||
      !m_model.shape(o.text) || !groups(o, m_options.subs)) return {};
  if (auto r = results.get(o.key)) {
    if (r->kind != Request::Order || r->qty != o.qty || r->price != o.price) return {};
    ++cached; return {*r};
  }
  if (o.key <= m_last) { if (replay) ++replayRefused; return {}; }
  bool fill = m_flow == Workflow::Orders;
  if (nextExec > UINT64_MAX - (fill ? 2 : 1)) return {};
  CachedResult r{o.key, 0, o.key, ++nextExec, fill ? ++nextExec : 0,
    o.qty, Request::Order, o.price, o.time, fill};
  if (!results.put(o.key, r)) return {};
  m_last = o.key;
  ++progress.offered; ++progress.admitted;
  return {r, true};
}
Prepared Broker::prepare(const Cancel &o, bool replay) {
  if (m_flow != Workflow::Cancel || !o.original || !o.key ||
      !o.qty || !*o.time || o.symbol != "ZFIX" || o.side != "1") return {};
  return change(o.key, o.original, o.qty, {}, o.time, Request::Cancel, replay);
}
Prepared Broker::prepare(const Replace &o, bool replay) {
  if (m_flow != Workflow::Replace || !valid(o.order) ||
      o.order.parties.length() != m_options.parties ||
      !m_model.shape(o.order.text) ||
      !groups(o.order, m_options.subs)) return {};
  return change(o.order.key, o.original, o.order.qty, o.order.price, o.order.time,
    Request::Replace, replay);
}
Prepared Broker::change(uint64_t key, uint64_t original, unsigned qty, ZuDecimal price,
    ZuDateTime time, unsigned kind, bool replay) {
  if (!original || original == key) return {};
  // A duplicate cancellation remains answerable after its original ages out;
  // it carries no price, so the retained result is already authoritative.
  if (auto r = results.get(key)) {
    if (r->kind != kind || r->original != original || r->qty != qty ||
      (kind == Request::Replace && r->price != price)) return {};
    ++cached; return {*r};
  }
  if (key <= m_last) { if (replay) ++replayRefused; return {}; }
  auto root = results.get(original);
  if (!root || root->kind != Request::Order || root->closed ||
      (kind == Request::Cancel && root->qty != qty)) return {};
  bool fill = kind == Request::Replace;
  if (nextExec > UINT64_MAX - (fill ? 2 : 1)) return {};
  CachedResult r{key, original, root->order, ++nextExec, fill ? ++nextExec : 0,
    qty, kind, fill ? price : root->price, time, true};
  if (!results.put(key, r)) return {};
  m_last = key;
  // Insertion may evict the original; do not reuse its prior borrowed pointer.
  if (auto root = results.get(original)) root->closed = true;
  return {r, true};
}
bool Run::start(uint64_t now) {
  if (m_phase != Phase::Idle || !now) return false;
  m_phase = m_options.warmup ? Phase::Warmup : Phase::Measured;
  m_start = now; return true;
}
void Run::settle(uint64_t now) {
  if (m_calls.progress().inFlight) return;
  switch (m_phase) {
    case Phase::Warmup:
      if (m_completed != m_options.warmup) return;
      m_baseline = m_calls.progress(); m_offered = m_completed = 0;
      m_phase = Phase::Measured; m_start = now; break;
    case Phase::Measured:
      if (m_offered < m_options.count && !expired(now)) return;
      m_phase = Phase::Complete; m_end = now; break;
  }
}
unsigned Run::receive(const Report &r, uint64_t now) {
  // The cold checkpoint validated this completed prefix. Historical responses
  // remain diagnostics; they never complete one of this process's fresh calls.
  if (r.key > m_namespace && r.key <= m_floor && !r.original &&
      (r.kind == "0" || r.kind == "F")) {
    ++m_oldReports; return Outcome::Duplicate;
  }
  auto result = m_calls.receive(r, now, &m_acks, &m_terms, &m_intendedAcks, &m_intendedTerms);
  switch (result) {
    case Outcome::Invalid: m_phase = Phase::Failed; break;
    case Outcome::Complete:
      m_calls.release(r.original ? r.original : r.key); ++m_completed; settle(now); break;
  }
  return result;
}
uint64_t Run::nextDue(uint64_t now) const {
  if ((m_phase != Phase::Warmup && m_phase != Phase::Measured) ||
      m_calls.progress().inFlight >= m_options.inFlight) return 0;
  unsigned limit = m_phase == Phase::Warmup ? m_options.warmup : m_options.count;
  if (m_offered == limit || expired(now)) return 0;
  return intended();
}
void Run::finish(Result &r, uint64_t now, Measurements *deferred) {
  const auto &s = m_calls.progress(); const auto &b = m_baseline;
  r.progress = {s.offered - b.offered, s.admitted - b.admitted, s.ack - b.ack,
    s.complete - b.complete,
    s.reports - b.reports, s.duplicate - b.duplicate, s.unknown - b.unknown,
    s.invalid - b.invalid, s.refused - b.refused, s.inFlight, m_peak};
  r.warmup = b.complete;
  r.startNs = m_start; r.endNs = m_start ? (m_end ? m_end : now) : 0;
  r.elapsedNs = r.endNs - r.startNs;
  r.targetRate = m_options.rate; r.seed = m_options.seed; r.cap = m_options.count;
  r.inFlightLimit = m_options.inFlight;
  r.oldReports = m_oldReports;
  if (deferred) {
    deferred->ack = ZuMv(m_acks); deferred->terminal = ZuMv(m_terms);
      deferred->schedule = ZuMv(m_delay);
    deferred->intendedAck = ZuMv(m_intendedAcks);
      deferred->intendedTerminal = ZuMv(m_intendedTerms);
  } else {
    r.ackNs = m_acks.finish(); r.terminalNs = m_terms.finish(); r.scheduleNs = m_delay.finish();
    r.intendedAckNs = m_intendedAcks.finish(); r.intendedTerminalNs = m_intendedTerms.finish();
  }
  if (m_phase != Phase::Complete) ++r.errors;
  m_calls.clear();
}
bool shape(ZuCSpan text, unsigned textBytes) {
  if (text.length() != textBytes) return false;
  for (unsigned i = 0; i < textBytes; ++i)
    if (text[i] != 'a' + i % 26) return false;
  return true;
}
bool shape(const Order &o, Shape s) {
  if (o.parties.length() != s.parties || !shape(o.text, s.text)) return false;
  return groups(o, s.subs);
}
} // ZfixLoad
