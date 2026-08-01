//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// logger
// - singleton
// - fans out to multiple sinks

// ZiLog::init("program", "daemon");           // for LOG_DAEMON
// ZiLog::sink(ZiLog::sysSink());              // syslog / event log
// ZiLOG(Debug, "component", "debug message"); // ZiLOG() is macro
// ZiLOG(Error, "component", ZeLastError);     // errno / GetLastError()
// ZiLOG(Error, "component", ([file, e = ZeLastError](auto &s) {
//   s << "fopen(" << file << ") failed: " << e;
// }));
// try { ... } catch (ZeException &e) { ZiLogEvent(ZuMv(e)); }

// if no sink is registered at initialization, the default sink is:
// - Unix - stderr
// - Windows - Application event log

#ifndef ZiLog_HH
#define ZiLog_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <zlib/ZuDerive.hh>
#include <zlib/ZuCmp.hh>
#include <zlib/ZuSpan.hh>

#include <zlib/ZmBackTrace.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmCleanup.hh>
#include <zlib/ZmQueue.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZuTime.hh>
#include <zlib/ZmRing.hh>
#include <zlib/ZmRingFn.hh>

#include <zlib/ZfJSON.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZiFile.hh>
#include <zlib/ZiCSV.hh>

class ZiLog;

ZtEnumNS(ZiAPI, ZiSinkType, int8_t, File, Debug, CSV, System, Lambda);
struct ZiSink : public ZmPolymorph {
  int	type;	// ZiSinkType

  ZiSink(int type_) : type(type_) { }

  virtual void pre(ZeLogBuf &, const ZeEventInfo &) = 0;
  virtual void post(ZeLogBuf &, const ZeEventInfo &) = 0;
  virtual void age() = 0;
};

struct ZiSinkOptions {
  ZiSinkOptions &path(ZuCSpan path) { m_path = path; return *this; }
  ZiSinkOptions &age(unsigned age) { m_age = age; return *this; }
  ZiSinkOptions &tzOffset(unsigned tzOffset)
    { m_tzOffset = tzOffset; return *this; }

  const auto &path() const { return m_path; }
  auto age() const { return m_age; }
  auto tzOffset() const { return m_tzOffset; }

private:
  ZuCSpan	m_path;
  unsigned	m_age = 8;
  int		m_tzOffset = 0;
};

class ZeAPI ZiFileSink : public ZiSink {
  using Lock = ZmPLock;
  using Guard = ZmGuard<Lock>;

public:
  ZiFileSink() :
      ZiSink{ZiSinkType::File} { init(); }
  ZiFileSink(const ZiSinkOptions &options) :
      ZiSink{ZiSinkType::File}, m_path{options.path()},
      m_age{options.age()}, m_dateFmt{options.tzOffset()} { init(); }

  ~ZiFileSink();

  void pre(ZeLogBuf &, const ZeEventInfo &);
  void post(ZeLogBuf &, const ZeEventInfo &);
  void age();

private:
  void init();

  Zi::Path		m_path;
  unsigned		m_age = 8;
  ZuDateTimeFmt::CSV	m_dateFmt;
  ZiFile		m_file;
};

class ZeAPI ZiDebugSink : public ZiSink {
  using Lock = ZmPLock;
  using Guard = ZmGuard<Lock>;

public:
  ZiDebugSink() : ZiSink{ZiSinkType::Debug},
    m_started{Zm::now()} { init(); }
  ZiDebugSink(const ZiSinkOptions &options) :
      ZiSink{ZiSinkType::Debug},
      m_path{options.path()}, m_started{Zm::now()} { init(); }

  ~ZiDebugSink();

  void pre(ZeLogBuf &, const ZeEventInfo &);
  void post(ZeLogBuf &, const ZeEventInfo &);
  void age() { } // unused

private:
  void init();

  ZeString	m_path;
  ZiFile	m_file;
  ZuTime	m_started;
};

struct ZiSinkEvent {
  const ZeLogBuf	&msg;
  const ZeEventInfo	&info;
};

ZfStruct(ZiSinkEvent,
  (((time,      AliasRd, info.time)),      (Time)),
  (((tid,       AliasRd, info.tid)),       (UInt32)),
  (((severity,  AliasRd, info.severity)),  (Int8)),
  (((file,      AliasRd, info.file)),      (String)),
  (((line,      AliasRd, info.line)),      (Int32)),
  (((function,  AliasRd, info.function)),  (String)),
  (((component, AliasRd, info.component)), (String)),
  (((msg,       Rd)),                      (String)));

class ZeAPI ZiCSVSink : public ZiSink {
  using Lock = ZmPLock;
  using Guard = ZmGuard<Lock>;

public:
  ZiCSVSink() :
      ZiSink{ZiSinkType::CSV} { init(); }
  ZiCSVSink(const ZiSinkOptions &options) :
      ZiSink{ZiSinkType::CSV}, m_path{options.path()},
      m_age{options.age()} { init(); }

  ~ZiCSVSink();

  void pre(ZeLogBuf &, const ZeEventInfo &);
  void post(ZeLogBuf &, const ZeEventInfo &);
  void age();

private:
  static constexpr unsigned MaxRowLen = 4096;
  using Writer = ZiCSV::PushFile<ZiSinkEvent, ZuFacet::Core, MaxRowLen>;

  void init();

  Zi::Path			m_path;
  unsigned			m_age = 8;
  ZuUnion<void, Writer>		m_writer;
};

struct ZeAPI ZiSysSink : public ZiSink {
  ZiSysSink() : ZiSink{ZiSinkType::System} { }

  void pre(ZeLogBuf &, const ZeEventInfo &);
  void post(ZeLogBuf &, const ZeEventInfo &);
  void age() { } // unused
};

struct ZeAPI ZiLambdaSink_ : public ZiSink {
  ZiLambdaSink_(int tzOffset = 0) :
      ZiSink{ZiSinkType::Lambda}, m_dateFmt{tzOffset} { }

  void pre(ZeLogBuf &, const ZeEventInfo &);

private:
  ZuDateTimeFmt::CSV	m_dateFmt;
};

template <typename L>
struct ZiLambdaSink : public ZiLambdaSink_ {
  L	l;

  ZiLambdaSink(L l_, int tzOffset = 0) :
      ZiLambdaSink_{tzOffset}, l{ZuMv(l_)} { }

  void post(ZeLogBuf &buf, const ZeEventInfo &info) { l(buf, info); }
  void age() { } // unused
};

class ZeAPI ZiLog {
  using Lock = ZmPLock;
  using Guard = ZmGuard<Lock>;

  ZuDerive(Ring, (ZmRing<ZmRingMW<true>>));
  using Fn = ZmRingFn<ZiLog *>;
  ZuDerive(Queue_, (ZmQueue<Fn, ZmQueueHeapID<"ZiLog.Queue">>));
  struct Queue : public Queue_ {
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

    Lock	m_lock;
  };

  ZiLog();

public:
  ~ZiLog();

  ZiLog(const ZiLog &) = delete;
  ZiLog &operator =(const ZiLog &) = delete;

  static ZiLog *instance();

  template <typename ...Args>
  static ZmRef<ZiSink> fileSink(Args &&...args) {
    return new ZiFileSink(ZuFwd<Args>(args)...);
  }
  template <typename ...Args>
  static ZmRef<ZiSink> debugSink(Args &&...args) {
    return new ZiDebugSink(ZuFwd<Args>(args)...);
  }
  template <typename ...Args>
  static ZmRef<ZiSink> csvSink(Args &&...args) {
    return new ZiCSVSink(ZuFwd<Args>(args)...);
  }
  template <typename ...Args>
  static ZmRef<ZiSink> sysSink(Args &&...args) {
    return new ZiSysSink(ZuFwd<Args>(args)...);
  }
  template <typename L>
  static ZmRef<ZiSink> lambdaSink(L &&l) {
    return new ZiLambdaSink<L>(ZuFwd<L>(l));
  }

  static void init() {
    instance()->init_();
  }
  static void init(const char *program) {
    instance()->init_(program);
  }
  static void init(const char *program, const char *facility) {
    instance()->init_(program, facility);
  }

  static void ringBufSize(unsigned n) {
    instance()->ringBufSize_(n);
  }

  static ZuCSpan program() { return instance()->program_(); }

  static int level() { return instance()->level_(); }
  static void level(int l) { instance()->level_(l); }

  template <typename ...Args>
  static void sink(Args &&...args) {
    instance()->sink_(ZuFwd<Args>(args)...);
  }

  static void start() { instance()->start_(); }
  static void stop() { instance()->stop_(); }
  static void forked() { instance()->forked_(); }

  template <typename L>
  static void log(ZeEvent<L> e) {
    instance()->log_(ZuMv(e));
  }
  template <typename L>
  void log_(ZeEvent<L> e) {
    if (static_cast<int>(e.severity) < m_level) return;
    auto fn_ = [e = ZuMv(e)](ZiLog *this_) mutable {
      auto sink = this_->sink_();
      auto &buf = this_->m_buf;
      buf.length(0);
      sink->pre(buf, e);
      buf << e;
      sink->post(buf, e);
    };
    Fn fn{fn_};
    log__(fn);
  }
  static void age() { instance()->age_(); }

private:
  void init_();
  void init__();
  void init_(const char *program);
  void init_(const char *program, const char *facility);
  void init__(const char *program, const char *facility);

  void ringBufSize_(unsigned n) { m_ringBufSize = n; }

  ZuCSpan program_() const { return m_program; }
  ZuCSpan facility_() const { return m_facility; }

  int level_() const { return m_level; }
  void level_(int l) { m_level = l; }

  ZmRef<ZiSink> sink_();
  void sink_(ZmRef<ZiSink> sink);

  void start_();
  void start__();
  void stop_();
  void forked_();

  void work_();

  void log__(Fn &fn);
  bool tryPush_(Fn &fn);

  void age_();

private:
  ZeString		m_program;
  ZeString		m_facility;
  int			m_level;
  unsigned		m_ringBufSize = (1<<20);	// default 1Mb ring buffer

  ZmThread		m_thread;
  Ring			m_ring;
  Queue			m_queue;
  ZmAtomic<unsigned>	m_queueCount = 0;

  Lock			m_lock;
    ZmRef<ZiSink>	  m_sink;

  // thread-specific to worker thread
  ZeLogBuf		m_buf;
};

// alias for ZiLog::log()
template <typename L>
inline void ZiLogEvent(ZeEvent<L> e) {
  ZiLog::instance()->log_(ZuMv(e));
}

template <typename L>
inline decltype(
    ZuDeclVal<L &>()(ZuDeclVal<ZeLogBuf &>()),
    void())
ZiLogBT(ZeEvent<L> event_) {
  ZmBackTrace bt{1};
  ZiLogEvent(ZeEvent(
      event_.severity, event_.file, event_.line, event_.function, event_.component,
      [bt = ZuMv(bt), l = ZuMv(event_).l](auto &s) mutable {
	l(s);
	s << '\n' << ZuMv(bt);
      }));
}
template <typename L>
inline decltype(
    ZuDeclVal<L &>()(
      ZuDeclVal<ZeLogBuf &>(),
      ZuDeclVal<const ZeEventInfo &>()),
    void())
ZiLogBT(ZeEvent<L> event_) {
  ZmBackTrace bt{1};
  ZiLogEvent(ZeEvent(
      event_.severity, event_.file, event_.line, event_.function, event_.component,
      [bt = ZuMv(bt), l = ZuMv(event_).l](auto &s, const auto &info) mutable {
	l(s, info);
	s << '\n' << ZuMv(bt);
      }));
}

#ifndef ZDEBUG

// filter out DEBUG messages in production builds
#define ZiLOG_(sev, component, msg) \
  ((sev > Ze::Debug) ? ZiLogEvent(ZeEVENT_(sev, component, msg)) : void())
#define ZiLOGBT_(sev, component, msg) \
  ((sev > Ze::Debug) ? ZiLogBT(ZeEVENT_(sev, component, msg)) : void())

#else /* !ZEBUG */

#define ZiLOG_(sev, component, msg) ZiLogEvent(ZeEVENT_(sev, component, msg))
#define ZiLOGBT_(sev, component, msg) ZiLogBT(ZeEVENT_(sev, component, msg))

#endif /* !ZEBUG */

#define ZiLOG(sev, component, msg) ZiLOG_(Ze:: sev, component, msg)
#define ZiLOGBT(sev, component, msg) ZiLOGBT_(Ze:: sev, component, msg)

#endif /* ZiLog_HH */
