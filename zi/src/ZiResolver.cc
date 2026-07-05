//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// asynchronous DNS and host resolver

#include <ares.h>

#include <zlib/ZuUTF.hh>

#include <zlib/ZmBlock.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmSingleton.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZiResolver.hh>

extern "C" {
  void ZiResolver_AQueryCB(
    void *, int, int, unsigned char *, int);
  void ZiResolver_AAAAQueryCB(
    void *, int, int, unsigned char *, int);
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

static bool pton4(const Host &host, in_addr &addr)
{
#ifndef _WIN32
  return ::inet_pton(AF_INET, host.data(), &addr) == 1;
#else
  if (::InetPtonW(AF_INET, host.data(), &addr) == 1) return true;
  sockaddr_in sa;
  int len = sizeof(sa);
  memset(&sa, 0, sizeof(sa));
  sa.sin_family = AF_INET;
  if (::WSAStringToAddressW(
	const_cast<wchar_t *>(host.data()), AF_INET, 0,
	reinterpret_cast<sockaddr *>(&sa), &len))
    return false;
  addr = sa.sin_addr;
  return true;
#endif
}

static bool pton6(const Host &host, in6_addr &addr)
{
#ifndef _WIN32
  return ::inet_pton(AF_INET6, host.data(), &addr) == 1;
#else
  if (::InetPtonW(AF_INET6, host.data(), &addr) == 1) return true;
  sockaddr_in6 sa;
  int len = sizeof(sa);
  memset(&sa, 0, sizeof(sa));
  sa.sin6_family = AF_INET6;
  if (::WSAStringToAddressW(
	const_cast<wchar_t *>(host.data()), AF_INET6, 0,
	reinterpret_cast<sockaddr *>(&sa), &len))
    return false;
  addr = sa.sin6_addr;
  return true;
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
  for (unsigned i = 0; i < data.length(); i++) buf[i] = data[i];
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
	  name->append(reinterpret_cast<const char *>(msg + pos), c);
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

template <typename Heap>
struct ZiResolver_TXT_ : public Heap, public ZmObject {
  DNSBuf		buf;
  TXTFn		fn;
};

using ZiResolver_TXT = ZiResolver_TXT_<
  ZmHeap<"ZiResolver.TXT", ZiResolver_TXT_<ZuEmpty>>>;

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
  void *arg, int status, int, unsigned char *abuf, int alen)
{
  ZmRef<Query> query{static_cast<Query *>(arg)};
  if (status == ARES_SUCCESS && abuf && alen > 0) {
    hostent *host = nullptr;
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    status = ares_parse_a_reply(abuf, alen, &host, nullptr, nullptr);
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
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
    if (host) ares_free_hostent(host);
  }
  if (status == ARES_SUCCESS) query->status = ARES_SUCCESS;
  else if (query->status == ARES_ENOTFOUND) query->status = status;
  if (--query->pending) return;
  auto resolver = Main::instance();
  if (!resolver->queryCancelled_(query)) {
    if (!query->emitted)
      query->resolveFn(
	ResolveResult{aresError(query->status)});
    else if (!query->stopped)
      query->resolveFn(ResolveResult{});
  }
  resolver->delQuery_(query);
}

void ZiResolver_AAAAQueryCB(
  void *arg, int status, int, unsigned char *abuf, int alen)
{
  ZmRef<Query> query{static_cast<Query *>(arg)};
  if (status == ARES_SUCCESS && abuf && alen > 0) {
    hostent *host = nullptr;
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    status = ares_parse_aaaa_reply(abuf, alen, &host, nullptr, nullptr);
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
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
    if (host) ares_free_hostent(host);
  }
  if (status == ARES_SUCCESS) query->status = ARES_SUCCESS;
  else if (query->status == ARES_ENOTFOUND) query->status = status;
  if (--query->pending) return;
  auto resolver = Main::instance();
  if (!resolver->queryCancelled_(query)) {
    if (!query->emitted)
      query->resolveFn(
	ResolveResult{aresError(query->status)});
    else if (!query->stopped)
      query->resolveFn(ResolveResult{});
  }
  resolver->delQuery_(query);
}

void ZiResolver_NameCB(
  void *arg, int status, int, char *node, char *)
{
  ZmRef<Query> query{static_cast<Query *>(arg)};
  auto resolver = Main::instance();
  if (status == ARES_SUCCESS && node) {
    Host host;
    setHost(host, node);
    if (!resolver->queryCancelled_(query))
      query->nameFn(NameResult{ZuMv(host)});
    resolver->delQuery_(query);
    return;
  }
  if (!resolver->queryCancelled_(query))
    query->nameFn(NameResult{aresError(status)});
  resolver->delQuery_(query);
}

void ZiResolver_QueryCB(
  void *arg, int status, int, unsigned char *abuf, int alen)
{
  ZmRef<Query> query{static_cast<Query *>(arg)};
  auto resolver = Main::instance();
  if (status == ARES_SUCCESS && abuf && alen > 0) {
    DNSMsg msg;
    msg.name = hostName(query->host);
    msg.type = query->type;
    msg.klass = query->klass;
    setBuf(msg.buf, ZuBSpan{abuf, unsigned(alen)});
    if (!resolver->queryCancelled_(query))
      query->queryFn(QueryResult{ZuMv(msg)});
    resolver->delQuery_(query);
    return;
  }
  if (!resolver->queryCancelled_(query))
    query->queryFn(QueryResult{aresError(status)});
  resolver->delQuery_(query);
}

namespace ZiResolver_ {

Main::Main()
{
}

Main::~Main()
{
  stop_(ZiEvent::StopFn{});
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

void Main::start(ZiEvent::StartFn fn)
{
  instance()->start_(ZuMv(fn));
}

void Main::stop(ZiEvent::StopFn fn)
{
  instance()->stop_(ZuMv(fn));
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
  Host host, uint16_t type, uint16_t klass, QueryFn fn)
{
  return instance()->query_(ZuMv(host), type, klass, ZuMv(fn));
}

void Main::txt(const DNSMsg &dns, TXTFn fn)
{
  instance()->txt_(dns, ZuMv(fn));
}

void Main::cancel(ZmRef<Query> handle)
{
  instance()->cancel_(ZuMv(handle));
}

bool Main::running() const
{
  Guard guard(m_lock);
  return m_sched && m_sched->running();
}

bool Main::initialized() const
{
  Guard guard(m_lock);
  return m_sched;
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

void Main::txt_(const DNSMsg &dns, TXTFn fn)
{
  start({});
  ZmRef<ZiResolver_TXT> query = new ZiResolver_TXT;
  query->buf = dns.buf;
  query->fn = ZuMv(fn);
  m_loop.invoke([query = ZuMv(query)]() mutable {
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
      unsigned klass = u16(msg + off + 2);
      (void)u32(msg + off + 4);
      unsigned rdlen = u16(msg + off + 8);
      off += 10;
      if (off + rdlen > len) { fail(ZiEINVAL); return; }
      if (type == DNSType::TXT && klass == DNSClass::IN) {
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

  if (m_sched) return;
  if (!params.ipv4() && !params.ipv6())
    throw ZeEXCEPT(Fatal, "ZiResolver", "IPv4 and IPv6 cannot both be disabled");

  schedParams(params);

  m_params = ZuMv(params);
  m_failFn = ZuMv(failFn);
  if (ares_library_init(ARES_LIB_INIT_ALL) != ARES_SUCCESS)
    throw ZeEXCEPT(Fatal, "ZiResolver", "ares_library_init() failed");
  ares_options options;
  memset(&options, 0, sizeof(options));
  int optmask = ARES_OPT_SOCK_STATE_CB;
  options.sock_state_cb = &Main::socketState;
  options.sock_state_cb_data = this;
  options.lookups = const_cast<char *>("fb");
  optmask |= ARES_OPT_LOOKUPS;
  if (m_params.timeoutMS()) {
    options.timeout = m_params.timeoutMS();
    optmask |= ARES_OPT_TIMEOUTMS;
  }
  if (m_params.tries()) {
    options.tries = m_params.tries();
    optmask |= ARES_OPT_TRIES;
  }
  if (ares_init_options(&m_channel, &options, optmask) != ARES_SUCCESS) {
    ares_library_cleanup();
    throw ZeEXCEPT(Fatal, "ZiResolver", "ares_init_options() failed");
  }
  if (m_params.servers() &&
      ares_set_servers_ports_csv(m_channel, m_params.servers().ndata()) !=
      ARES_SUCCESS) {
    ares_destroy(m_channel);
    m_channel = nullptr;
    ares_library_cleanup();
    throw ZeEXCEPT(Fatal, "ZiResolver", "ares_set_servers_ports_csv() failed");
  }
  m_sched = new ZmScheduler{m_params.scheduler()};
  m_loop.init(m_sched, SID, m_failFn);
}

void Main::start_(ZiEvent::StartFn fn)
{
  {
    Guard guard(m_lock);
    if (m_sched) goto initialized;
  }
  init_(Params{}, defltFailFn());

initialized:
  {
    Guard guard(m_lock);
    if (m_sched->running()) {
      if (fn) fn(ZiEvent::StartResult{});
      return;
    }
    if (!m_sched->start()) {
      if (fn) fn(ZiEvent::StartResult{
	ZeEXCEPT(Fatal, "ZiResolver", "scheduler start failed")});
      return;
    }
  }

  if (m_sched->invoked(SID)) {
    m_loop.start(ZuMv(fn));
    return;
  }
  auto result = ZmBlock<ZiEvent::StartResult>{}([this](auto wake) {
    m_loop.start([wake = ZuMv(wake)](ZiEvent::StartResult result) mutable {
      wake(ZuMv(result));
    });
  });
  if (fn) fn(ZuMv(result));
}

void Main::stop_(ZiEvent::StopFn fn)
{
  ZmScheduler *sched = nullptr;
  {
    Guard guard(m_lock);
    sched = m_sched;
  }
  if (!sched || !sched->running()) {
    if (fn) fn(ZiEvent::StopResult{});
    return;
  }

  sched->del(&m_timer);

  if (sched->invoked(SID)) {
    m_loop.stop([this, sched, fn = ZuMv(fn)](
      ZiEvent::StopResult result) mutable {
      if (m_channel) ares_cancel(m_channel);
      sched->stop();
      if (fn) fn(ZuMv(result));
    });
    return;
  }

  auto result = ZmBlock<ZiEvent::StopResult>{}([this](auto wake) {
    m_loop.stop([this, wake = ZuMv(wake)](
      ZiEvent::StopResult result_) mutable {
      if (m_channel) ares_cancel(m_channel);
      wake(ZuMv(result_));
    });
  });
  sched->stop();
  if (fn) fn(ZuMv(result));
}

void Main::final_()
{
  bool running;
  {
    Guard guard(m_lock);
    running = m_sched && m_sched->running();
  }
  if (running) {
    ZmBlock<>{}([this](auto wake) {
      stop_([wake = ZuMv(wake)](ZiEvent::StopResult) mutable { wake(); });
    });
  }

  {
    Guard guard(m_lock);
    bool cleanupAres = m_channel;
    if (m_channel) {
      ares_destroy(m_channel);
      m_channel = nullptr;
    }
    if (cleanupAres) ares_library_cleanup();
    m_loop.final();
    if (m_sched) {
      delete m_sched;
      m_sched = nullptr;
    }
    m_params = Params{};
    m_failFn = ZiEvent::FailFn{};
  }
}

ZmRef<Query> Main::resolve_(Host host, ResolveFn fn)
{
  start({});
  in_addr v4;
  if (m_params.ipv4() && pton4(host, v4)) {
    if (fn(ResolveResult{ZiIP{v4}})) fn(ResolveResult{});
    return {};
  }
  in6_addr v6;
  if (m_params.ipv6() && pton6(host, v6)) {
    if (fn(ResolveResult{ZiIP{v6}})) fn(ResolveResult{});
    return {};
  }
  ZmRef<Query> query = new Query;
  query->name = hostName(host);
  query->resolveFn = ZuMv(fn);
  query->pending = unsigned(m_params.ipv4()) + unsigned(m_params.ipv6());
  m_loop.invoke([this, query]() mutable {
    if (query->cancelled()) return;
    if (!m_channel || m_loop.stopping()) {
      if (!query->cancelled())
	query->resolveFn(ResolveResult{aresError(ARES_ECANCELLED)});
      return;
    }
    addQuery_(query);
    if (m_params.ipv4()) {
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
      ares_query(
	m_channel, query->name.ndata(), DNSClass::IN, DNSType::A,
	ZiResolver_AQueryCB, query);
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
    }
    if (m_params.ipv6()) {
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
      ares_query(
	m_channel, query->name.ndata(), DNSClass::IN, DNSType::AAAA,
	ZiResolver_AAAAQueryCB, query);
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
    }
    armTimer_();
  });
  return query;
}

ZmRef<Query> Main::name_(ZiIP ip, NameFn fn)
{
  start({});
  ZiSockAddr addr(ip, 0);
  if (!addr) {
    fn(NameResult{aresError(ARES_ENOTFOUND)});
    return {};
  }
  ZmRef<Query> query = new Query;
  query->addr = addr;
  query->nameFn = ZuMv(fn);
  m_loop.invoke([this, query]() mutable {
    if (query->cancelled()) return;
    if (!m_channel || m_loop.stopping()) {
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
  Host host, uint16_t type, uint16_t klass, QueryFn fn)
{
  start({});
  ZmRef<Query> query = new Query;
  query->host = ZuMv(host);
  query->name = hostName(query->host);
  query->type = type;
  query->klass = klass;
  query->queryFn = ZuMv(fn);
  m_loop.invoke([this, query]() mutable {
    if (query->cancelled()) return;
    if (!m_channel || m_loop.stopping()) {
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
      m_channel, query->name.ndata(), query->klass, query->type,
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
  if (m_loop.stopping()) return;
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
  if (m_channel && !m_loop.stopping()) {
    ares_process_fd(m_channel, readFD, writeFD);
    armTimer_();
  }
}

void Main::armTimer_()
{
  if (!m_sched || !m_channel || m_loop.stopping()) return;

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
