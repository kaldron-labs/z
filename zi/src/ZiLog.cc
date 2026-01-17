//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// singleton logger

#include <zlib/ZuBox.hh>
#include <zlib/ZuDateTime.hh>

#include <zlib/ZmPlatform.hh>
#include <zlib/ZmSingleton.hh>
#include <zlib/ZmTrap.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZtRegex.hh>
#include <zlib/ZtJSON.hh>

#include <zlib/ZiLog.hh>

ZiLog::ZiLog() : m_level{1}
{
  init_();
}

ZiLog::~ZiLog()
{
  if (!m_ring.closed()) {
    if (m_thread) {
      m_ring.eof(true);
      // protect against blocking on self-destruction, do NOT use ZmSelf()
      // - ZmSelf() depends on TLS and should not be called during exit
      if (m_thread.tid() != Zm::getTID()) m_thread.join();
    }
    m_ring.close();
  }
}

ZiLog *ZiLog::instance()
{
  static constexpr auto ctor = []() { return new ZiLog(); };
  return
    ZmSingleton<ZiLog,
      ZmSingletonCtor<ctor,
	ZmSingletonCleanup<ZmCleanup::Library>>>::instance();
}

#ifndef _WIN32

struct ZePlatform_Syslogger {
  using Lock = ZmLock;
  using Guard = ZmGuard<Lock>;

public:
  ZePlatform_Syslogger() { openlog("", 0, LOG_USER); }
  ~ZePlatform_Syslogger() { closelog(); }

  void init(const char *program, int facility = LOG_USER) {
    Guard guard(m_lock);

    closelog();
    openlog(program, 0, m_facility = facility);
  }

  int facility() { return m_facility; }

private:
  ZmLock	m_lock;
    int		  m_facility;
};

static ZePlatform_Syslogger *syslogger() {
  return
    ZmSingleton<ZePlatform_Syslogger,
      ZmSingletonCleanup<ZmCleanup::Platform>>::instance();
}

static int sysloglevel(int i) {
  static const int levels[] = {
    LOG_DEBUG,		// Debug
    LOG_INFO,		// Info
    LOG_WARNING,	// Warning
    LOG_ERR,		// Error
    LOG_CRIT		// Fatal
  };

  return (i < 0 || i > 4) ? LOG_ERR : levels[i];
}

#else /* !_WIN32 */

static int eventlogtype(int i) {
  static const int types[] = {
    EVENTLOG_SUCCESS,		// Debug
    EVENTLOG_INFORMATION_TYPE,	// Info
    EVENTLOG_WARNING_TYPE,	// Warning
    EVENTLOG_ERROR_TYPE,	// Error
    EVENTLOG_ERROR_TYPE		// Fatal
  };
  enum { N = sizeof(types) / sizeof(types[0]) };

  return (i < 0 || i >= N) ? EVENTLOG_WARNING_TYPE : types[i];
}

#endif /* !_WIN32 */

#ifdef linux
extern "C" {
  extern char *program_invocation_short_name;
}
#endif

void ZiLog::init_()
{
  Guard guard(m_lock);
  init__();
}

void ZiLog::init__()
{
  if (m_program) return;
#ifdef linux
  init__(program_invocation_short_name, "user");
#else
  init__("ZiLog", "user");
#endif
}

void ZiLog::init_(const char *program)
{
  Guard guard(m_lock);
  init__(program, "user");
}

void ZiLog::init_(const char *program, const char *facility)
{
  Guard guard(m_lock);
  init__(program, facility);
}

void ZiLog::init__(const char *program, const char *facility)
{
  // intentionally not idempotent - permit re-initialization
  m_program = program;
  m_facility = facility;
#ifndef _WIN32
  static const char * const names[] = {
    "daemon",
    "local0", "local1", "local2", "local3",
    "local4", "local5", "local6", "local7", 0
  };
  static const int values[] = {
    LOG_DAEMON,
    LOG_LOCAL0, LOG_LOCAL1, LOG_LOCAL2, LOG_LOCAL3,
    LOG_LOCAL4, LOG_LOCAL5, LOG_LOCAL6, LOG_LOCAL7
  };
  const char *name;

  if (facility)
    for (unsigned i = 0; name = names[i]; i++) {
      if (!strcmp(facility, name)) {
	syslogger()->init(program, values[i]);
	return;
      }
    }
  syslogger()->init(program, LOG_USER);
#else
  ZmTrap::winProgram(program);
#endif
}

void ZiLog::sink_(ZmRef<ZiSink> sink)
{
  Guard guard(m_lock);
  m_sink = sink;
}

void ZiLog::start_()
{
  Guard guard(m_lock);
  start__();
}

void ZiLog::start__()
{
  if (m_thread) return;
  m_ring.init(ZmRingParams{m_bufSize});
  {
    int r;
    if ((r = m_ring.open(Ring::Read | Ring::Write)) != Zu::OK) // idempotent
      throw Zu::IOResult{r};
  }
  m_thread = ZmThread{[this]() { work_(); },
      ZmThreadParams().name("log").priority(ZmThreadPriority::Low)};
}

void ZiLog::forked_()
{
  Guard guard(m_lock);
  try { start__(); } catch (...) {
    throw ZeEXCEPT(Fatal, "ZiLog", "start failed!");
  }
}

void ZiLog::stop_()
{
  ZmThread thread;
  {
    Guard guard(m_lock);
    thread = m_thread;
    m_thread = {};
  }
  if (!thread) return;
  m_ring.eof(true);
  thread.join();		// wait for ring buffer to drain
  m_ring.close();
}

void ZiLog::work_()
{
  for (;;) {
    if (void *ptr = m_ring.shift()) {
      m_ring.shift2(Fn::invoke(ptr, this));
    } else {
      if (m_ring.readStatus() == Zu::EndOfFile) break;
    }
  }
}

ZmRef<ZiSink> ZiLog::sink_()
{
  Guard guard(m_lock);
  if (ZuUnlikely(!m_sink)) {
#ifdef _WIN32
    m_sink = sysSink(); // on Windows, default to the event log
#else
    m_sink = fileSink(); // on Unix, default to stderr
#endif
  }
  return m_sink;
}

void ZiLog::log__(Fn &fn)
{
  if (ZuUnlikely(!m_ring.ctrl())) {
    Guard guard(m_lock);
    if (!m_program) init__();
    try { start__(); } catch (...) {
      throw ZeEXCEPT(Fatal, "ZiLog", "start failed!");
    }
  }
  unsigned size = fn.pushSize();
  void *ptr;
  if (ZuLikely(ptr = m_ring.push(size))) {
    fn.push(ptr);
    m_ring.push2(ptr, size);
  }
}

void ZiLog::age_()
{
  ZmRef<ZiSink> sink;
  {
    Guard guard(m_lock);
    sink = m_sink;
  }
  if (sink) sink->age();
}

void ZiSysSink::pre(ZiLogBuf &buf, const ZeEventInfo &info)
{
#ifdef _WIN32
  buf << ZuBoxed(info.tid) << " - ";
#endif
  if (info.severity == Ze::Debug || info.severity == Ze::Fatal)
    buf << '\"' << Ze::file(info.file) << "\":" <<
      ZuBoxed(info.line) << ' ';
  buf << '[' << info.component << "] " << Ze::function(info.function) << "() ";
}

void ZiSysSink::post(ZiLogBuf &buf, const ZeEventInfo &info)
{
  buf << '\n';

  {
    unsigned len = buf.length();

    if (buf[len - 1] != '\n') buf[len - 1] = '\n';
  }

#ifndef _WIN32
  ::syslog(syslogger()->facility() | sysloglevel(info.severity),
      "%.*s", buf.length(), buf.data());
#else
  ZmTrap::winErrLog(eventlogtype(info.severity), buf);
#endif
}

static void ageFile(const Zi::Path &path, unsigned age)
{
  unsigned size = path.length() + ZuBoxed(age).length() + 4;

  Zi::Path prevName_(size), nextName_(size), sideName_(size);
  Zi::Path *prevName = &prevName_;
  Zi::Path *nextName = &nextName_;
  Zi::Path *sideName = &sideName_;

  *prevName << path;
  bool last = false;
  unsigned i;
  for (i = 0; i < age && !last; i++) {
    nextName->length(0);
    *nextName << path << '.' << ZuBoxed(i + 1);
    sideName->length(0);
    *sideName << *nextName << '_';
    last = (ZiFile::rename(*nextName, *sideName) != Zi::OK);
    ZiFile::rename(*prevName, *nextName);
    Zi::Path *oldName = prevName;
    prevName = sideName;
    sideName = oldName;
  }
  if (i == age) ZiFile::remove(*prevName);
}

void ZiFileSink::init()
{
  if (!m_path) m_path << ZiLog::program() << ".log";

  if (m_path != "&2") {
    ageFile(m_path, m_age);
    m_file.open(m_path, ZiFile::Write | ZiFile::GC);
  }

  if (!m_file) m_file = ZiFile::stdErr();
}

ZiFileSink::~ZiFileSink()
{
}

void ZiFileSink::pre(ZiLogBuf &buf, const ZeEventInfo &info)
{
  ZuDateTime d{info.time};

  buf << d.fmt(m_dateFmt) << ' ' <<
    ZuBoxed(info.tid) << ' ' <<
    Ze::severity(info.severity) << ' ';
  if (info.severity == Ze::Debug || info.severity == Ze::Fatal)
    buf << '\"' << Ze::file(info.file) << "\":" <<
      ZuBoxed(info.line) << ' ';
  buf << '[' << info.component << "] " << Ze::function(info.function) << "() ";
}

void ZiFileSink::post(ZiLogBuf &buf, const ZeEventInfo &info)
{
  buf << '\n';

  unsigned len = buf.length();

  if (buf[len - 1] != '\n') buf[len - 1] = '\n';

  m_file.write(buf.data(), len);
}

void ZiFileSink::age()
{
  if (m_path == "&2") return;

  m_file.close();
  ageFile(m_path, m_age);
  m_file.open(m_path, ZiFile::Write | ZiFile::GC);
}

void ZiCSVSink::init()
{
  if (!m_path) m_path << ZiLog::program() << ".csv";

  ageFile(m_path, m_age);

  new (m_writer.new_<Writer>()) Writer(m_path);
}

ZiCSVSink::~ZiCSVSink() { }

void ZiCSVSink::pre(ZiLogBuf &, const ZeEventInfo &) { }

void ZiCSVSink::post(ZiLogBuf &buf, const ZeEventInfo &info)
{
  ZiSinkEvent event(buf, info);
  m_writer.p<Writer>()(event);
}

void ZiCSVSink::age()
{
  m_writer.new_<void>();
  ageFile(m_path, m_age);
  new (m_writer.new_<Writer>()) Writer(m_path);
}

void ZiDebugSink::init()
{
  m_path << ZiLog::program() << ".log." << ZuBoxed(Zm::getPID());

  if (m_path != "&2")
    m_file.open(m_path, ZiFile::Write | ZiFile::GC);

  if (!m_file) m_file = ZiFile::stdErr();
}

ZiDebugSink::~ZiDebugSink()
{
}

void ZiDebugSink::pre(ZiLogBuf &buf, const ZeEventInfo &info)
{
  ZuTime d = info.time - m_started;

  buf << '+' << d.interval() << ' ' <<
    ZuBoxed(info.tid) << ' ' <<
    Ze::severity(info.severity) << ' ';
  if (info.severity == Ze::Debug || info.severity == Ze::Fatal)
    buf << '\"' << Ze::file(info.file) << "\":" <<
      ZuBoxed(info.line) << ' ';
  buf << '[' << info.component << "] " << Ze::function(info.function) << "() ";
}

void ZiDebugSink::post(ZiLogBuf &buf, const ZeEventInfo &info)
{
  buf << '\n';

  unsigned len = buf.length();

  if (buf[len - 1] != '\n') buf[len - 1] = '\n';

  m_file.write(buf.data(), len);
}

void ZiLambdaSink_::pre(ZiLogBuf &buf, const ZeEventInfo &info)
{
  ZuDateTime d{info.time};

  buf << d.fmt(m_dateFmt) << ' ' <<
    ZuBoxed(info.tid) << ' ' <<
    Ze::severity(info.severity) << ' ';
  if (info.severity == Ze::Debug || info.severity == Ze::Fatal)
    buf << '\"' << Ze::file(info.file) << "\":" <<
      ZuBoxed(info.line) << ' ';
  buf << '[' << info.component << "] " << Ze::function(info.function) << "() ";
}
