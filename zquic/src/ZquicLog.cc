//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC qlog

#include <zlib/ZmSingleton.hh>

#include <zlib/ZtJSON.hh>

#include <zlib/ZquicLog.hh>

#ifdef Zquic_DEBUG

struct QLogTrace {
  ZuCSpan	vantagePoint;
};

ZtStruct((QLogTrace, JSON),
  (((vantagePoint), (JSON::ID<"vantage_point">)), (String)));

struct QLogHeader {
  ZuCSpan	qlogVersion;
  ZuCSpan	qlogFormat;
  ZuCSpan	title;
  QLogTrace	trace;
};

ZtStruct((QLogHeader, JSON),
  (((qlogVersion), (JSON::ID<"qlog_version">)), (String)),
  (((qlogFormat), (JSON::ID<"qlog_format">)), (String)),
  (((title)), (String)),
  (((trace)), (UDT)));

struct QLogEventData {
  ZuCSpan	category;
  ZuCSpan	cid;
  uint64_t	value = 0;
  ZuCSpan	detail;
};

ZtStruct((QLogEventData, JSON),
  (((category)), (String)),
  (((cid)), (String)),
  (((value)), (UInt64)),
  (((detail)), (String)));

struct QLogEvent {
  uint64_t	time = 0;
  ZuCSpan	name;
  QLogEventData	data;
};

ZtStruct((QLogEvent, JSON),
  (((time)), (UInt64)),
  (((name)), (String)),
  (((data)), (UDT)));

bool ZquicLogSink::init(ZuCSpan path)
{
  final();
  if (path)
    m_path = path;
  else
    m_path = "zquic.sqlog";
  m_file.open(m_path, ZiFile::Write | ZiFile::GC);
  return !!m_file;
}

void ZquicLogSink::final()
{
  if (m_file) m_file.close();
  m_path.length(0);
}

bool ZquicLogSink::write(ZuCSpan s)
{
  return m_file && m_file.write(s.data(), s.length()) == Zi::OK;
}

ZquicLog::ZquicLog()
{
}

ZquicLog::~ZquicLog()
{
  stop_();
  final_();
}

ZquicLog *ZquicLog::instance()
{
  static constexpr auto ctor = []() { return new ZquicLog(); };
  return
    ZmSingleton<ZquicLog,
      ZmSingletonCtor<ctor,
	ZmSingletonCleanup<ZmCleanup::Library>>>::instance();
}

bool ZquicLog::init_(const ZquicLogParams &params)
{
  Guard guard(m_lock);
  if (m_started) return false;
  m_params = params;
  m_configured = params.enabled();
  m_enabled.store_(false);
  m_recordsEnqueued.store_(0);
  m_recordsWritten.store_(0);
  m_recordsDropped.store_(0);
  m_ringBackPressure.store_(0);
  m_writerFailures.store_(0);
  m_bytesWritten.store_(0);
  return true;
}

void ZquicLog::start_()
{
  Guard guard(m_lock);
  if (!m_configured || m_started) return;
  m_ring.init(ZmRingParams{m_params.ringSize()});
  if (m_ring.open(Ring::Read | Ring::Write) != Zu::OK) {
    ++m_writerFailures;
    return;
  }
  m_enabled.store_(true);
  ZmThreadParams threadParams;
  threadParams.name(m_params.thread() ? m_params.thread() : ZuCSpan{"zquic-qlog"});
  threadParams.priority(ZmThreadPriority::Low);
  m_thread = ZmThread{[this]() { work_(); }, threadParams};
  m_started = true;
}

void ZquicLog::stop_()
{
  ZmThread thread;
  {
    Guard guard(m_lock);
    m_enabled.store_(false);
    if (!m_started) return;
    thread = m_thread;
    m_thread = {};
    m_started = false;
  }
  if (thread) {
    m_ring.eof(true);
    thread.join();
  }
  m_ring.close();
}

void ZquicLog::final_()
{
  Guard guard(m_lock);
  m_enabled.store_(false);
  m_configured = false;
  m_params = {};
}

ZquicLogDiag ZquicLog::diag_() const
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

void ZquicLog::event_(
  ZuCSpan name, ZuCSpan category, ZuCSpan cid, uint64_t value, ZuCSpan detail)
{
  if (!enabled_()) return;
  struct Event {
    ZuCArray<64>	name;
    ZuCArray<32>	category;
    ZuCArray<64>	cid;
    ZuCArray<128>	detail;
    uint64_t		value = 0;
    ZuTime		time;
  };
  Event event;
  event.name.length(0);
  event.name << name;
  event.category.length(0);
  event.category << category;
  event.cid.length(0);
  event.cid << cid;
  event.detail.length(0);
  event.detail << detail;
  event.value = value;
  event.time = Zm::now();
  auto fn_ = [event](ZquicLog *this_) mutable {
    this_->writeEvent_(
      event.name, event.category, event.cid, event.value, event.detail,
      event.time);
  };
  Fn fn{fn_};
  unsigned size = fn.pushSize();
  if (void *ptr = m_ring.tryPush(size)) {
    fn.push(ptr);
    m_ring.push2(ptr, size);
    ++m_recordsEnqueued;
  } else {
    ++m_recordsDropped;
    ++m_ringBackPressure;
  }
}

void ZquicLog::work_()
{
  if (!m_sink.init(m_params.path())) {
    ++m_writerFailures;
  } else {
    writeHeader_();
    for (;;) {
      if (void *ptr = m_ring.shift()) {
	m_ring.shift2(Fn::invoke(ptr, this));
      } else {
	if (m_ring.readStatus() == Zu::EndOfFile) break;
      }
    }
  }
  m_sink.final();
}

bool ZquicLog::writeHeader_()
{
  QLogHeader header{
    "0.3",
    "JSON-SEQ",
    "zquic",
    QLogTrace{"unknown"}
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, header);
  m_buf << '\n';
  if (!m_sink.write({m_buf.data(), m_buf.length()})) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLog::writeEvent_(
  ZuCSpan name, ZuCSpan category, ZuCSpan cid, uint64_t value,
  ZuCSpan detail, ZuTime time)
{
  QLogEvent event{
    uint64_t(time.sec()) * 1000000 + uint64_t(time.nsec() / 1000),
    name,
    QLogEventData{category, cid, value, detail}
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, event);
  m_buf << '\n';
  if (!m_sink.write({m_buf.data(), m_buf.length()})) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

#endif /* Zquic_DEBUG */
