//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// asynchronous DNS and host resolver

#include <ares.h>

#include <zlib/ZuDerive.hh>
#include <zlib/ZuUTF.hh>

#include <zlib/ZmBlock.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmSingleton.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZiResolver.hh>

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

using namespace ZiResolver_;

static ZeError aresError(int status)
{
  return ZeError(status);
}

static Zi::Name hostName(const Host &host)
{
#ifndef _WIN32
  return Zi::Name{host};
#else
  Zi::Name name;
  name.length(ZuUTF<char, wchar_t>::cvt(name.span(), host));
  name.truncate();
  return name;
#endif
}

static void setHost(Host &host, ZuCSpan s)
{
#ifndef _WIN32
  host = s;
#else
  host.length(ZuUTF<wchar_t, char>::cvt(host.span(), s));
  host.truncate();
#endif
}

static Zi::Socket ziSocket(ares_socket_t s)
{
  return static_cast<Zi::Socket>(s);
}

static ares_socket_t aresSocket(Zi::Socket s)
{
  return static_cast<ares_socket_t>(s);
}

static uint16_t u16(const uint8_t *ptr)
{
  return (uint16_t(ptr[0]) << 8) | uint16_t(ptr[1]);
}

static uint32_t u32(const uint8_t *ptr)
{
  return
    (uint32_t(ptr[0]) << 24) | (uint32_t(ptr[1]) << 16) |
    (uint32_t(ptr[2]) << 8) | uint32_t(ptr[3]);
}

static void setBuf(DNSBuf &buf, ZuBSpan data)
{
  buf.length(data.length());
  for (unsigned i = 0, n = data.length(); i < n; i++) buf[i] = data[i];
}

static bool dnsName(
  const uint8_t *msg, unsigned len, unsigned &off, ZtString<> *name = nullptr)
{
  enum { MaxSteps = 64 };

  unsigned pos = off;
  bool jumped = false;
  for (unsigned steps = 0; steps < MaxSteps; steps++) {
    if (pos >= len) return false;
    uint8_t c = msg[pos++];
    switch (c & 0xc0) {
      case 0x00:
	if (!c) {
	  if (!jumped) off = pos;
	  return true;
	}
	if (pos + c > len) return false;
	if (name) {
	  if (name->length()) *name << '.';
	  *name << ZuBSpan{msg + pos, c};
	}
	pos += c;
	break;
      case 0xc0:
	if (pos >= len) return false;
	if (!jumped) off = pos + 1;
	pos = ((unsigned(c & 0x3f) << 8) | unsigned(msg[pos]));
	if (pos >= len) return false;
	jumped = true;
	break;
      default:
	return false;
    }
  }
  return false;
}

struct ZiResolver_TXT : public ZmNHeap<ZiResolver_TXT>, public ZmObject {
friend inline ZuStringT<"ZiResolver.TXT"> ZmNHeapID(ZiResolver_TXT *);

  DNSBuf		buf;
  TxtFn		fn;
};

class ZiResolver_Sched : public ZmNHeap<ZiResolver_Sched>, public ZmScheduler {
friend inline ZuStringT<"ZiResolver.Sched"> ZmNHeapID(ZiResolver_Sched *);
public:
  using ZmScheduler::ZmScheduler;
};

static ZiEvent::FailFn defltFailFn()
{
  return [](ZeException e) {
    ZiLOG(Fatal, "ZiResolver", ZuMv(e));
  };
}

static void schedParams(Params &params)
{
  params.scheduler()
    .id("ZiResolver")
    .nThreads(1);
}

void ZiResolver_AQueryCB(
  void *arg, int status, int, struct hostent *host)
{
  ZmRef<Query> query{static_cast<Query *>(arg)};
  auto resolver = Main::instance();
  if (status == ARES_SUCCESS && host && host->h_addr_list) {
    for (char **addr = host->h_addr_list;
	*addr && !query->stopped && !query->cancelled(); ++addr) {
      ++query->emitted;
      if (!query->resolveFn(ResolveResult{
	    ZiIP{*reinterpret_cast<const in_addr *>(*addr)}})) {
	query->stopped = true;
	break;
      }
    }
  }
  if (status == ARES_SUCCESS) query->status = ARES_SUCCESS;
  else if (query->status == ARES_ENOTFOUND) query->status = status;
  if (--query->pending) return;
  bool cancelled = resolver->queryCancelled_(query);
  auto fn = ZuMv(query->resolveFn);
  resolver->delQuery_(query);
  if (!cancelled) {
    if (!query->emitted)
      fn(ResolveResult{aresError(query->status)});
    else if (!query->stopped)
      fn(ResolveResult{});
  }
}

void ZiResolver_AAAAQueryCB(
  void *arg, int status, int, struct hostent *host)
{
  ZmRef<Query> query{static_cast<Query *>(arg)};
  auto resolver = Main::instance();
  if (status == ARES_SUCCESS && host && host->h_addr_list) {
    for (char **addr = host->h_addr_list;
	*addr && !query->stopped && !query->cancelled(); ++addr) {
      ++query->emitted;
      if (!query->resolveFn(ResolveResult{
	    ZiIP{*reinterpret_cast<const in6_addr *>(*addr)}})) {
	query->stopped = true;
	break;
      }
    }
  }
  if (status == ARES_SUCCESS) query->status = ARES_SUCCESS;
  else if (query->status == ARES_ENOTFOUND) query->status = status;
  if (--query->pending) return;
  bool cancelled = resolver->queryCancelled_(query);
  auto fn = ZuMv(query->resolveFn);
  resolver->delQuery_(query);
  if (!cancelled) {
    if (!query->emitted)
      fn(ResolveResult{aresError(query->status)});
    else if (!query->stopped)
      fn(ResolveResult{});
  }
}

void ZiResolver_NameCB(
  void *arg, int status, int, char *node, char *)
{
  ZmRef<Query> query{static_cast<Query *>(arg)};
  auto resolver = Main::instance();
  bool cancelled = resolver->queryCancelled_(query);
  auto fn = ZuMv(query->nameFn);
  resolver->delQuery_(query);
  if (status == ARES_SUCCESS && node) {
    Host host;
    setHost(host, node);
    if (!cancelled) fn(NameResult{ZuMv(host)});
    return;
  }
  if (!cancelled) fn(NameResult{aresError(status)});
}

void ZiResolver_QueryCB(
  void *arg, int status, int, unsigned char *abuf, int alen)
{
  ZmRef<Query> query{static_cast<Query *>(arg)};
  auto resolver = Main::instance();
  bool cancelled = resolver->queryCancelled_(query);
  auto fn = ZuMv(query->queryFn);
  resolver->delQuery_(query);
  if (status == ARES_SUCCESS && abuf && alen > 0) {
    DNSMsg msg;
    msg.name = hostName(query->host);
    msg.type = query->type;
    msg.class_ = query->class_;
    setBuf(msg.buf, ZuBSpan{abuf, unsigned(alen)});
    if (!cancelled) fn(QueryResult{ZuMv(msg)});
    return;
  }
  if (!cancelled) fn(QueryResult{aresError(status)});
}

namespace ZiResolver_ {

Main::Main()
{
}

Main::~Main()
{
  ZmAssert_(Engine::state() == ZmEngineState::Stopped);
  final_();
}

Main *Main::instance()
{
  return
    ZmSingleton<Main,
      ZmSingletonCleanup<ZmCleanup::Library>>::instance();
}

void Main::init()
{
  instance()->init_();
}

void Main::init(Params params)
{
  instance()->init_(ZuMv(params), defltFailFn());
}

void Main::init(Params params, ZiEvent::FailFn failFn)
{
  instance()->init_(ZuMv(params), ZuMv(failFn));
}

void Main::start(CtrlFn fn)
{
  instance()->Engine::start(ZuMv(fn));
}

void Main::stop(CtrlFn fn)
{
  instance()->Engine::stop(ZuMv(fn));
}

bool Main::startSync()
{
  return ZmBlock<bool>{}([](auto wake) {
    start(ZuMv(wake));
  });
}

bool Main::stopSync()
{
  return ZmBlock<bool>{}([](auto wake) {
    stop(ZuMv(wake));
  });
}

void Main::final()
{
  instance()->final_();
}

ZmRef<Query> Main::resolve(Host host, ResolveFn fn)
{
  return instance()->resolve_(ZuMv(host), ZuMv(fn));
}

ZmRef<Query> Main::name(ZiIP ip, NameFn fn)
{
  return instance()->name_(ip, ZuMv(fn));
}

ZmRef<Query> Main::query(
  Host host, uint16_t type, uint16_t class_, QueryFn fn)
{
  return instance()->query_(ZuMv(host), type, class_, ZuMv(fn));
}

void Main::txt(const DNSMsg &dns, TxtFn fn)
{
  instance()->txt_(dns, ZuMv(fn));
}

void Main::cancel(ZmRef<Query> handle)
{
  instance()->cancel_(ZuMv(handle));
}

bool Main::initialized() const
{
  Guard guard(m_lock);
  return m_params.is<Params>();
}

void Main::addQuery_(ZmRef<Query> query)
{
  m_queries.addNode(ZuMv(query));
}

void Main::delQuery_(Query *query)
{
  m_queries.delNode(query);
}

bool Main::queryCancelled_(const Query *query) const
{
  return m_loop.stopping() || !query || query->cancelled();
}

void Main::txt_(const DNSMsg &dns, TxtFn fn)
{
  ZmRef<ZiResolver_TXT> query = new ZiResolver_TXT;
  query->buf = dns.buf;
  query->fn = ZuMv(fn);
  invoke_([query = ZuMv(query)](bool ready) mutable {
    if (!ready) { query->fn(TXTResult{aresError(ARES_ECANCELLED)}); return; }
    enum { HdrLen = 12 };

    auto &fn = query->fn;
    auto fail = [&fn](int errNo) {
      fn(TXTResult{ZeError(errNo)});
    };

    const uint8_t *msg = query->buf.data();
    unsigned len = query->buf.length();
    if (len < HdrLen) { fail(ZiEINVAL); return; }

    unsigned qd = u16(msg + 4);
    unsigned an = u16(msg + 6);
    unsigned ns = u16(msg + 8);
    unsigned ar = u16(msg + 10);
    unsigned off = HdrLen;

    for (unsigned i = 0; i < qd; i++) {
      if (!dnsName(msg, len, off)) { fail(ZiEINVAL); return; }
      if (off + 4 > len) { fail(ZiEINVAL); return; }
      off += 4;
    }

    unsigned emitted = 0;
    unsigned rr = an + ns + ar;
    for (unsigned i = 0; i < rr; i++) {
      if (!dnsName(msg, len, off)) { fail(ZiEINVAL); return; }
      if (off + 10 > len) { fail(ZiEINVAL); return; }
      unsigned type = u16(msg + off);
      unsigned class_ = u16(msg + off + 2);
      (void)u32(msg + off + 4);
      unsigned rdlen = u16(msg + off + 8);
      off += 10;
      if (off + rdlen > len) { fail(ZiEINVAL); return; }
      if (type == DNSType::TXT && class_ == DNSClass::IN) {
	unsigned end = off + rdlen;
	unsigned pos = off;
	while (pos < end) {
	  unsigned n = msg[pos++];
	  if (pos + n > end) { fail(ZiEINVAL); return; }
	  ++emitted;
	  if (!fn(TXTResult{ZuBSpan{msg + pos, n}})) {
	    return;
	  }
	  pos += n;
	}
      }
      off += rdlen;
    }

    if (!emitted) { fail(EAI_NONAME); return; }
    fn(TXTResult{});
  });
}

void Main::cancel_(ZmRef<Query> handle)
{
  if (handle) handle->cancel();
}

void Main::init_()
{
  init_(Params{}, defltFailFn());
}

void Main::init_(Params params, ZiEvent::FailFn failFn)
{
  Guard guard(m_lock);
  ZmAssert_(Engine::state() == ZmEngineState::Stopped);
  init__(ZuMv(params), ZuMv(failFn));
}

void Main::init__(Params params, ZiEvent::FailFn failFn)
{
  if (m_params.is<Params>()) return;
  if (!params.ipv4() && !params.ipv6())
    throw ZeEXCEPT(Fatal, "ZiResolver", "IPv4 and IPv6 cannot both be disabled");

  schedParams(params);

  m_params = ZuMv(params);
  m_failFn = ZuMv(failFn);
  openChannel_();
  m_sched = new ZiResolver_Sched{params_().scheduler()};
  m_loop.init(m_sched, SID, m_failFn);
}

void Main::openChannel_()
{
  if (ares_library_init(ARES_LIB_INIT_ALL) != ARES_SUCCESS)
    throw ZeEXCEPT(Fatal, "ZiResolver", "ares_library_init() failed");
  ares_options options;
  memset(&options, 0, sizeof(options));
  int optmask = ARES_OPT_SOCK_STATE_CB;
  options.sock_state_cb = &Main::socketState;
  options.sock_state_cb_data = this;
  options.lookups = const_cast<char *>("fb");
  optmask |= ARES_OPT_LOOKUPS;
  if (params_().timeoutMS()) {
    options.timeout = params_().timeoutMS();
    optmask |= ARES_OPT_TIMEOUTMS;
  }
  if (params_().tries()) {
    options.tries = params_().tries();
    optmask |= ARES_OPT_TRIES;
  }
  if (ares_init_options(&m_channel, &options, optmask) != ARES_SUCCESS) {
    ares_library_cleanup();
    throw ZeEXCEPT(Fatal, "ZiResolver", "ares_init_options() failed");
  }
  if (params_().servers() &&
      ares_set_servers_ports_csv(m_channel, params_().servers().ndata()) !=
      ARES_SUCCESS) {
    ares_destroy(m_channel);
    m_channel = nullptr;
    ares_library_cleanup();
    throw ZeEXCEPT(Fatal, "ZiResolver", "ares_set_servers_ports_csv() failed");
  }
}

void Main::closeChannel_()
{
  if (!m_channel) return;
  ares_destroy(m_channel);
  m_channel = nullptr;
  ares_library_cleanup();
}

void Main::start_()
{
  ZmScheduler *sched;
  try {
    Guard guard(m_lock);
    if (!m_params.is<Params>()) init__(Params{}, defltFailFn());
    if (!m_channel) openChannel_();
    if (!m_sched) {
      m_sched = new ZiResolver_Sched{params_().scheduler()};
      m_loop.init(m_sched, SID, m_failFn);
    }
    sched = m_sched;
  } catch (ZeException &e) {
    startFailed_(ZuMv(e));
    return;
  } catch (Zu::IOResult) {
    startFailed_(ZeEXCEPT(Fatal, "ZiResolver", "scheduler initialization failed"));
    return;
  }
  sched->ZmEngine<ZmScheduler>::start([this](bool ok) {
    if (!ok) {
      closeSched_();
      startFailed_(ZeEXCEPT(Fatal, "ZiResolver", "scheduler start failed"));
      return;
    }
    m_loop.start([this](ZiEvent::StartResult result) mutable {
      if (!result.is<ZiEvent::Exception>()) {
	// Loop startup completes on SID; publish admission before completion.
	m_up = true;
	{
	  Guard guard(m_lock);
	  m_accepting = true;
	}
	Engine::started(true);
	return;
      }
      // Loop failure has closed its handles. Retire the scheduler on its
      // control thread before publishing the parent's failed startup.
      m_sched->ZmEngine<ZmScheduler>::stop(
	[this, result = ZuMv(result)](bool ok) mutable {
	  ZmAssert_(ok);
	  closeSched_();
	  startFailed_(ZuMv(result).p<ZiEvent::Exception>());
	});
    });
  });
}

void Main::startFailed_(ZeException e)
{
  if (m_failFn) m_failFn(ZuMv(e));
  Engine::started(false);
}

void Main::stop_()
{
  Guard guard(m_lock);
  m_accepting = false;
  // Ingress is now closed. Retire every earlier query and timer post on SID.
  m_loop.run([this]() { stopRx_(); });
}

void Main::stopRx_()
{
  ZiAssert(m_loop.invoked(), "ZiResolver", (), "stop off resolver shard", return);
  m_up = false;
  m_sched->del(&m_timer);
  // c-ares completes all channel requests synchronously on this dedicated SID.
  // Those callbacks unlink their queries before the loop's drain continuation.
  ares_cancel(m_channel);
  ZmAssert_(m_queries.empty_());
  closeChannel_();
  m_loop.stop([this](ZiEvent::StopResult) {
    m_sched->ZmEngine<ZmScheduler>::stop([this](bool ok) {
      ZmAssert_(ok); // concrete ZmScheduler::stop__() always succeeds
      closeSched_();
      Engine::stopped(true);
    });
  });
}

void Main::closeSched_()
{
  ZmScheduler *sched;
  {
    Guard guard(m_lock);
    ZmAssert_(!m_accepting && !m_up && m_queries.empty_());
    sched = m_sched;
    m_sched = nullptr;
    closeChannel_();
    m_loop.final();
  }
  // Called on this scheduler's control thread after its workers have joined
  // (or failed startup). Its destructor detaches that last control handle.
  // Each restart constructs a fresh scheduler, so there is no predecessor
  // control-thread chain to join after parent completion.
  delete sched;
}

void Main::final_()
{
  ZmAssert_(Engine::state() == ZmEngineState::Stopped);
  Guard guard(m_lock);
  ZmAssert_(!m_accepting && !m_up && m_queries.empty_());
  ZmAssert_(!m_sched || m_sched->state() == ZmEngineState::Stopped);
  closeChannel_();
  m_loop.final();
  // A surviving scheduler was initialized but never started: it has no
  // control/worker thread handles. Running schedulers retire inside stop.
  auto sched = m_sched;
  m_sched = nullptr;
  m_params.null();
  m_failFn = {};
  guard.unlock();
  delete sched;
}

ZmRef<Query> Main::resolve_(Host host, ResolveFn fn)
{
#ifndef _WIN32
  ZuCSpan text{host};
#else
  Zi::Name name = hostName(host);
  ZuCSpan text{name};
#endif
  ZiIP ip;
  if (ZiIP::parse(ip, text)) {
    Guard guard(m_lock);
    switch (Engine::state()) {
      case ZmEngineState::StopPending:
      case ZmEngineState::Stopping:
      case ZmEngineState::StartPending:
	guard.unlock();
	fn(ResolveResult{aresError(ARES_ECANCELLED)});
	return {};
    }
    if (!m_params.is<Params>()) init__(Params{}, defltFailFn());
    bool enabled = (params_().ipv4() && ip.v4()) ||
      (params_().ipv6() && ip.v6());
    guard.unlock();
    if (enabled) {
      if (fn(ResolveResult{ip})) fn(ResolveResult{});
      return {};
    }
  }
  ZmRef<Query> query = new Query;
#ifndef _WIN32
  query->name = hostName(host);
#else
  query->name = ZuMv(name);
#endif
  query->resolveFn = ZuMv(fn);
  invoke_([this, query](bool ready) mutable {
    if (query->cancelled()) return;
    if (!ready || !m_channel || m_loop.stopping()) {
      if (!query->cancelled())
	query->resolveFn(ResolveResult{aresError(ARES_ECANCELLED)});
      return;
    }
    addQuery_(query);
    query->pending = 0;
    if (params_().ipv4()) ++query->pending;
    if (params_().ipv6()) ++query->pending;
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    if (params_().ipv4())
      ares_gethostbyname(
	m_channel, query->name.ndata(), AF_INET,
	ZiResolver_AQueryCB, query);
    if (params_().ipv6())
      ares_gethostbyname(
	m_channel, query->name.ndata(), AF_INET6,
	ZiResolver_AAAAQueryCB, query);
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
    armTimer_();
  });
  return query;
}

ZmRef<Query> Main::name_(ZiIP ip, NameFn fn)
{
  ZiSockAddr addr(ip, 0);
  if (!addr) {
    fn(NameResult{aresError(ARES_ENOTFOUND)});
    return {};
  }
  ZmRef<Query> query = new Query;
  query->addr = addr;
  query->nameFn = ZuMv(fn);
  invoke_([this, query](bool ready) mutable {
    if (query->cancelled()) return;
    if (!ready || !m_channel || m_loop.stopping()) {
      if (!query->cancelled())
	query->nameFn(NameResult{aresError(ARES_ECANCELLED)});
      return;
    }
    addQuery_(query);
    ares_getnameinfo(
      m_channel, query->addr.sa(), query->addr.len(), 0,
      ZiResolver_NameCB, query);
    armTimer_();
  });
  return query;
}

ZmRef<Query> Main::query_(
  Host host, uint16_t type, uint16_t class_, QueryFn fn)
{
  ZmRef<Query> query = new Query;
  query->host = ZuMv(host);
  query->name = hostName(query->host);
  query->type = type;
  query->class_ = class_;
  query->queryFn = ZuMv(fn);
  invoke_([this, query](bool ready) mutable {
    if (query->cancelled()) return;
    if (!ready || !m_channel || m_loop.stopping()) {
      if (!query->cancelled())
	query->queryFn(QueryResult{aresError(ARES_ECANCELLED)});
      return;
    }
    addQuery_(query);
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    ares_query(
      m_channel, query->name.ndata(), query->class_, query->type,
      ZiResolver_QueryCB, query);
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
    armTimer_();
  });
  return query;
}

void Main::socketState(
  void *ptr, ares_socket_t socket, int readable, int writable)
{
  static_cast<Main *>(ptr)->socketState_(socket, readable, writable);
}

void Main::socketState_(
  ares_socket_t socket, int readable, int writable)
{
  Zi::Socket s = ziSocket(socket);
  m_loop.delSocket(s);
  if (!m_up || m_loop.stopping()) return;
  if (!readable && !writable) {
    armTimer_();
    return;
  }
  m_loop.addSocket(s,
    writable ? ZiEvent::SocketSendFn{[this](Zi::Socket socket_) {
      process_(ARES_SOCKET_BAD, aresSocket(socket_));
    }} : ZiEvent::SocketSendFn{},
    readable ? ZiEvent::SocketRecvFn{[this](Zi::Socket socket_) {
      process_(aresSocket(socket_), ARES_SOCKET_BAD);
    }} : ZiEvent::SocketRecvFn{},
    false);
  armTimer_();
}

void Main::process_(ares_socket_t readFD, ares_socket_t writeFD)
{
  if (m_up && m_channel && !m_loop.stopping()) {
    ares_process_fd(m_channel, readFD, writeFD);
    armTimer_();
  }
}

void Main::armTimer_()
{
  if (!m_up || !m_sched || !m_channel || m_loop.stopping()) return;

  timeval tv;
  timeval *timeout = ares_timeout(m_channel, nullptr, &tv);
  if (!timeout) {
    m_sched->del(&m_timer);
    return;
  }

  ZuTime when = Zm::now() + ZuTime{*timeout};
  m_sched->add(&m_timer, when, ZmScheduler::Update,
    [this](auto &&arm) {
      return arm([this]() { timeout_(); });
    }, SID);
}

void Main::timeout_()
{
  process_(ARES_SOCKET_BAD, ARES_SOCKET_BAD);
}

} // ZiResolver_
