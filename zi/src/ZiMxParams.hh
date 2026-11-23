//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// multiplexer configuration

#ifndef ZiMxParams_HH
#define ZiMxParams_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <zlib/ZmScheduler.hh>

#if defined(ZDEBUG) && !defined(ZiMultiplex_DEBUG)
#define ZiMultiplex_DEBUG
#endif
#ifdef linux
#define ZiMultiplex_EPoll
#endif

// named parameter list for configuring ZiMultiplex
class ZiMxParams {
public:
  enum { RxThread = 1, TxThread = 2 }; // defaults
  enum { RxQuantum = 8 };
  enum { EPollMaxFDs = 256 };

  ZiMxParams() :
    m_scheduler{ZmSchedParams{}
      .nThreads(3)
      .thread(ZiMxParams::RxThread, [](auto &t) { t.isolated(true); })
      .thread(ZiMxParams::TxThread, [](auto &t) { t.isolated(true); })} { }

  ZiMxParams(const ZiMxParams &) = default;
  ZiMxParams &operator =(const ZiMxParams &) = default;
  ZiMxParams(ZiMxParams &&) = default;
  ZiMxParams &operator =(ZiMxParams &&) = default;

  ZiMxParams &&rxThread(unsigned tid)
    { m_rxThread = tid; return ZuMv(*this); }
  ZiMxParams &&rxThread(ZuCSpan id) {
    unsigned tid = m_scheduler.sid(id);
    return rxThread(tid && tid <= m_scheduler.nThreads() ? tid : 0);
  }
  ZiMxParams &&txThread(unsigned tid)
    { m_txThread = tid; return ZuMv(*this); }
  ZiMxParams &&txThread(ZuCSpan id) {
    unsigned tid = m_scheduler.sid(id);
    return txThread(tid && tid <= m_scheduler.nThreads() ? tid : 0);
  }
  ZiMxParams &&rxBufSize(unsigned v)
    { m_rxBufSize = v; return ZuMv(*this); }
  ZiMxParams &&txBufSize(unsigned v)
    { m_txBufSize = v; return ZuMv(*this); }
  ZiMxParams &&rxQuantum(unsigned n)
    { m_rxQuantum = n; return ZuMv(*this); }
#ifdef ZiMultiplex_EPoll
  ZiMxParams &&epollMaxFDs(unsigned n)
    { m_epollMaxFDs = n; return ZuMv(*this); }
#endif
  ZiMxParams &&listenerHash(const char *id)
    { m_listenerHash = id; return ZuMv(*this); }
  ZiMxParams &&requestHash(const char *id)
    { m_requestHash = id; return ZuMv(*this); }
  ZiMxParams &&cxnHash(const char *id)
    { m_cxnHash = id; return ZuMv(*this); }
#ifdef ZiMultiplex_DEBUG
  ZiMxParams &&trace(bool b) { m_trace = b; return ZuMv(*this); }
  ZiMxParams &&debug(bool b) { m_debug = b; return ZuMv(*this); }
  ZiMxParams &&frag(bool b) { m_frag = b; return ZuMv(*this); }
  ZiMxParams &&yield(bool b) { m_yield = b; return ZuMv(*this); }
#endif

  ZmSchedParams &scheduler() { return m_scheduler; }

  template <typename L>
  ZiMxParams &&scheduler(L &&l) {
    ZuFwd<L>(l)(m_scheduler);
    return ZuMv(*this);
  }

  unsigned rxThread() const { return m_rxThread; }
  unsigned txThread() const { return m_txThread; }
  ZuCSpan rxThreadID() const { return m_scheduler.thread(m_rxThread).name(); }
  ZuCSpan txThreadID() const { return m_scheduler.thread(m_txThread).name(); }
  unsigned rxBufSize() const { return m_rxBufSize; }
  unsigned txBufSize() const { return m_txBufSize; }
  unsigned rxQuantum() const { return m_rxQuantum; }
#ifdef ZiMultiplex_EPoll
  unsigned epollMaxFDs() const { return m_epollMaxFDs; }
#endif
  ZuCSpan listenerHash() const { return m_listenerHash; }
  ZuCSpan requestHash() const { return m_requestHash; }
  ZuCSpan cxnHash() const { return m_cxnHash; }
#ifdef ZiMultiplex_DEBUG
  bool trace() const { return m_trace; }
  bool debug() const { return m_debug; }
  bool frag() const { return m_frag; }
  bool yield() const { return m_yield; }
#endif

private:
  ZmSchedParams		m_scheduler;
  unsigned		m_rxThread = RxThread;
  unsigned		m_txThread = TxThread;
  unsigned		m_rxBufSize = 0;
  unsigned		m_txBufSize = 0;
  unsigned		m_rxQuantum = RxQuantum;
#ifdef ZiMultiplex_EPoll
  unsigned		m_epollMaxFDs = EPollMaxFDs;
#endif

  const char		*m_listenerHash = "ZiMultiplex.ListenerHash";
  const char		*m_requestHash = "ZiMultiplex.RequestHash";
  const char		*m_cxnHash = "ZiMultiplex.CxnHash";
#ifdef ZiMultiplex_DEBUG
  bool			m_trace = false;
  bool			m_debug = false;
  bool			m_frag = false;
  bool			m_yield = false;
#endif
};

#endif /* ZiMxParams_HH */
