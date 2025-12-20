//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
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

#include <zlib/ZeLog.hh>

ZeLog::ZeLog() : m_level{1}
{
  init_();
}

ZeLog::~ZeLog()
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

ZeLog *ZeLog::instance()
{
  static constexpr auto ctor = []() { return new ZeLog(); };
  return
    ZmSingleton<ZeLog,
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

void ZeLog::init_()
{
  Guard guard(m_lock);
  init__();
}

void ZeLog::init__()
{
  if (m_program) return;
#ifdef linux
  init__(program_invocation_short_name, "user");
#else
  init__("ZeLog", "user");
#endif
}

void ZeLog::init_(const char *program)
{
  Guard guard(m_lock);
  init__(program, "user");
}

void ZeLog::init_(const char *program, const char *facility)
{
  Guard guard(m_lock);
  init__(program, facility);
}

void ZeLog::init__(const char *program, const char *facility)
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

void ZeLog::sink_(ZmRef<ZeSink> sink)
{
  Guard guard(m_lock);
  m_sink = sink;
}

void ZeLog::start_()
{
  Guard guard(m_lock);
  start__();
}

void ZeLog::start__()
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

void ZeLog::forked_()
{
  Guard guard(m_lock);
  try { start__(); } catch (...) {
    throw ZeString{"ZeLog::start failed!"};
  }
}

void ZeLog::stop_()
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

void ZeLog::work_()
{
  for (;;) {
    if (void *ptr = m_ring.shift()) {
      m_ring.shift2(Fn::invoke(ptr, this));
    } else {
      if (m_ring.readStatus() == Zu::EndOfFile) break;
    }
  }
}

ZmRef<ZeSink> ZeLog::sink_()
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

void ZeLog::log__(Fn &fn)
{
  if (ZuUnlikely(!m_ring.ctrl())) {
    Guard guard(m_lock);
    if (!m_program) init__();
    try { start__(); } catch (...) {
      throw ZeString{"ZeLog::start failed!"};
    }
  }
  unsigned size = fn.pushSize();
  void *ptr;
  if (ZuLikely(ptr = m_ring.push(size))) {
    fn.push(ptr);
    m_ring.push2(ptr, size);
  }
}

void ZeLog::age_()
{
  ZmRef<ZeSink> sink;
  {
    Guard guard(m_lock);
    sink = m_sink;
  }
  if (sink) sink->age();
}

void ZeSysSink::pre(ZeLogBuf &buf, const ZeEventInfo &info)
{
#ifndef _WIN32
  if (info.severity == Ze::Debug || info.severity == Ze::Fatal)
    buf << '\"' << Ze::file(info.file) << "\":" <<
      ZuBoxed(info.line) << ' ';
  buf << Ze::function(info.function) << ' ';
#else
  buf << ZuBoxed(info.tid) << " - ";
  if (info.severity == Ze::Debug || info.severity == Ze::Fatal)
    buf << '\"' << Ze::file(info.file) << "\":" <<
      ZuBoxed(info.line) << ' ';
  buf << Ze::function(info.function) << ' ';
#endif
}

void ZeSysSink::post(ZeLogBuf &buf, const ZeEventInfo &info)
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

// suppress security warnings about fopen()
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4996)
#endif

void ZeFileSink::init()
{
  if (!m_path) m_path << ZeLog::program() << ".log";

  if (m_path != "&2") {
    age_();
    m_file = fopen(m_path, "w");
  }

  if (!m_file)
    m_file = stderr;
  else
    setvbuf(m_file, 0, _IOLBF, ZeLog_BUFSIZ);
}

ZeFileSink::~ZeFileSink()
{
  if (m_file) fclose(m_file);
}

void ZeFileSink::pre(ZeLogBuf &buf, const ZeEventInfo &info)
{
  ZuDateTime d{info.time};

  buf << d.fmt(m_dateFmt) << ' ' <<
    ZuBoxed(info.tid) << ' ' <<
    Ze::severity(info.severity) << ' ';
  if (info.severity == Ze::Debug || info.severity == Ze::Fatal)
    buf << '\"' << Ze::file(info.file) << "\":" <<
      ZuBoxed(info.line) << ' ';
  buf << Ze::function(info.function) << "() ";
}

void ZeFileSink::post(ZeLogBuf &buf, const ZeEventInfo &info)
{
  buf << '\n';

  unsigned len = buf.length();

  if (buf[len - 1] != '\n') buf[len - 1] = '\n';

  fwrite(buf.data(), 1, len, m_file);
  if (info.severity > Ze::Debug) fflush(m_file);
}

void ZeFileSink::age()
{
  Guard guard(m_lock);

  fclose(m_file);
  age_();
  m_file = fopen(m_path, "w");
}

void ZeFileSink::age_()
{
  unsigned size = m_path.length() + ZuBoxed(m_age).length() + 4;

  ZeFileName prevName_(size), nextName_(size), sideName_(size);
  ZeFileName *prevName = &prevName_;
  ZeFileName *nextName = &nextName_;
  ZeFileName *sideName = &sideName_;

  *prevName << m_path;
  bool last = false;
  unsigned i;
  for (i = 0; i < m_age && !last; i++) {
    nextName->length(0);
    *nextName << m_path << '.' << ZuBoxed(i + 1);
    sideName->length(0);
    *sideName << *nextName << '_';
    last = (::rename(*nextName, *sideName) < 0);
    ::rename(*prevName, *nextName);
    ZeFileName *oldName = prevName;
    prevName = sideName;
    sideName = oldName;
  }
  if (i == m_age) ::remove(*prevName);
}

void ZeDebugSink::init()
{
  m_path << ZeLog::program() << ".log." << ZuBoxed(Zm::getPID());

  if (m_path != "&2") m_file = fopen(m_path, "w");

  if (!m_file)
    m_file = stderr;
  else
    setvbuf(m_file, 0, _IOLBF, ZeLog_BUFSIZ);
}

ZeDebugSink::~ZeDebugSink()
{
  if (m_file) fclose(m_file);
}

void ZeDebugSink::pre(ZeLogBuf &buf, const ZeEventInfo &info)
{
  ZuTime d = info.time - m_started;

  buf << '+' << d.interval() << ' ' <<
    ZuBoxed(info.tid) << ' ' <<
    Ze::severity(info.severity) << ' ';
  if (info.severity == Ze::Debug || info.severity == Ze::Fatal)
    buf << '\"' << Ze::file(info.file) << "\":" <<
      ZuBoxed(info.line) << ' ';
  buf << Ze::function(info.function) << "() ";
}

void ZeDebugSink::post(ZeLogBuf &buf, const ZeEventInfo &info)
{
  buf << '\n';

  unsigned len = buf.length();

  if (buf[len - 1] != '\n') buf[len - 1] = '\n';

  fwrite(buf.data(), 1, len, m_file);
  fflush(m_file);
}

void ZeLambdaSink_::pre(ZeLogBuf &buf, const ZeEventInfo &info)
{
  ZuDateTime d{info.time};

  buf << d.fmt(m_dateFmt) << ' ' <<
    ZuBoxed(info.tid) << ' ' <<
    Ze::severity(info.severity) << ' ';
  if (info.severity == Ze::Debug || info.severity == Ze::Fatal)
    buf << '\"' << Ze::file(info.file) << "\":" <<
      ZuBoxed(info.line) << ' ';
  buf << Ze::function(info.function) << "() ";
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif
