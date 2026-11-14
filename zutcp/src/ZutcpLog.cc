//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Asynchronous TCP JSON sequence logging

#include <zlib/ZmSingleton.hh>


#include <zlib/Zutcp.hh>

#ifdef Zutcp_DEBUG

namespace ZutcpLog_ {
ZtEnumImplStruct(Reason);
ZtEnumImplStruct(RecKind);
ZtEnumImplStruct(RecReason);
ZtEnumImplStruct(PathKind);
ZtEnumImplStruct(PathAction);
ZtEnumImplStruct(PathReason);
ZtEnumImplStruct(CloseInitiator);
ZtEnumImplStruct(CloseTrigger);
ZtEnumImplStruct(CloseReason);

} // namespace ZutcpLog_

using namespace ZutcpLog_;

bool ZutcpLogSink::init(ZuCSpan path, unsigned age)
{
  final();
  if (path)
    m_path = path;
  else
    m_path = "zutcp.sqlog";
  ZiFile::age(m_path, age);
  m_file.open(m_path, ZiFile::Write | ZiFile::GC);
  return !!m_file;
}

void ZutcpLogSink::final()
{
  if (m_file) m_file.close();
  m_path = {};
}

bool ZutcpLogSink::write(ZuCSpan s)
{
  return m_file && m_file.write(s.data(), s.length()) == Zi::OK;
}

ZutcpLogger::ZutcpLogger()
{
}

ZutcpLogger::~ZutcpLogger()
{
  // Do not use ZmSelf(): thread-local storage may already be in teardown.
  if (m_thread && m_thread.tid() != Zm::getTID()) {
    Engine::stop();
    m_thread.join();
  }
  if (!m_ring.closed()) m_ring.close();
}

ZutcpLogger *ZutcpLogger::instance()
{
  static constexpr auto ctor = []() { return new ZutcpLogger(); };
  return
    ZmSingleton<ZutcpLogger,
      ZmSingletonCtor<ctor,
	ZmSingletonCleanup<ZmCleanup::Library>>>::instance();
}

bool ZutcpLogger::init_(
  Trace &trace, const ZutcpLogParams &params, Zutcp::Vantage::T vantage)
{
  Guard guard(m_lock);
  if (!params.enabled()) return true;
  if (!m_configured) {
    m_params = params;
    m_recordsEnqueued.store_(0);
    m_recordsWritten.store_(0);
    m_recordsDropped.store_(0);
    m_ringBackPressure.store_(0);
    m_writerFailures.store_(0);
    m_bytesWritten.store_(0);
  }
  if (!trace.configured) ++m_configured;
  trace.sink.final();
  trace.configured = true;
  trace.sinkOpened = false;
  trace.headerWritten = false;
  trace.vantage = vantage;
  trace.params = params;
  return true;
}

void ZutcpLogger::start_()
{
  bool ok = false;
  {
    Guard guard(m_lock);
    if (m_configured) {
      m_ring.init(ZmRingParams{m_params.ringSize()});
      ok = m_ring.open(Ring::Read | Ring::Write) == Zu::OK;
      if (!ok) ++m_writerFailures;
    }
  }
  started(ok);
  if (ok) {
    work();
    Engine::stopped();
  }
}

void ZutcpLogger::wake()
{
  Guard guard(m_lock);
  m_ring.eof(true);
}

void ZutcpLogger::stop_()
{
  {
    Guard guard(m_lock);
    m_ring.close();
    m_activeTrace = nullptr;
  }
  Engine::stopped(true);
}

void ZutcpLogger::stopIdle_(CtrlFn fn)
{
  {
    Guard guard(m_lock);
    if (m_configured) {
      guard.unlock();
      fn(true);
      return;
    }
  }
  Engine::stop(ZuMv(fn));
}

void ZutcpLogger::final_(Trace &trace)
{
  closeTrace(trace);
}

ZutcpLogDiag ZutcpLogger::diag_() const
{
  return {
    m_recordsEnqueued.load_(),
    m_recordsWritten.load_(),
    m_recordsDropped.load_(),
    m_ringBackPressure.load_(),
    m_writerFailures.load_(),
    m_bytesWritten.load_()
  };
}

void ZutcpLogger::cxnStarted_(Trace &trace, CxnStartedEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZutcpLogger *this_) mutable {
    if (!this_->useTrace(trace)) return;
    this_->writeEvent("connection_started", event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZutcpLogger::pktSent_(Trace &trace, PktEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZutcpLogger *this_) mutable {
    if (!this_->useTrace(trace)) return;
    this_->writeEvent("segment_sent", event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZutcpLogger::pktRecv_(Trace &trace, PktEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZutcpLogger *this_) mutable {
    if (!this_->useTrace(trace)) return;
    this_->writeEvent("segment_received", event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZutcpLogger::pktDrop_(Trace &trace, PktEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZutcpLogger *this_) mutable {
    if (!this_->useTrace(trace)) return;
    this_->writeEvent("segment_dropped", event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZutcpLogger::pktsAcked_(Trace &trace, AckEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZutcpLogger *this_) mutable {
    if (!this_->useTrace(trace)) return;
    this_->writeEvent("bytes_acked", event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZutcpLogger::pktLost_(Trace &trace, RecLogEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZutcpLogger *this_) mutable {
    if (!this_->useTrace(trace)) return;
    this_->writeEvent("transmission_lost", event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZutcpLogger::markRetrans_(Trace &trace, RecLogEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZutcpLogger *this_) mutable {
    if (!this_->useTrace(trace)) return;
    this_->writeEvent("retransmit", event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZutcpLogger::metricsUpd_(Trace &trace, RecLogEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZutcpLogger *this_) mutable {
    if (!this_->useTrace(trace)) return;
    this_->writeEvent("recovery_metrics", event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZutcpLogger::lossTimerUpd_(Trace &trace, RecLogEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZutcpLogger *this_) mutable {
    if (!this_->useTrace(trace)) return;
    this_->writeEvent("timer_updated", event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZutcpLogger::congStateUpd_(Trace &trace, RecLogEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZutcpLogger *this_) mutable {
    if (!this_->useTrace(trace)) return;
    this_->writeEvent("congestion_updated", event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZutcpLogger::pathUpdated_(Trace &trace, PathEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZutcpLogger *this_) mutable {
    if (!this_->useTrace(trace)) return;
    this_->writeEvent("path_updated", event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZutcpLogger::pmtudUpdated_(Trace &trace, PathEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZutcpLogger *this_) mutable {
    if (!this_->useTrace(trace)) return;
    this_->writeEvent("mtu_updated", event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZutcpLogger::cxnClosed_(Trace &trace, CloseEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZutcpLogger *this_) mutable {
    if (!this_->useTrace(trace)) return;
    this_->writeEvent("connection_closed", event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZutcpLogger::cxnStateUpd_(Trace &trace, CxnStateEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZutcpLogger *this_) mutable {
    if (!this_->useTrace(trace)) return;
    this_->writeEvent("connection_state", event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZutcpLogger::work()
{
  bool draining = false;
  for (;;) {
    if (ZuLikely(!m_queueCount.load_())) goto shift;
    if (Fn fn = m_queue.shift()) {
      if (ZuLikely(tryPush(fn)))
	--m_queueCount;
      else
	m_queue.unshift(ZuMv(fn));
    }
shift:
    if (draining && !m_queueCount.load_()) m_ring.eof(true);
    if (void *ptr = m_ring.shift()) {
      m_activeTrace = nullptr;
      m_ring.shift2(Fn::invoke(ptr, this));
    } else {
      if (m_ring.readStatus() == Zu::EndOfFile) {
	if (!m_queueCount.load_()) break;
	// Reopen admission only for the writer's overflow drain.
	draining = true;
	m_ring.eof(false);
      }
    }
  }
  m_activeTrace = nullptr;
}

bool ZutcpLogger::tryPush(Fn &fn)
{
  unsigned size = fn.pushSize();
  if (void *ptr = m_ring.tryPush(size)) {
    fn.push(ptr);
    m_ring.push2(ptr, size);
    return true;
  }
  return false;
}

bool ZutcpLogger::useTrace(Trace *trace)
{
  if (!trace || !trace->configured) return false;
  if (!trace->sinkOpened) {
    if (!trace->sink.init(trace->params.path(), trace->params.age())) {
      ++m_writerFailures;
      return false;
    }
    trace->sinkOpened = true;
  }
  m_activeTrace = trace;
  return true;
}

void ZutcpLogger::closeTrace(Trace &trace)
{
  if (trace.sinkOpened)
    trace.sink.final();
  Guard guard(m_lock);
  if (trace.configured && m_configured) --m_configured;
  trace.configured = false;
  trace.sinkOpened = false;
  trace.headerWritten = false;
  trace.vantage = Zutcp::Vantage::Unknown;
  trace.params = {};
  if (!m_configured) {
    m_params = {};
  }
}

bool ZutcpLogger::writeTrace(Trace *trace, ZuCSpan data)
{
  if (!useTrace(trace)) return false;
  if (!writeHeader(trace)) return false;
  if (!trace->sink.write(data)) {
    ++m_writerFailures;
    return false;
  }
  return true;
}

bool ZutcpLogger::writeHeader(Trace *trace)
{
  if (!trace || trace->headerWritten) return true;
  constexpr auto header = "\x1e{\"format\":\"zutcp-tcp-json-seq\",\"version\":1}\n"_z;
  if (!trace->sink.write(header)) { ++m_writerFailures; return false; }
  trace->headerWritten = true;
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + header.length());
  return true;
}

// Event values contain scalars and copied endpoints, never backing or live owners.
static void tcpLogStart(ZeLogBuf &buf, ZuCSpan name, ZuTime time, const LinkInfo &info)
{
  buf << char(0x1e) << "{\"event\":\"" << name << "\",\"time_us\":" <<
    time.microsecs() << ",\"local_ip\":\"" << info.local.ip() <<
    "\",\"local_port\":" << info.local.port() << ",\"remote_ip\":\"" << info.remote.ip() <<
    "\",\"remote_port\":" << info.remote.port() << ",\"vantage\":" << int(info.vantage);
}

bool ZutcpLogger::writeEvent(ZuCSpan name, const PktEvt &event, ZuTime time)
{
  ZeLogBuf buf;
  tcpLogStart(buf, name, time, event.linkInfo);
  buf << ",\"seq\":" << event.header.seq << ",\"ack\":" << event.header.ack <<
    ",\"flags\":" << unsigned(event.header.flags) << ",\"window\":" << event.header.window <<
    ",\"payload_bytes\":" << event.payloadSize << ",\"packet_bytes\":" << event.packetSize <<
    ",\"bytes_in_flight\":" << event.bytesInFlight << ",\"ecn\":" << int(event.ecn) <<
    ",\"reason\":" << int(event.reason);
  const auto &options = event.header.options;
  buf << ",\"options_flags\":" << unsigned(options.flags) <<
    ",\"mss\":" << options.mss << ",\"scale\":" << unsigned(options.scale) <<
    ",\"tsval\":" << options.tsval << ",\"tsecr\":" << options.tsecr <<
    ",\"options_bytes\":" << options.encodedLength(event.header.flags & 0x02) <<
    ",\"tfo_cookie_bytes\":" << unsigned(options.tfoLength) << ",\"sack\":[";
  for (unsigned i = 0; i < options.sackCount && i < TCPOptions::MaxSACKBlocks; ++i) {
    if (i) buf << ',';
    buf << '[' << options.sack[i].left << ',' << options.sack[i].right << ']';
  }
  buf << ']';
  buf << "}\n";
  if (!writeTrace(m_activeTrace, buf.cspan())) return false;
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + buf.length());
  return true;
}

bool ZutcpLogger::writeEvent(ZuCSpan name, const AckEvt &event, ZuTime time)
{
  ZeLogBuf buf;
  tcpLogStart(buf, name, time, event.linkInfo);
  buf << ",\"byte_next\":" << event.byteNext << ",\"acked_bytes\":" << event.ackedBytes <<
    ",\"lost_bytes\":" << event.lostBytes;
  buf << "}\n";
  if (!writeTrace(m_activeTrace, buf.cspan())) return false;
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + buf.length());
  return true;
}

bool ZutcpLogger::writeEvent(ZuCSpan name, const RecLogEvt &event, ZuTime time)
{
  ZeLogBuf buf;
  tcpLogStart(buf, name, time, event.linkInfo);
  buf << ",\"local_transmission_id\":" << event.transmissionID << ",\"bytes\":" << event.bytes <<
    ",\"deadline_us\":" << event.deadlineUS << ",\"latest_rtt_us\":" << event.latestRTTUS <<
    ",\"smoothed_rtt_us\":" << event.smoothedRTTUS <<
    ",\"rtt_variance_us\":" << event.rttVarianceUS << ",\"min_rtt_us\":" << event.minRTTUS <<
    ",\"cwnd\":" << event.cwnd << ",\"ssthresh\":" << event.ssthresh <<
    ",\"bytes_in_flight\":" << event.bytesInFlight << ",\"kind\":" << int(event.kind) <<
    ",\"reason\":" << int(event.reason);
  buf << "}\n";
  if (!writeTrace(m_activeTrace, buf.cspan())) return false;
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + buf.length());
  return true;
}

bool ZutcpLogger::writeEvent(ZuCSpan name, const PathEvt &event, ZuTime time)
{
  ZeLogBuf buf;
  tcpLogStart(buf, name, time, event.linkInfo);
  buf << ",\"bytes\":" << event.bytes << ",\"mtu\":" << event.mtu <<
    ",\"kind\":" << int(event.kind) << ",\"action\":" << int(event.action) <<
    ",\"reason\":" << int(event.reason) << ",\"validated\":" << unsigned(event.validated);
  buf << "}\n";
  if (!writeTrace(m_activeTrace, buf.cspan())) return false;
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + buf.length());
  return true;
}

bool ZutcpLogger::writeEvent(ZuCSpan name, const CloseEvt &event, ZuTime time)
{
  ZeLogBuf buf;
  tcpLogStart(buf, name, time, event.linkInfo);
  buf << ",\"local_error_code\":" << event.errorCode <<
    ",\"initiator\":" << int(event.initiator) <<
    ",\"trigger\":" << int(event.trigger) << ",\"reason\":" << int(event.reason);
  buf << "}\n";
  if (!writeTrace(m_activeTrace, buf.cspan())) return false;
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + buf.length());
  return true;
}

bool ZutcpLogger::writeEvent(ZuCSpan name, const CxnStateEvt &event, ZuTime time)
{
  ZeLogBuf buf;
  tcpLogStart(buf, name, time, event.linkInfo);
  buf << ",\"old_state\":" << int(event.oldState) << ",\"new_state\":" << int(event.newState);
  buf << "}\n";
  if (!writeTrace(m_activeTrace, buf.cspan())) return false;
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + buf.length());
  return true;
}

bool ZutcpLogger::writeEvent(ZuCSpan name, const CxnStartedEvt &event, ZuTime time)
{
  ZeLogBuf buf;
  tcpLogStart(buf, name, time, event.linkInfo);
  buf << ",\"local_endpoint_ip\":\"" << event.local.ip() <<
    "\",\"local_endpoint_port\":" << event.local.port() <<
    ",\"remote_endpoint_ip\":\"" << event.remote.ip() <<
    "\",\"remote_endpoint_port\":" << event.remote.port();
  buf << "}\n";
  if (!writeTrace(m_activeTrace, buf.cspan())) return false;
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + buf.length());
  return true;
}

#endif // Zutcp_DEBUG
