//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// singleton logger

// ZeLog::init("program", "daemon");	// for LOG_DAEMON
// ZeLog::sink(ZeLog::sysSink());	// sysSink() is syslog / event log
// ZeLOG(Debug, "debug message");	// ZeLOG() is macro
// ZeLOG(Error, ZeLastError);		// errno / GetLastError()
// ZeLOG(Error,
//      ZtSprintf("fopen(%s) failed: %s", file, ZeLastError.message().data()));
// try { ... } catch (ZeError &e) { ZeLOG(Error, e); }

// if no sink is registered at initialization, the default sink is stderr
// on Unix and the Application event log on Windows

#ifndef ZeLog_HH
#define ZeLog_HH

#ifndef ZeLib_HH
#include <zlib/ZeLib.hh>
#endif

#include <stdio.h>

#include <zlib/ZuDerive.hh>
#include <zlib/ZuCmp.hh>
#include <zlib/ZuSpan.hh>

#include <zlib/ZmBackTrace.hh>
#include <zlib/ZmCleanup.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZuTime.hh>
#include <zlib/ZmRing.hh>
#include <zlib/ZmRingFn.hh>

#include <zlib/ZePlatform.hh>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4251 4231 4660)
#endif

class ZeLog;

namespace ZeSinkType {
  ZtEnum(ZeSinkType, int8_t, File, Debug, System, Lambda);
}
struct ZeSink : public ZmPolymorph {
  int	type;	// ZeSinkType

  ZeSink(int type_) : type(type_) { }

  virtual void pre(ZeLogBuf &, const ZeEventInfo &) = 0;
  virtual void post(ZeLogBuf &, const ZeEventInfo &) = 0;
  virtual void age() = 0;
};

struct ZeSinkOptions {
  ZeSinkOptions &path(ZuCSpan path) { m_path = path; return *this; }
  ZeSinkOptions &age(unsigned age) { m_age = age; return *this; }
  ZeSinkOptions &tzOffset(unsigned tzOffset)
    { m_tzOffset = tzOffset; return *this; }

  const auto &path() const { return m_path; }
  auto age() const { return m_age; }
  auto tzOffset() const { return m_tzOffset; }

private:
  ZuCSpan	m_path;
  unsigned	m_age = 8;
  int		m_tzOffset = 0;
};

// general purpose on-heap string for file names
// - distinct from Zi::Path in two respects:
//   - regular char string (Zi::Path is UCS2 on Windows)
//   - Ze is a dependency of Zi, and cannot depend on it
//     (it uses stdio for that reason)
ZuDerive(ZeFileName, ZtString<ZtStringHeapID<"ZeFileName">>);

class ZeAPI ZeFileSink : public ZeSink {
  using Lock = ZmPLock;
  using Guard = ZmGuard<Lock>;

public:
  ZeFileSink() :
      ZeSink{ZeSinkType::File} { init(); }
  ZeFileSink(const ZeSinkOptions &options) :
      ZeSink{ZeSinkType::File}, m_path{options.path()},
      m_age{options.age()}, m_dateFmt{options.tzOffset()} { init(); }

  ~ZeFileSink();

  void pre(ZeLogBuf &, const ZeEventInfo &);
  void post(ZeLogBuf &, const ZeEventInfo &);
  void age();

private:
  void init();
  void age_();

  ZeFileName		m_path;
  unsigned		m_age = 8;
  ZuDateTimeFmt::CSV	m_dateFmt;

  Lock			m_lock;
    FILE *		  m_file = nullptr;
};

class ZeAPI ZeDebugSink : public ZeSink {
  using Lock = ZmPLock;
  using Guard = ZmGuard<Lock>;

public:
  ZeDebugSink() : ZeSink{ZeSinkType::Debug},
    m_started{Zm::now()} { init(); }
  ZeDebugSink(const ZeSinkOptions &options) :
      ZeSink{ZeSinkType::Debug},
      m_path{options.path()}, m_started{Zm::now()} { init(); }

  ~ZeDebugSink();

  void pre(ZeLogBuf &, const ZeEventInfo &);
  void post(ZeLogBuf &, const ZeEventInfo &);
  void age() { } // unused

private:
  void init();

  ZeString	m_path;
  FILE *	m_file = nullptr;
  ZuTime	m_started;
};

struct ZeAPI ZeSysSink : public ZeSink {
  ZeSysSink() : ZeSink{ZeSinkType::System} { }

  void pre(ZeLogBuf &, const ZeEventInfo &);
  void post(ZeLogBuf &, const ZeEventInfo &);
  void age() { } // unused
};

struct ZeAPI ZeLambdaSink_ : public ZeSink {
  ZeLambdaSink_(int tzOffset = 0) :
      ZeSink{ZeSinkType::Lambda}, m_dateFmt{tzOffset} { }

  void pre(ZeLogBuf &, const ZeEventInfo &);

private:
  ZuDateTimeFmt::CSV	m_dateFmt;
};
template <typename L>
struct ZeLambdaSink : public ZeLambdaSink_ {
  L	l;

  ZeLambdaSink(L l_, int tzOffset = 0) :
      ZeLambdaSink_{tzOffset}, l{ZuMv(l_)} { }

  void post(ZeLogBuf &buf, const ZeEventInfo &info) { l(buf, info); }
  void age() { } // unused
};

class ZeAPI ZeLog {
  using Lock = ZmPLock;
  using Guard = ZmGuard<Lock>;

  ZuDerive(Ring, (ZmRing<ZmRingMW<true>>));
  using Fn = ZmRingFn<ZeLog *>;

  ZeLog();

public:
  ~ZeLog();

  ZeLog(const ZeLog &) = delete;
  ZeLog &operator =(const ZeLog &) = delete;

  static ZeLog *instance();

  template <typename ...Args>
  static ZmRef<ZeSink> fileSink(Args &&...args) {
    return new ZeFileSink(ZuFwd<Args>(args)...);
  }
  template <typename ...Args>
  static ZmRef<ZeSink> debugSink(Args &&...args) {
    return new ZeDebugSink(ZuFwd<Args>(args)...);
  }
  template <typename ...Args>
  static ZmRef<ZeSink> sysSink(Args &&...args) {
    return new ZeSysSink(ZuFwd<Args>(args)...);
  }
  template <typename L>
  static ZmRef<ZeSink> lambdaSink(L &&l) {
    return new ZeLambdaSink<L>(ZuFwd<L>(l));
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

  static void bufSize(unsigned n) {
    instance()->bufSize_(n);
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
    auto fn_ = [e = ZuMv(e)](ZeLog *this_) mutable {
      auto sink = this_->sink_();
      auto &buf = this_->m_buf;
      buf.null();
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

  void bufSize_(unsigned n) { m_bufSize = n; }

  ZuCSpan program_() const { return m_program; }
  ZuCSpan facility_() const { return m_facility; }

  int level_() const { return m_level; }
  void level_(int l) { m_level = l; }

  ZmRef<ZeSink> sink_();
  void sink_(ZmRef<ZeSink> sink);

  void start_();
  void start__();
  void stop_();
  void forked_();

  void work_();

  void log__(Fn &fn);

  void age_();

private:
  ZeString		m_program;
  ZeString		m_facility;
  int			m_level;
  unsigned		m_bufSize = (1<<20);	// 1Mbyte

  ZmThread		m_thread;
  Ring			m_ring;

  Lock			m_lock;
    ZmRef<ZeSink>	  m_sink;

  // thread-specific
  ZeLogBuf		m_buf;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif

// alias for ZeLog::log()
template <typename L>
inline void ZeLogEvent(ZeEvent<L> e) {
  ZeLog::instance()->log_(ZuMv(e));
}

template <typename L>
inline decltype(
    ZuDeclVal<L &>()(ZuDeclVal<ZeLogBuf &>()),
    void())
ZeBackTrace(ZeEvent<L> event_) {
  ZmBackTrace bt{1};
  ZeLogEvent(ZeEvent(
      event_.severity, event_.file, event_.line, event_.function,
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
ZeBackTrace(ZeEvent<L> event_) {
  ZmBackTrace bt{1};
  ZeLogEvent(ZeEvent(
      event_.severity, event_.file, event_.line, event_.function,
      [bt = ZuMv(bt), l = ZuMv(event_).l](auto &s, const auto &info) mutable {
	l(s, info);
	s << '\n' << ZuMv(bt);
      }));
}

#ifndef ZDEBUG

// filter out DEBUG messages in production builds
#define ZeLOG_(sev, msg) \
  ((sev > Ze::Debug) ? ZeLogEvent(ZeEVENT_(sev, msg)) : void())
#define ZeLOGBT_(sev, msg) \
  ((sev > Ze::Debug) ? ZeBackTrace(ZeEVENT_(sev, msg)) : void())

#else /* !ZEBUG */

#define ZeLOG_(sev, msg) ZeLogEvent(ZeEVENT_(sev, msg))
#define ZeLOGBT_(sev, msg) ZeBackTrace(ZeEVENT_(sev, msg))

#endif /* !ZEBUG */

#define ZeLOG(sev, msg) ZeLOG_(Ze:: sev, msg)
#define ZeLOGBT(sev, msg) ZeLOGBT_(Ze:: sev, msg)

#endif /* ZeLog_HH */
