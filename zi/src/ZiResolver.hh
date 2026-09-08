//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// asynchronous DNS and host resolver

#ifndef ZiResolver_HH
#define ZiResolver_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <ares.h>

#include <zlib/ZuUnion.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmPolymorph.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmScheduler.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtBuiltin.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZiEventLoop.hh>
#include <zlib/ZiIP.hh>

namespace ZiResolver_ {

struct Params {
  Params() = default;
  Params(const Params &) = default;
  Params &operator =(const Params &) = default;
  Params(Params &&) = default;
  Params &operator =(Params &&) = default;

  Params &&ipv4(bool b) { m_ipv4 = b; return ZuMv(*this); }
  Params &&ipv6(bool b) { m_ipv6 = b; return ZuMv(*this); }
  Params &&timeoutMS(unsigned v)
    { m_timeoutMS = v; return ZuMv(*this); }
  Params &&tries(unsigned v) { m_tries = v; return ZuMv(*this); }
  Params &&servers(ZuCSpan s) { m_servers = s; return ZuMv(*this); }

  ZmSchedParams &scheduler() { return m_scheduler; }
  const ZmSchedParams &scheduler() const { return m_scheduler; }

  template <typename L>
  Params &&scheduler(L &&l) {
    ZuFwd<L>(l)(m_scheduler);
    return ZuMv(*this);
  }

  bool ipv4() const { return m_ipv4; }
  bool ipv6() const { return m_ipv6; }
  unsigned timeoutMS() const { return m_timeoutMS; }
  unsigned tries() const { return m_tries; }
  const Zi::Name &servers() const { return m_servers; }

private:
  ZmSchedParams	m_scheduler;
  Zi::Name	m_servers;
  bool		m_ipv4 = true;
  bool		m_ipv6 = true;
  unsigned	m_timeoutMS = 0;
  unsigned	m_tries = 0;
};

namespace DNSClass {
  enum {
    IN = 1
  };
}

namespace DNSType {
  enum {
    A = 1,
    PTR = 12,
    MX = 15,
    TXT = 16,
    AAAA = 28,
    SRV = 33,
    CAA = 257,
    SVCB = 64,
    HTTPS = 65
  };
}

using DNSEvent = ZeError;

enum { DNSBufSize = 512 };

using DNSBuf = ZtBuiltin<
  ZtArray<uint8_t, ZtArrayHeapID<"ZiResolver.DNSBuf">>, DNSBufSize>;

struct DNSMsg {
  Zi::Name	name;
  uint16_t	type = 0;
  uint16_t	class_ = 0;
  DNSBuf	buf;
};

using Host = Zi::Hostname;
using Event = DNSEvent;
using ResolveResult = ZuUnion<void, ZiIP, Event>;
using NameResult = ZuUnion<Host, Event>;
using QueryResult = ZuUnion<DNSMsg, Event>;
using TXTResult = ZuUnion<void, ZuBSpan, Event>;
using ResolveFn = ZmFn<bool(ResolveResult),
  ZmFnHeapID<"ZiResolver.ResolveFn">>;
using NameFn = ZmFn<void(NameResult), ZmFnHeapID<"ZiResolver.NameFn">>;
using QueryFn = ZmFn<void(QueryResult), ZmFnHeapID<"ZiResolver.QueryFn">>;
using TxtFn = ZmFn<bool(TXTResult), ZmFnHeapID<"ZiResolver.TxtFn">>;

class Main;

extern "C" {
  void ZiResolver_AQueryCB(
    void *, int, int, struct hostent *);
  void ZiResolver_AAAAQueryCB(
    void *, int, int, struct hostent *);
  void ZiResolver_NameCB(
    void *, int, int, char *, char *);
  void ZiResolver_QueryCB(
    void *, int, int, unsigned char *, int);
}

class Query_ : public ZmPolymorph {
friend class Main;
friend void ZiResolver_AQueryCB(void *, int, int, struct hostent *);
friend void ZiResolver_AAAAQueryCB(void *, int, int, struct hostent *);
friend void ZiResolver_NameCB(void *, int, int, char *, char *);
friend void ZiResolver_QueryCB(void *, int, int, unsigned char *, int);

public:
  bool cancelled() const { return m_cancelled.load_(); }

  void cancel() { m_cancelled.store_(1); }
  Host			host;
  Zi::Name		name;
  ZiSockAddr		addr;
  ResolveFn		resolveFn;
  NameFn		nameFn;
  QueryFn		queryFn;
  uint16_t		type = 0;
  uint16_t		class_ = 0;
  unsigned		pending = 0;
  unsigned		emitted = 0;
  int			status = ARES_ENOTFOUND;
  bool			stopped = false;

private:
  ZmAtomic<unsigned>	m_cancelled = 0;
};
ZmListDerive(QueryList, Query_,
  ZmListNode<Query_,
    ZmListHeapID<"ZiResolver.Query">>);
using Query = QueryList::Node;

class ZiAPI Main {
friend void ZiResolver_AQueryCB(void *, int, int, struct hostent *);
friend void ZiResolver_AAAAQueryCB(void *, int, int, struct hostent *);
friend void ZiResolver_NameCB(void *, int, int, char *, char *);
friend void ZiResolver_QueryCB(void *, int, int, unsigned char *, int);

  using Lock = ZmPLock;
  using Guard = ZmGuard<Lock>;

public:
  Main();
  ~Main();

  Main(const Main &) = delete;
  Main &operator =(const Main &) = delete;

  static Main *instance();

  static void init();
  static void init(Params);
  static void init(Params, ZiEvent::FailFn);
  static void start(ZiEvent::StartFn = {});
  static void stop(ZiEvent::StopFn = {});
  static void final();

  static ZmRef<Query> resolve(Host, ResolveFn);
  static ZmRef<Query> name(ZiIP, NameFn);
  static ZmRef<Query> query(Host, uint16_t type, uint16_t class_, QueryFn);
  static void txt(const DNSMsg &, TxtFn);
  static void cancel(ZmRef<Query>);

  bool running() const;
  bool initialized() const;

private:
  enum {
    SID = 1
  };

  void init_();
  void init_(Params, ZiEvent::FailFn);
  void start_(ZiEvent::StartFn);
  void stop_(ZiEvent::StopFn);
  void stop__(ZiEvent::StopFn);
  void final_();
  void final__();

  ZmRef<Query> resolve_(Host, ResolveFn);
  ZmRef<Query> name_(ZiIP, NameFn);
  ZmRef<Query> query_(Host, uint16_t type, uint16_t class_, QueryFn);
  void txt_(const DNSMsg &, TxtFn);
  void cancel_(ZmRef<Query>);

  void addQuery_(ZmRef<Query>);

public:
  void delQuery_(Query *);
  bool queryCancelled_(const Query *) const;

private:
  void socketState_(ares_socket_t, int readable, int writable);
  void process_(ares_socket_t readFD, ares_socket_t writeFD);
  void armTimer_();
  void timeout_();

  static void socketState(void *, ares_socket_t, int, int);

private:
  // initialized before start, immutable until final
  Params		m_params;
  ZiEvent::FailFn	m_failFn;

  // lifecycle - guarded by m_lock
  mutable Lock		m_lock;
    ZmScheduler		  *m_sched = nullptr;

  // shared lifecycle object; c-ares activity is on SID
  ZiEventLoop		m_loop;

  // c-ares/SID exclusive after start
  ares_channel_t	*m_channel = nullptr;
  QueryList		m_queries;
  ZmScheduler::Timer	m_timer;
};

} // ZiResolver_

using ZiResolver = ZiResolver_::Main;
using ZiResolverParams = ZiResolver_::Params;
namespace ZiDNSClass = ZiResolver_::DNSClass;
namespace ZiDNSType = ZiResolver_::DNSType;
using ZiDNSEvent = ZiResolver_::DNSEvent;
using ZiDNSBuf = ZiResolver_::DNSBuf;
using ZiDNSMsg = ZiResolver_::DNSMsg;
using ZiResolverHost = ZiResolver_::Host;
using ZiResolverEvent = ZiResolver_::Event;
using ZiResolverResolveResult = ZiResolver_::ResolveResult;
using ZiResolverNameResult = ZiResolver_::NameResult;
using ZiResolverQueryResult = ZiResolver_::QueryResult;
using ZiResolverTXTResult = ZiResolver_::TXTResult;
using ZiResolverResolveFn = ZiResolver_::ResolveFn;
using ZiResolverNameFn = ZiResolver_::NameFn;
using ZiResolverQueryFn = ZiResolver_::QueryFn;
using ZiResolverTxtFn = ZiResolver_::TxtFn;
using ZiResolverQuery = ZiResolver_::Query;

#endif /* ZiResolver_HH */
