//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC qlog

#ifndef ZquicLog_HH
#define ZquicLog_HH

#ifndef ZquicLib_HH
#include <zlib/ZquicLib.hh>
#endif

#include <zlib/ZuTime.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmGuard.hh>
#include <zlib/ZmPLock.hh>
#include <zlib/ZmRing.hh>
#include <zlib/ZmRingFn.hh>
#include <zlib/ZmThread.hh>

#include <zlib/ZtString.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZiFile.hh>

#if defined(ZDEBUG) && !defined(Zquic_DEBUG)
#define Zquic_DEBUG	// enable testing / debugging
#endif

struct ZquicLogDiag {
  uint64_t	recordsEnqueued = 0;
  uint64_t	recordsWritten = 0;
  uint64_t	recordsDropped = 0;
  uint64_t	ringBackPressure = 0;
  uint64_t	writerFailures = 0;
  uint64_t	bytesWritten = 0;
};

struct ZquicLogParams {
  ZquicLogParams &enabled(bool v) { m_enabled = v; return *this; }
  ZquicLogParams &path(ZuCSpan v) { m_path = v; return *this; }
  ZquicLogParams &thread(ZuCSpan v) { m_thread = v; return *this; }
  ZquicLogParams &ringSize(unsigned v) { m_ringSize = v; return *this; }

  bool enabled() const { return m_enabled; }
  ZuCSpan path() const { return m_path; }
  ZuCSpan thread() const { return m_thread; }
  unsigned ringSize() const { return m_ringSize; }

private:
  bool		m_enabled = false;
  ZtString<>	m_path;
  ZtString<>	m_thread;
  unsigned	m_ringSize = (1<<20);
};

#ifdef Zquic_DEBUG

class ZquicAPI ZquicLogSink {
public:
  bool init(ZuCSpan);
  void final();
  bool write(ZuCSpan);

private:
  Zi::Path	m_path;
  ZiFile	m_file;
};

class ZquicAPI ZquicLog {
  using Lock = ZmPLock;
  using Guard = ZmGuard<Lock>;
  ZuDerive(Ring, (ZmRing<ZmRingMW<true>>));
  using Fn = ZmRingFn<ZquicLog *>;

public:
  ~ZquicLog();

  ZquicLog(const ZquicLog &) = delete;
  ZquicLog &operator =(const ZquicLog &) = delete;

  static ZquicLog *instance();

  static bool enabled() { return instance()->enabled_(); }
  static bool init(const ZquicLogParams &params) {
    return instance()->init_(params);
  }
  static void start() { instance()->start_(); }
  static void stop() { instance()->stop_(); }
  static void final() { instance()->final_(); }
  static ZquicLogDiag diag() { return instance()->diag_(); }

  static void event(
    ZuCSpan name, ZuCSpan category, ZuCSpan cid = {},
    uint64_t value = 0, ZuCSpan detail = {}) {
    instance()->event_(name, category, cid, value, detail);
  }

private:
  ZquicLog();

  bool enabled_() const { return m_enabled.load_(); }
  bool init_(const ZquicLogParams &);
  void start_();
  void stop_();
  void final_();
  ZquicLogDiag diag_() const;
  void event_(
    ZuCSpan name, ZuCSpan category, ZuCSpan cid, uint64_t value,
    ZuCSpan detail);

  void work_();
  bool writeHeader_();
  bool writeEvent_(
    ZuCSpan name, ZuCSpan category, ZuCSpan cid, uint64_t value,
    ZuCSpan detail, ZuTime time);

private:
  ZmAtomic<int>		m_enabled = 0;
  bool			m_configured = false;
  bool			m_started = false;
  ZquicLogParams		m_params;
  ZmThread		m_thread;
  Ring			m_ring;
  ZquicLogSink		m_sink;
  ZeLogBuf		m_buf;
  ZmAtomic<uint64_t>	m_recordsEnqueued = 0;
  ZmAtomic<uint64_t>	m_recordsWritten = 0;
  ZmAtomic<uint64_t>	m_recordsDropped = 0;
  ZmAtomic<uint64_t>	m_ringBackPressure = 0;
  ZmAtomic<uint64_t>	m_writerFailures = 0;
  ZmAtomic<uint64_t>	m_bytesWritten = 0;
  mutable Lock		m_lock;
};

#else /* Zquic_DEBUG */

struct ZquicLog {
  static constexpr bool enabled() { return false; }
  static bool init(const ZquicLogParams &) { return true; }
  static void start() { }
  static void stop() { }
  static void final() { }
  static ZquicLogDiag diag() { return {}; }
  static void event(
    ZuCSpan, ZuCSpan, ZuCSpan = {}, uint64_t = 0, ZuCSpan = {}) { }
};

#endif /* Zquic_DEBUG */

#endif /* ZquicLog_HH */
