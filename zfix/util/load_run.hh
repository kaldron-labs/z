//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZfixLoadRun_HH
#define ZfixLoadRun_HH

#include <iostream>
#include <zlib/ZuPrint.hh>
#include <zlib/ZmTime.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZfCLI.hh>
#include "load.hh"
#include "load_cache.hh"

namespace ZfixLoad {

struct Options {
  Text		host{"localhost"}, bind{"127.0.0.1"}, ports, cert, key, ca;
  Text		sender{"ZFIX-C"}, target{"ZFIX-S"};
  Mode::T	mode = Mode::FIX4;
  Version::T	version = Version::V4_4;
  Scenario::T	scenario = Scenario::Orders;
  Text		stateIn, stateOut;
  unsigned	port = 0, sessions = 1, count = 1000, warmup = 100, inFlight = 64;
  unsigned	connections = 1;
  unsigned	duration = 0, deadline = 30, heartbeat = 30;
  unsigned	parties = 0, subs = 0, text = 0, binary = 0, retain = 4096;
  uint64_t	rate = 0, seed = 1, queueBytes = 16U<<20, cacheBytes = 64U<<20;
  uint64_t	keyOffset = 0, recoverRx = 0;
  bool		tcp = false, help = false, resetLogon = false;

  Shape shape() const { return {parties, subs, text, binary}; }
};

ZfStruct(, (Options, CLI), (host, (CLI::Long<"host">), String),
  (bind, (CLI::Long<"bind">), String), (port, (CLI::Long<"port">), UInt32),
  (ports, (CLI::Long<"ports">), String), (cert, (CLI::Long<"cert">), String),
  (key, (CLI::Long<"key">), String), (ca, (CLI::Long<"ca">), String),
  (sender, (CLI::Long<"sender">), String), (target, (CLI::Long<"target">), String),
  (mode, (CLI::Long<"mode">, Enum<Mode::CLI>), Int8),
  (version, (CLI::Long<"version">, Enum<Version::Wire>), Int8),
  (scenario, (CLI::Long<"scenario">, Enum<Scenario::CLI>), Int8),
  (sessions, (CLI::Long<"sessions">), UInt32),
  (stateIn, (CLI::Long<"state-in">), String), (stateOut, (CLI::Long<"state-out">), String),
  (connections, (CLI::Long<"connections">), UInt32),
  (keyOffset, (CLI::Long<"key-offset">), UInt64),
  (recoverRx, (CLI::Long<"recover-rx">), UInt64),
  (resetLogon, (CLI::Long<"reset-logon">), Bool),
  (count, (CLI::Long<"count">), UInt32), (warmup, (CLI::Long<"warmup">), UInt32),
  (inFlight, (CLI::Long<"inflight">), UInt32), (duration, (CLI::Long<"duration">), UInt32),
  (deadline, (CLI::Long<"deadline">), UInt32), (heartbeat, (CLI::Long<"heartbeat">), UInt32),
  (parties, (CLI::Long<"parties">), UInt32), (subs, (CLI::Long<"subs">), UInt32),
  (text, (CLI::Long<"text">), UInt32), (binary, (CLI::Long<"binary">), UInt32),
  (retain, (CLI::Long<"retain">), UInt32), (rate, (CLI::Long<"rate">), UInt64),
  (seed, (CLI::Long<"seed">), UInt64), (queueBytes, (CLI::Long<"queue-bytes">), UInt64),
  (cacheBytes, (CLI::Long<"cache-bytes">), UInt64), (tcp, (CLI::Long<"tcp">), Bool),
  (help, (CLI::Flag<'h'>, CLI::Long<"help">), Bool));
struct Ports : ZtArray<unsigned, ZtArrayHeapID<"Zfix.Load.Ports">> {
  ZuDerive_(Ports, (ZtArray<unsigned, ZtArrayHeapID<"Zfix.Load.Ports">>));
friend ZfJSON::AsArray<ZfFieldTC::UInt32> ZfJSON_Fmt(Ports *);
};

bool options(Options &, bool server, int argc, char **argv, Ports &);
void usage(bool server);
struct Ready {
  ZuCSpan	event{"ready"}, engine{"zfix"}, role;
  Ports		ports;
};

ZfStruct(, (Ready, JSON), (event, , String), (engine, , String), (role, , String), (ports, ,
  UDT));
struct ReconnectReady {
  ZuCSpan	event{"reconnect_ready"};
  ZuCSpan	barrier{"owner-drained"};
  unsigned	connection = 0;
  Ports		ports;
};

ZfStruct(, (ReconnectReady, JSON), (event, , String),
  (barrier, , String), (connection, , UInt32), (ports, , UDT));
struct Result {
  ZuCSpan	event{"result"}, engine{"zfix"}, role;
  unsigned	session = 0, errors = 0;
  Progress	progress;
  uint64_t	warmup = 0, elapsedNs = 0, targetRate = 0, seed = 0;
  unsigned	cap = 0, inFlightLimit = 0;
  Quantiles	ackNs, terminalNs, scheduleNs, intendedAckNs, intendedTerminalNs;
  uint64_t	rxFrames = 0, txFrames = 0, rxBytes = 0, txBytes = 0;
  uint64_t	retainedPeak = 0, historyMiss = 0, cached = 0, replayRefused = 0;
  uint64_t	oldReports = 0;
  uint64_t	nextSender = 0, nextTarget = 0;
  unsigned	connections = 0;
};

ZfStruct(, (Result, JSON), (event, , String), (engine, , String), (role, , String),
  (session, , UInt32), (errors, , UInt32), (progress, , UDT), (warmup, , UInt64),
  (elapsedNs, , UInt64), (targetRate, , UInt64), (seed, , UInt64), (cap, , UInt32),
  (inFlightLimit, , UInt32), (ackNs, , UDT), (terminalNs, , UDT), (scheduleNs, , UDT),
  (intendedAckNs, , UDT), (intendedTerminalNs, , UDT),
  (rxFrames, , UInt64), (txFrames, , UInt64), (rxBytes, , UInt64), (txBytes, , UInt64),
  (retainedPeak, , UInt64), (historyMiss, , UInt64), (cached, , UInt64),
  (replayRefused, , UInt64), (oldReports, , UInt64), (nextSender, , UInt64),
  (nextTarget, , UInt64), (connections, , UInt32));
template <typename T> void output(const T &value) {
  Text json; ZfJSON::save(json, value); json << '\n';
  std::cout << json << std::flush;
}
struct Clock { uint64_t operator()() const { return monotonic(); } };

struct Measurements {
  Samples	ack, terminal, schedule, intendedAck, intendedTerminal;

  void finish(Result &r) {
    r.ackNs = ack.finish(); r.terminalNs = terminal.finish(); r.scheduleNs = schedule.finish();
    r.intendedAckNs = intendedAck.finish(); r.intendedTerminalNs = intendedTerminal.finish();
  }
};

struct HeapEvent {
  ZuCSpan	event{"heap"}, scope{"process-framework-heaps-at-logical-drain"};
  uint64_t	frameworkAllocCalls = 0, frameworkLiveBytes = 0;
};

ZfStruct(, (HeapEvent, JSON), (event, , String), (scope, , String),
  (frameworkAllocCalls, , UInt64), (frameworkLiveBytes, , UInt64));
void heapEvent();
namespace Request {

  enum { Order, Cancel, Replace };
}

struct CachedResult {
  uint64_t	key = 0, original = 0, order = 0, first = 0, terminal = 0;
  unsigned	qty = 0, kind = Request::Order;
  ZuDecimal	price;
  ZuDateTime	time;
  bool		closed = false;
};

struct Prepared {
  CachedResult	value;
  bool		fresh = false;
};

// Workload business policy, shared by both independent wire/session engines.
// Cache entries own fixed scalars; report shape spans belong to this model.
struct BrokerData {
  Cache<uint64_t, CachedResult>	results;
  Progress			progress;
  uint64_t			nextExec = 0, cached = 0, replayRefused = 0;

  BrokerData(const Options &o, unsigned index) : results{o.retain, o.cacheBytes},
    nextExec{uint64_t(index)<<48} { }
};

class Broker : public BrokerData {
public:
  Broker(const Options &o, unsigned index) : BrokerData{o, index},
    m_options{o}, m_flow{workflow(o.scenario)}, m_model{{0, 0, o.text, o.binary}, o.seed} { }

  template <typename O, typename Emit> bool receive(const O &o, bool replay, Emit emit) {
    auto prepared = prepare(o, replay);
    auto &r = prepared.value;
    if (!r.key) return false;
    auto &payload = m_model.next(r.key, r.time, r.qty, r.price);
    auto kind = r.kind == Request::Cancel ? ZuCSpan{"4"} :
      r.kind == Request::Replace ? ZuCSpan{"5"} : ZuCSpan{"0"};
    if (!emit(report(payload, r.first, kind, r.order, r.original))) return false;
    ++progress.reports;
    if (r.terminal) {
      if (!emit(report(payload, r.terminal, "F", r.order, r.original))) return false;
      ++progress.reports;
    }
    if (prepared.fresh) {
      if (r.kind == Request::Order) ++progress.ack;
      if (r.kind != Request::Order || m_flow == Workflow::Orders)
	++progress.complete;
    }
    return true;
  }
private:
  Prepared prepare(const Order &, bool replay);
  Prepared prepare(const Cancel &, bool replay);
  Prepared prepare(const Replace &, bool replay);
  Prepared change(uint64_t key, uint64_t original, unsigned qty, ZuDecimal price,
    ZuDateTime time, unsigned kind, bool replay);
  const Options		&m_options;
  const unsigned	m_flow;
  Model			m_model;
  uint64_t		m_last = 0;
};

namespace Phase {

  enum { Idle, Warmup, Measured, Complete, Failed };
}

class Run {
public:
  explicit Run(const Options &o, unsigned index = 0) : m_options{o},
    m_flow{workflow(o.scenario)}, m_model{o.shape(), o.seed},
    m_calls{o.inFlight, m_flow}, m_acks{o.count}, m_terms{o.count},
    m_delay{o.count}, m_intendedAcks{o.count}, m_intendedTerms{o.count},
    m_key{(uint64_t(index)<<48) + o.keyOffset},
    m_floor{m_key}, m_namespace{uint64_t(index)<<48} {
    if (m_flow == Workflow::Replace) {
      const auto &o = m_model.order();
      m_follow.order.text = o.text; m_follow.order.encoded = o.encoded;
      // Model groups are immutable and outlive this read-only replacement view.
      m_follow.order.parties -= o.parties;
    }
  }

  bool start(uint64_t now);

  template <typename Send, typename Time = Clock> void pump(uint64_t now, Send send,
    Time time = {}) {
    if (m_phase != Phase::Warmup && m_phase != Phase::Measured) return;
    settle(now);
    const auto &progress = m_calls.progress();
    while ((m_phase == Phase::Warmup || m_phase == Phase::Measured) &&
	progress.inFlight < m_options.inFlight) {
      unsigned limit = m_phase == Phase::Warmup ? m_options.warmup : m_options.count;
      if (m_offered == limit || expired(now)) { settle(now); return; }
      auto due = intended(); if (now < due) return;
      const auto &o = m_model.next(++m_key, ZuDateTime{Zm::now()});
      bool warmup = m_phase == Phase::Warmup;
      if (!m_calls.add(o, now, m_options.rate ? due : now, warmup)) {
	m_phase = Phase::Failed; return;
      }
      if (!send(o)) { m_calls.refused(o.key); m_phase = Phase::Failed; return; }
      if (!warmup) {
	if (!m_delay.add(m_options.rate ? now - due : 0)) { m_phase = Phase::Failed; return; }
	if (progress.inFlight > m_peak) m_peak = progress.inFlight;
      }
      ++m_offered; now = time();
    }
  }

  unsigned receive(const Report &, uint64_t now);

  template <typename Send> unsigned receive(const Report &r, uint64_t now, Send send) {
    auto result = receive(r, now);
    auto flow = m_flow;
    if (result != Outcome::Ack || flow == Workflow::Orders) return result;
    auto key = ++m_key;
    auto time = ZuDateTime{Zm::now()};
    if (flow == Workflow::Cancel) {
      Order next; next.key = key; next.time = time; next.qty = r.qty; next.price = r.price;
      Cancel cancel{r.key, key, next.symbol, next.side, time, r.qty};
      if (!m_calls.follow(r.key, next) || !send(cancel)) m_phase = Phase::Failed;
    } else {
      m_follow.original = r.key;
      auto &next = m_follow.order; next.key = key; next.time = time;
      next.qty = r.qty == 999 ? 1 : r.qty + 1;
      next.price = r.price + ZuDecimal{1, 2};
      if (!m_calls.follow(r.key, next) || !send(m_follow)) m_phase = Phase::Failed;
    }
    return m_phase == Phase::Failed ? Outcome::Invalid : result;
  }

  uint64_t nextDue(uint64_t now) const;

  bool shape(ZuCSpan text, ZuBSpan binary) const { return m_model.shape(text, binary); }

  unsigned phase() const { return m_phase; }

  void finish(Result &, uint64_t now, Measurements *deferred = nullptr);
private:
  bool expired(uint64_t now) const {
    return m_phase == Phase::Measured && m_options.duration &&
      now - m_start >= uint64_t(m_options.duration) * 1000000000;
  }

  uint64_t intended() const {
    return m_options.rate ?
      m_start + uint64_t(uint128_t(m_offered) * 1000000000/m_options.rate) : m_start;
  }

  void settle(uint64_t);
  const Options		&m_options;
  const unsigned	m_flow;
  Model			m_model;
  Replace		m_follow;
  Calls			m_calls;
  Samples		m_acks, m_terms, m_delay, m_intendedAcks, m_intendedTerms;
  Progress		m_baseline;
  uint64_t		m_start = 0, m_end = 0, m_key = 0;
  uint64_t		m_floor, m_namespace, m_oldReports = 0;
  unsigned		m_offered = 0, m_completed = 0, m_peak = 0, m_phase = Phase::Idle;
};

bool shape(ZuCSpan text, ZuBSpan binary, unsigned textBytes, unsigned binaryBytes);
bool shape(const Order &, Shape);
} // ZfixLoad

#endif /* ZfixLoadRun_HH */
