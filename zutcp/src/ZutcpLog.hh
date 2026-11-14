//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Asynchronous TCP JSON sequence logging

#ifndef Zutcp_HH
#error "include zlib/Zutcp.hh before this header"
#endif

#include <zlib/ZuTime.hh>
#include <zlib/ZuArray.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmEngine.hh>
#include <zlib/ZmGuard.hh>
#include <zlib/ZmPLock.hh>
#include <zlib/ZmQueue.hh>
#include <zlib/ZmRing.hh>
#include <zlib/ZmRingFn.hh>
#include <zlib/ZmThread.hh>

#include <zlib/ZtString.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZiFile.hh>
#include <zlib/ZiIP.hh>

namespace ZutcpLog_ {

using namespace Zutcp_;

struct Diag {
  uint64_t	recordsEnqueued = 0;
  uint64_t	recordsWritten = 0;
  uint64_t	recordsDropped = 0;
  uint64_t	ringBackPressure = 0;
  uint64_t	writerFailures = 0;
  uint64_t	bytesWritten = 0;
};

class Params {
public:
  Params &enabled(bool v) { m_enabled = v; return *this; }
  Params &path(ZuCSpan v) { m_path = v; return *this; }
  Params &ringSize(unsigned v) { m_ringSize = v; return *this; }
  Params &age(unsigned v) { m_age = v; return *this; }

  bool enabled() const { return m_enabled; }
  ZuCSpan path() const { return m_path; }
  unsigned ringSize() const { return m_ringSize; }
  unsigned age() const { return m_age; }

private:
  bool		m_enabled = false;
  ParamString	m_path;
  unsigned	m_ringSize = (1<<20);
  unsigned	m_age = 8;
};

struct Reason {
  ZtEnum(ZutcpAPI, Reason, int8_t, None, Length, Checksum, Sequence, PAWS, Duplicate,
    Window, Admission, AppSend);
};
struct RecKind {
  ZtEnum(ZutcpAPI, RecKind, int8_t, Aggregate, Loss, RTO, TLP, RTT, NewReno);
};
struct RecReason {
  ZtEnum(ZutcpAPI, RecReason, int8_t, None, RACK, DuplicateACK, Canceled, Armed,
    Ack, PMTUDAck, Loss, PMTUDLoss, Expired, Probe, Backoff, ECNCE);
};
struct PathKind { ZtEnum(ZutcpAPI, PathKind, int8_t, Path, PMTUD); };
struct PathAction {
  ZtEnum(ZutcpAPI, PathAction, int8_t,
    Created, Updated, Received, Validated, Blocked, Expired, Hint, Acked, Lost, Sent);
};
struct PathReason {
  ZtEnum(ZutcpAPI, PathReason, int8_t,
    None, Client, Server, SYN, Segment, Timeout, PathHint, Probe, Admission, SendFail);
};
struct CloseInitiator { ZtEnum(ZutcpAPI, CloseInitiator, int8_t, Local, Remote); };
struct CloseTrigger {
  ZtEnum(ZutcpAPI, CloseTrigger, int8_t, Application, Error, IdleTimeout, Aborted);
};
struct CloseReason {
  ZtEnum(ZutcpAPI, CloseReason, int8_t, None, LocalClose, PeerFIN, PeerRST, Idle);
};
struct PktEvt {
  LinkInfo		linkInfo;
  TCPHeader		header;
  uint64_t		packetSize = 0, payloadSize = 0, bytesInFlight = 0;
  EcnMark::T		ecn = EcnMark::NotECT;
  Reason::T		reason = Reason::None;
};
struct AckEvt {
  LinkInfo		linkInfo;
  uint64_t		byteNext = 0, ackedBytes = 0, lostBytes = 0;
};
struct RecLogEvt {
  LinkInfo		linkInfo;
  uint64_t		transmissionID = 0, bytes = 0, deadlineUS = 0;
  uint64_t		latestRTTUS = 0, smoothedRTTUS = 0, rttVarianceUS = 0, minRTTUS = 0;
  uint64_t		cwnd = 0, ssthresh = 0, bytesInFlight = 0;
  RecKind::T		kind = RecKind::Aggregate;
  RecReason::T		reason = RecReason::None;
};
struct PathEvt {
  LinkInfo		linkInfo;
  uint64_t		bytes = 0;
  uint32_t		mtu = 0;
  PathKind::T		kind = PathKind::Path;
  PathAction::T		action = PathAction::Updated;
  PathReason::T		reason = PathReason::None;
  bool			validated = false;
};
struct CloseEvt {
  LinkInfo		linkInfo;
  uint64_t		errorCode = 0; // local application evidence, never sent on wire
  CloseInitiator::T	initiator = CloseInitiator::Local;
  CloseTrigger::T	trigger = CloseTrigger::Error;
  CloseReason::T	reason = CloseReason::None;
};
struct CxnStateEvt {
  LinkInfo		linkInfo;
  LinkState::T		oldState = LinkState::Starting, newState = LinkState::Handshaking;
};
struct CxnStartedEvt {
  ZiSockAddr		local, remote;
  LinkInfo		linkInfo;
};

} // namespace ZutcpLog_

using ZutcpLogDiag = ZutcpLog_::Diag;
using ZutcpLogParams = ZutcpLog_::Params;

#ifdef Zutcp_DEBUG

class ZutcpAPI ZutcpLogSink {
public:
  bool init(ZuCSpan, unsigned);
  void final();
  bool write(ZuCSpan);

private:
  Zi::Path	m_path;
  ZiFile	m_file;
};

class ZutcpAPI ZutcpLogger : private ZmEngine<ZutcpLogger> {
friend ZmEngine<ZutcpLogger>;
  using Engine = ZmEngine<ZutcpLogger>;
  using Lock = ZmPLock;
  using Guard = ZmGuard<Lock>;
  ZuDerive(Ring, (ZmRing<ZmRingMW<true>>));
  using Fn = ZmRingFn<ZutcpLogger *>;
  ZmQueueDerive(Queue_, Fn, ZmQueueHeapID<"Zutcp.Log.Queue">);
  class Queue : public Queue_ {
  public:
    using Lock = ZmPLock;
    using Guard = ZmGuard<Lock>;

    void push(Fn fn) {
      Guard guard(m_lock);
      Queue_::push(ZuMv(fn));
    }
    void unshift(Fn fn) {
      Guard guard(m_lock);
      Queue_::unshift(ZuMv(fn));
    }
    Fn shift() {
      Guard guard(m_lock);
      return Queue_::shift();
    }

  private:
    Lock	m_lock;
  };

public:
  using CtrlFn = ZmFn<void(bool), ZmFnHeapID<"ZmEngine.CtrlFn">>;

  struct Trace {
    bool		configured = false;
    bool		sinkOpened = false;
    bool		headerWritten = false;
    Zutcp::Vantage::T	vantage = Zutcp::Vantage::Unknown;
    ZutcpLogParams	params;
    ZutcpLogSink		sink;
  };

  ~ZutcpLogger();

  ZutcpLogger(const ZutcpLogger &) = delete;
  ZutcpLogger &operator =(const ZutcpLogger &) = delete;

  static ZutcpLogger *instance();

  static bool enabled() { return instance()->enabled_(); }
  static bool init(
    Trace &trace, const ZutcpLogParams &params, Zutcp::Vantage::T vantage) {
    return instance()->init_(trace, params, vantage);
  }
  static bool start() { return instance()->Engine::start(); }
  static void start(CtrlFn fn) {
    instance()->Engine::start(ZuMv(fn));
  }
  static bool stop() { return instance()->Engine::stop(); }
  static void stop(CtrlFn fn) {
    instance()->Engine::stop(ZuMv(fn));
  }
  static void stopIdle(CtrlFn fn) {
    instance()->stopIdle_(ZuMv(fn));
  }
  template <typename L>
  static void close(Trace &trace, L &&l) {
    instance()->close_(trace, ZuFwd<L>(l));
  }
  static void final(Trace &trace) { instance()->final_(trace); }
  static ZutcpLogDiag diag() { return instance()->diag_(); }

  static void cxnStarted(Trace &trace, ZutcpLog_::CxnStartedEvt event) {
    instance()->cxnStarted_(trace, ZuMv(event));
  }
  static void pktSent(Trace &trace, ZutcpLog_::PktEvt event) {
    instance()->pktSent_(trace, ZuMv(event));
  }
  static void pktRecv(Trace &trace, ZutcpLog_::PktEvt event) {
    instance()->pktRecv_(trace, ZuMv(event));
  }
  static void pktDrop(Trace &trace, ZutcpLog_::PktEvt event) {
    instance()->pktDrop_(trace, ZuMv(event));
  }
  static void pktsAcked(Trace &trace, ZutcpLog_::AckEvt event) {
    instance()->pktsAcked_(trace, ZuMv(event));
  }
  static void pktLost(Trace &trace, ZutcpLog_::RecLogEvt event) {
    instance()->pktLost_(trace, ZuMv(event));
  }
  static void markRetrans(Trace &trace, ZutcpLog_::RecLogEvt event) {
    instance()->markRetrans_(trace, ZuMv(event));
  }
  static void metricsUpd(Trace &trace, ZutcpLog_::RecLogEvt event) {
    instance()->metricsUpd_(trace, ZuMv(event));
  }
  static void lossTimerUpd(Trace &trace, ZutcpLog_::RecLogEvt event) {
    instance()->lossTimerUpd_(trace, ZuMv(event));
  }
  static void congStateUpd(Trace &trace, ZutcpLog_::RecLogEvt event) {
    instance()->congStateUpd_(trace, ZuMv(event));
  }
  static void pathUpdated(Trace &trace, ZutcpLog_::PathEvt event) {
    instance()->pathUpdated_(trace, ZuMv(event));
  }
  static void pmtudUpdated(Trace &trace, ZutcpLog_::PathEvt event) {
    instance()->pmtudUpdated_(trace, ZuMv(event));
  }
  static void cxnClosed(Trace &trace, ZutcpLog_::CloseEvt event) {
    instance()->cxnClosed_(trace, ZuMv(event));
  }
  static void cxnStateUpd(Trace &trace, ZutcpLog_::CxnStateEvt event) {
    instance()->cxnStateUpd_(trace, ZuMv(event));
  }
  template <typename L>
  static void log(Trace &trace, L &&l) {
    instance()->log_(trace, ZuFwd<L>(l));
  }

  void logCxnStarted(const ZutcpLog_::CxnStartedEvt &event, ZuTime time) {
    writeEvent("connection_started", event, time);
  }
  void logPktSent(const ZutcpLog_::PktEvt &event, ZuTime time) {
    writeEvent("segment_sent", event, time);
  }
  void logPktRecv(const ZutcpLog_::PktEvt &event, ZuTime time) {
    writeEvent("segment_received", event, time);
  }
  void logPktDrop(const ZutcpLog_::PktEvt &event, ZuTime time) {
    writeEvent("segment_dropped", event, time);
  }
  void logPktsAcked(const ZutcpLog_::AckEvt &event, ZuTime time) {
    writeEvent("bytes_acked", event, time);
  }
  void logPktLost(const ZutcpLog_::RecLogEvt &event, ZuTime time) {
    writeEvent("transmission_lost", event, time);
  }
  void logMarkRetrans(const ZutcpLog_::RecLogEvt &event, ZuTime time) {
    writeEvent("retransmit", event, time);
  }
  void logMetricsUpd(const ZutcpLog_::RecLogEvt &event, ZuTime time) {
    writeEvent("recovery_metrics", event, time);
  }
  void logLossTimerUpd(const ZutcpLog_::RecLogEvt &event, ZuTime time) {
    writeEvent("timer_updated", event, time);
  }
  void logCongStateUpd(const ZutcpLog_::RecLogEvt &event, ZuTime time) {
    writeEvent("congestion_updated", event, time);
  }
  void logPathUpdated(const ZutcpLog_::PathEvt &event, ZuTime time) {
    writeEvent("path_updated", event, time);
  }
  void logPmtudUpdated(const ZutcpLog_::PathEvt &event, ZuTime time) {
    writeEvent("mtu_updated", event, time);
  }
  void logCxnClosed(const ZutcpLog_::CloseEvt &event, ZuTime time) {
    writeEvent("connection_closed", event, time);
  }
  void logCxnStateUpd(const ZutcpLog_::CxnStateEvt &event, ZuTime time) {
    writeEvent("connection_state", event, time);
  }
private:
  ZutcpLogger();

  bool enabled_() const { return state() == ZmEngineState::Running; }
  bool init_(Trace &, const ZutcpLogParams &, Zutcp::Vantage::T);
  void start_();
  void stop_();
  void stopIdle_(CtrlFn);
  void wake();
  template <typename L>
  bool spawn(L &&l) {
    Guard guard(m_lock);
    // A completion callback may start the successor before this thread exits.
    auto previous = ZuMv(m_thread);
    m_thread = ZmThread{
      [l = ZuFwd<L>(l), previous]() mutable {
	l();
	if (previous) previous.join();
      }, ZmThreadParams().name("zutcp-log").priority(ZmThreadPriority::Low)};
    if (!m_thread) {
      m_thread = ZuMv(previous);
      return false;
    }
    return true;
  }
  template <typename L>
  void close_(Trace &trace, L &&l) {
    Guard guard(m_lock);
    if (!trace.configured) { guard.unlock(); l(); return; }
    if (enabled_()) {
      auto fn_ = [trace = &trace, l = ZuFwd<L>(l)](ZutcpLogger *this_) mutable {
	this_->closeTrace(*trace);
	l();
      };
      Fn fn{fn_};
      // The writer can close the last trace and stop itself as soon as this
      // record is visible. Keep its ring mapped through push2's bookkeeping.
      log__(fn);
    } else {
      guard.unlock();
      closeTrace(trace);
      l();
    }
  }
  void final_(Trace &);
  ZutcpLogDiag diag_() const;
  void cxnStarted_(Trace &, ZutcpLog_::CxnStartedEvt);
  void pktSent_(Trace &, ZutcpLog_::PktEvt);
  void pktRecv_(Trace &, ZutcpLog_::PktEvt);
  void pktDrop_(Trace &, ZutcpLog_::PktEvt);
  void pktsAcked_(Trace &, ZutcpLog_::AckEvt);
  void pktLost_(Trace &, ZutcpLog_::RecLogEvt);
  void markRetrans_(Trace &, ZutcpLog_::RecLogEvt);
  void metricsUpd_(Trace &, ZutcpLog_::RecLogEvt);
  void lossTimerUpd_(Trace &, ZutcpLog_::RecLogEvt);
  void congStateUpd_(Trace &, ZutcpLog_::RecLogEvt);
  void pathUpdated_(Trace &, ZutcpLog_::PathEvt);
  void pmtudUpdated_(Trace &, ZutcpLog_::PathEvt);
  void cxnClosed_(Trace &, ZutcpLog_::CloseEvt);
  void cxnStateUpd_(Trace &, ZutcpLog_::CxnStateEvt);
  void log__(Fn &fn) {
    if (tryPush(fn)) {
      ++m_recordsEnqueued;
    } else {
      ++m_ringBackPressure;
      m_queue.push(ZuMv(fn));
      ++m_queueCount;
      ++m_recordsEnqueued;
    }
  }
  template <typename L>
  void log_(Trace &trace, L &&l) {
    if (!enabled_()) return;
    ZuTime time = Zm::now();
    auto fn_ = [trace = &trace, l = ZuFwd<L>(l), time](ZutcpLogger *this_) mutable {
      if (!this_->useTrace(trace)) return;
      l(*this_, time);
    };
    Fn fn{fn_};
    log__(fn);
  }

  void work();

  bool tryPush(Fn &);
  bool useTrace(Trace *);
  void closeTrace(Trace &);
  bool writeTrace(Trace *, ZuCSpan);
  bool writeHeader(Trace *);
  bool writeEvent(ZuCSpan, const ZutcpLog_::CxnStartedEvt &, ZuTime);
  bool writeEvent(ZuCSpan, const ZutcpLog_::PktEvt &, ZuTime);
  bool writeEvent(ZuCSpan, const ZutcpLog_::AckEvt &, ZuTime);
  bool writeEvent(ZuCSpan, const ZutcpLog_::RecLogEvt &, ZuTime);
  bool writeEvent(ZuCSpan, const ZutcpLog_::PathEvt &, ZuTime);
  bool writeEvent(ZuCSpan, const ZutcpLog_::CloseEvt &, ZuTime);
  bool writeEvent(ZuCSpan, const ZutcpLog_::CxnStateEvt &, ZuTime);

private:
  unsigned		m_configured = 0;
  ZutcpLogParams	m_params;
  ZmThread		m_thread;
  Ring			m_ring;
  Queue			m_queue;
  ZmAtomic<unsigned>	m_queueCount = 0;
  Trace			*m_activeTrace = nullptr;
  ZeLogBuf		m_buf;
  ZmAtomic<uint64_t>	m_recordsEnqueued = 0;
  ZmAtomic<uint64_t>	m_recordsWritten = 0;
  ZmAtomic<uint64_t>	m_recordsDropped = 0;
  ZmAtomic<uint64_t>	m_ringBackPressure = 0;
  ZmAtomic<uint64_t>	m_writerFailures = 0;
  ZmAtomic<uint64_t>	m_bytesWritten = 0;
  mutable Lock		m_lock;
};

#else /* Zutcp_DEBUG */

struct ZutcpLogger {
  using CtrlFn = ZmFn<void(bool), ZmFnHeapID<"ZmEngine.CtrlFn">>;

  struct Trace { };
  static constexpr bool enabled() { return false; }
  static bool init(
    Trace &, const ZutcpLogParams &, Zutcp::Vantage::T) {
    return true;
  }
  static bool start() { return true; }
  static void start(CtrlFn fn) { fn(true); }
  static bool stop() { return true; }
  static void stop(CtrlFn fn) { fn(true); }
  static void stopIdle(CtrlFn fn) { fn(true); }
  template <typename L>
  static void close(Trace &, L &&l) { l(); }
  static void final(Trace &) { }
  static ZutcpLogDiag diag() { return {}; }
  static void cxnStarted(Trace &, ZutcpLog_::CxnStartedEvt) { }
  static void pktSent(Trace &, ZutcpLog_::PktEvt) { }
  static void pktRecv(Trace &, ZutcpLog_::PktEvt) { }
  static void pktDrop(Trace &, ZutcpLog_::PktEvt) { }
  static void pktsAcked(Trace &, ZutcpLog_::AckEvt) { }
  static void pktLost(Trace &, ZutcpLog_::RecLogEvt) { }
  static void markRetrans(Trace &, ZutcpLog_::RecLogEvt) { }
  static void metricsUpd(Trace &, ZutcpLog_::RecLogEvt) { }
  static void lossTimerUpd(Trace &, ZutcpLog_::RecLogEvt) { }
  static void congStateUpd(Trace &, ZutcpLog_::RecLogEvt) { }
  static void pathUpdated(Trace &, ZutcpLog_::PathEvt) { }
  static void pmtudUpdated(Trace &, ZutcpLog_::PathEvt) { }
  static void cxnClosed(Trace &, ZutcpLog_::CloseEvt) { }
  static void cxnStateUpd(Trace &, ZutcpLog_::CxnStateEvt) { }
  template <typename L>
  static void log(Trace &, L &&) { }
};

#endif /* Zutcp_DEBUG */

template <typename L>
inline void ZutcpLog(ZutcpLogger::Trace &trace, L &&l) {
  ZutcpLogger::log(trace, ZuFwd<L>(l));
}

#ifdef Zutcp_DEBUG
#define ZutcpLOG(trace, msg) \
  do { \
    if (ZutcpLogger::enabled()) { \
      using namespace ZutcpLog_; \
      ZutcpLog(trace, msg); \
    } \
  } while (0)
#else
#define ZutcpLOG(trace, msg) do { } while (0)
#endif
