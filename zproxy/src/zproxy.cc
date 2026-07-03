//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdio.h>
#include <stdlib.h>

#include <zlib/ZuDerive.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZuPolymorph.hh>

#include <zlib/ZmPlatform.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmTrap.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/ZtString.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZtRegex.hh>
#include <zlib/ZtHexDump.hh>

#include <zlib/ZiMultiplex.hh>

#include <zlib/ZvCf.hh>
#include <zlib/ZvMxParams.hh>

#include <zlib/ZrlCLI.hh>
#include <zlib/ZrlGlobber.hh>
#include <zlib/ZrlHistory.hh>

#include <zlib/ZcmdHost.hh>

class IOBuf;		// I/O buffer
class Connection;	// ZiConnection, owns queue of IO buffers
class Proxy;		// pair of active connections
class Listener;		// spawns proxies
class App;		// the app (singleton) owns mx, listeners and proxies

#define BufSize (32<<10)

ZuDeclTuple(Error, (const char *, op), (int, result), (ZeError, error));
// overloaded print for various types
template <typename T> struct Print : public ZuPrintable {
  Print(const T &v_) : v(v_) { }

  // Error wrapper
  template <typename S, typename T_ = T>
  ZuIs<Error, T_> print(S &s) const {
    s << v.op()
      << "() - " << Zi::ioResult(v.result()) << " - "
      << v.error();
  }
  // ZiCxnInfo
  template <typename S, typename T_ = T>
  ZuIs<ZiCxnInfo, T_> print(S &s) const {
    s << (v.type() == ZiCxnType::TCPIn ? "IN  " : "OUT ") <<
      v.localIP << ':' << ZuBoxed(v.localPort) << " -> " <<
      v.remoteIP << ':' << ZuBoxed(v.remotePort);
  }

  const T &v;
};
template <typename T> Print<T> print(const T &v) { return Print<T>{v}; }

class IOBuf : public ZmPolymorph {
public:
  IOBuf(Connection *connection) :
    m_connection(connection), m_buf(BufSize) { }
  IOBuf(Connection *connection, const ZuTime &stamp) :
    m_connection(connection), m_stamp(stamp), m_buf(BufSize) { }

  Connection *connection() const { return m_connection; }
  void connection(Connection *connection) { m_connection = connection; }

  const ZuTime &stamp() const { return m_stamp; }
  ZuTime &stamp() { return m_stamp; }

  const ZtArray<char> &buf() const { return m_buf; }
  ZtArray<char> &buf() { return m_buf; }

  const char *data() const { return m_buf.data(); }
  char *data() { return m_buf.data(); }
  unsigned length() const { return m_buf.length(); }
  template <typename ...Args>
  void append(Args &&...args) { m_buf.append(ZuFwd<Args>(args)...); }
  template <typename ...Args>
  void splice(Args &&...args) { m_buf.splice(ZuFwd<Args>(args)...); }

  void recv(ZiIOContext *io);
  bool recv_(ZiIOContext &io);
  bool rcvd_(ZiIOContext &io);
  void send(ZiIOContext *io);
  bool send_(ZiIOContext &io);
  bool sent_(ZiIOContext &io);

private:
  Connection	*m_connection;
  ZuTime	m_stamp;
  ZtArray<char>	m_buf;
};

ZuDerive(IOList, (ZmList<ZmRef<IOBuf>, ZmListLock<ZmNoLock> >));

class IOQueue : protected IOList {
public:
  IOQueue() : m_size(0) { }

  uint32_t size() const { return m_size; }

  unsigned count() const { return IOList::count_(); }

  ZmRef<IOBuf> head() const { return IOList::head(); }
  ZmRef<IOBuf> tail() const { return IOList::tail(); }

  void push(ZmRef<IOBuf> &&ioBuf) {
    m_size += ioBuf->length();
    IOList::push(ZuMv(ioBuf));
  }
  void unshift(ZmRef<IOBuf> &&ioBuf) {
    m_size += ioBuf->length();
    IOList::unshift(ZuMv(ioBuf));
  }
  ZmRef<IOBuf> pop() {
    ZmRef<IOBuf> ioBuf = IOList::popVal();
    if (ioBuf) m_size -= ioBuf->length();
    return ioBuf;
  }
  ZmRef<IOBuf> shift() {
    ZmRef<IOBuf> ioBuf = IOList::shiftVal();
    if (ioBuf) m_size -= ioBuf->length();
    return ioBuf;
  }

private:
  uint32_t	m_size;
};

class Connection : public ZiConnection {
  using Lock = ZmPLock;
  using Guard = ZmGuard<Lock>;

public:
  enum {
    In		= 0x001,	// incoming
    Hold	= 0x002,	// held
    SuspRecv	= 0x004,	// read suspended
    SuspSend	= 0x008,	// send suspended
    Trace	= 0x010,	// trace
    Drop	= 0x020		// drop
  };

  Connection(Proxy *proxy,
      uint32_t flags, double latency, uint32_t frag,
      uint32_t pack, double delay, const ZiCxnInfo &ci);

  ZiMultiplex *mx() const { return m_mx; }

  ZmRef<Proxy> proxy() const { return m_proxy; }
  void proxy(Proxy *p) { m_proxy = p; }

  Connection *peer() const { return m_peer; }
  void peer(Connection *peer) { m_peer = peer; }

  uint32_t queueSize() const { return m_queue.size(); }
  uint32_t flags() const { return m_flags; }
  double latency() const { return m_latency; }
  uint32_t frag() const { return m_frag; }
  uint32_t pack() const { return m_pack; }
  double delay() const { return m_delay; }

  void connected(ZiIOContext &io);
  void connected_();
  void disconnected(bool);

  void send(ZmRef<IOBuf> ioBuf);

  void recv();
  void recv(ZiIOContext *io);
  void recv_(ZmRef<IOBuf> ioBuf, ZiIOContext &io);
  void delayedSend();
  void send();
  void send(ZiIOContext *io);
  void send(Guard &guard, ZiIOContext *io);
  void send_(IOBuf *ioBuf, ZiIOContext &io);

  void hold() { m_flags |= Hold; }
  void release();

  void suspRecv() { m_flags |= SuspRecv; }
  void resRecv() { m_flags &= ~SuspRecv; recv(); }
  void suspSend() { m_flags |= SuspSend; }
  void resSend() { m_flags &= ~SuspSend; send(); }

  void trace(bool on) { on ? (m_flags |= Trace) : (m_flags &= ~Trace); }
  void drop(bool on) { on ? (m_flags |= Drop) : (m_flags &= ~Drop); }

  void latency(ZuTime n) { m_latency = n; }
  void frag(uint32_t n) { m_frag = n; }
  void pack(uint32_t n) { m_pack = n; }
  void delay(ZuTime n) { m_delay = n; }

  template <typename S> void print(S &) const;
  friend ZuPrintFn ZuPrintType(Connection *);

private:
  ZiMultiplex		*m_mx;
  ZmRef<Proxy>		m_proxy;
  Connection		*m_peer;
  Lock			m_lock;
    IOQueue		  m_queue;
    bool		  m_sendPending;
  uint32_t		m_flags;
  ZuTime		m_latency;
  uint32_t		m_frag;
  uint32_t		m_pack;
  ZuTime		m_delay;
  ZmScheduler::Timer	m_recvTimer;
  ZmScheduler::Timer	m_discTimer;
  ZmScheduler::Timer	m_sendTimer;
};

class Proxy : public ZmPolymorph {
public:
  Proxy(Listener *listener);
  virtual ~Proxy() { }

  ZiMultiplex *mx() const { return m_mx; }
  App *app() const { return m_app; }

  ZmRef<Listener> listener() const { return m_listener; }

  static unsigned SrcPortAxor(Proxy *p);

  ZmRef<Connection> in() { return m_in; }
  ZmRef<Connection> out() { return m_out; }

  ZuCSpan tag() const { return m_tag; }

  void connected(Connection *connection);
  void connect2();
  ZiConnection *connected2(const ZiCxnInfo &ci);
  void failed2(bool transient);
  void disconnected(Connection *connection);

private:
  void status_(ZuVStream &) const;
  template <typename S> void status_(S &s_) const {
    ZuVStream s{s_};
    status_(s);
  }
public:
  struct Status;
friend Status;
  struct Status {
    template <typename S> void print(S &s) const { p.status_(s); }
    friend ZuPrintFn ZuPrintType(Status *);
    const Proxy &p;
  };
  Status status() const { return Status{*this}; }

private:
  ZiMultiplex		*m_mx;
  App			*m_app;
  ZmRef<Listener>	m_listener;
  ZmRef<Connection>	m_in;
  ZmRef<Connection>	m_out;
  ZuCSpan		m_tag;
  ZmScheduler::Timer	m_connectTimer;
};

template <typename S> inline void Connection::print(S &s) const
{
  const ZiCxnInfo &info = this->info();
  s << (info.type == ZiCxnType::TCPIn ? "IN  " : "OUT ") <<
    info.localIP << ':' << ZuBoxed(info.localPort) <<
    (this->up() ? " -> " : " !> ") <<
    info.remoteIP << ':' << ZuBoxed(info.remotePort) <<
    " (" << (m_proxy ? m_proxy->tag() : ZuCSpan("null")) << ") [" <<
    ((m_flags & Connection::Hold) ? 'H' : '-') <<
    ((m_flags & Connection::SuspRecv) ? 'R' : '-') <<
    ((m_flags & Connection::SuspSend) ? 'S' : '-') <<
    ((m_flags & Connection::Trace) ? 'T' : '-') <<
    ((m_flags & Connection::Drop) ? 'D' : '-') << ']';
}

struct ListenerPrintIn;
struct ListenerPrintOut;
class Listener : public ZmObject {
  ZuDerive(ProxyHash, (ZmHash<ZmRef<Proxy>>));

public:
  Listener(App *app, uint32_t cxnFlags,
      double cxnLatency, uint32_t cxnFrag, uint32_t cxnPack, double cxnDelay,
      ZiIP localIP, unsigned localPort, ZiIP remoteIP, unsigned remotePort,
      ZiIP srcIP, unsigned srcPort, ZuCSpan tag, unsigned reconnectFreq);
  virtual ~Listener() { }

  void add(Proxy *proxy) { m_proxies->add(proxy); }
  void del(Proxy *proxy) { m_proxies->del(proxy); }

  ZiMultiplex *mx() const { return m_mx; }
  App *app() const { return m_app; }

  uint32_t cxnFlags() const { return m_cxnFlags; }
  double cxnLatency() const { return m_cxnLatency; }
  uint32_t cxnFrag() const { return m_cxnFrag; }
  uint32_t cxnPack() const { return m_cxnPack; }
  double cxnDelay() const { return m_cxnDelay; }
  ZiIP localIP() const { return m_localIP; }
  unsigned localPort() const { return m_localPort; }
  ZiIP remoteIP() const { return m_remoteIP; }
  unsigned remotePort() const { return m_remotePort; }
  ZiIP srcIP() const { return m_srcIP; }
  unsigned srcPort() const { return m_srcPort; }
  bool listening() const { return m_listening; }
  ZuCSpan tag() const { return m_tag; }
  unsigned reconnectFreq() const { return m_reconnectFreq; }

  int start();
  void stop();

  ZiConnection *accepted(const ZiCxnInfo &ci);

  static int LocalPortAxor(Listener *listener) {
    return listener->m_localPort;
  }

private:
  void status_(ZuVStream &) const;
  template <typename S> void status_(S &s_) const {
    ZuVStream s{s_};
    status_(s);
  }
public:
  struct Status;
friend Status;
  struct Status {
    template <typename S> void print(S &s) const { l.status_(s); }
    friend ZuPrintFn ZuPrintType(Status *);
    const Listener &l;
  };
  Status status() const { return Status{*this}; }

  template <typename S> void print(S &s) const {
    s << (m_listening ? "LISTEN " : "STOPPED") << " (" << m_tag << ") " <<
      m_localIP << ':' << ZuBoxed(m_localPort) << " = " <<
      m_srcIP << ':' << ZuBoxed(m_srcPort) << " -> " <<
      m_remoteIP << ':' << ZuBoxed(m_remotePort);
  }

  ListenerPrintIn printIn() const;
  ListenerPrintOut printOut() const;

  friend ZuPrintFn ZuPrintType(Listener *);

private:
  void ok(const ZiListenInfo &);
  void failed(bool transient);

friend ListenerPrintIn;
friend ListenerPrintOut;
  template <typename S> void printIn_(S &s) const {
    s << "NC:NC !> " << m_localIP << ':' << ZuBoxed(m_localPort) <<
      " (" << m_tag << ')';
  }
  template <typename S> void printOut_(S &s) const {
    s << m_srcIP << ':' << ZuBoxed(m_srcPort) << " !> " <<
      m_remoteIP << ':' << ZuBoxed(m_remotePort) << " (" << m_tag << ')';
  }

  ZiMultiplex		*m_mx;
  App			*m_app;
  ZmSemaphore		m_started;
  ZmRef<ProxyHash>	m_proxies;
  uint32_t		m_cxnFlags;
  double		m_cxnLatency;
  uint32_t		m_cxnFrag;
  uint32_t		m_cxnPack;
  double		m_cxnDelay;
  ZiIP			m_localIP;
  unsigned		m_localPort;
  ZiIP			m_remoteIP;
  unsigned		m_remotePort;
  ZiIP			m_srcIP;
  unsigned		m_srcPort;
  bool			m_listening;
  ZtString<>		m_tag;
  unsigned		m_reconnectFreq;
};

struct ListenerPrintIn {
  ListenerPrintIn(const Listener &l_) : l(l_) { }
  template <typename S> void print(S &s) const { l.printIn_(s); }
  friend ZuPrintFn ZuPrintType(ListenerPrintIn *);
  const Listener &l;
};
struct ListenerPrintOut {
  ListenerPrintOut(const Listener &l_) : l(l_) { }
  template <typename S> void print(S &s) const { l.printOut_(s); }
  friend ZuPrintFn ZuPrintType(ListenerPrintOut *);
  const Listener &l;
};
ListenerPrintIn Listener::printIn() const {
  return ListenerPrintIn(*this);
}
ListenerPrintOut Listener::printOut() const {
  return ListenerPrintOut(*this);
}

bool validateTag(ZuCSpan s) {
  const char *t = s.data();
  return !(s.length() < 2 || !*t || *t != '#');
}

template <typename S>
void parseAddr(const S &s, ZiIP &ip, uint16_t &port) {
  ZtRegexCaptures(c, 0);
  if (!s) {
    ip = ZiIP();
    port = 0;
  } else if (ZtREGEX(":").m(s, c)) {
    ip = c[0];
    port = ZuBox<unsigned>(c[2]);
  } else if (ZtREGEX("\D").m(s)) {
    ip = s;
    port = 0;
  } else {
    ip = ZiIP();
    port = ZuBox<unsigned>(s);
  }
}

#ifdef _MSC_VER
#pragma warning(disable:4800)
#endif

ZtEnumNS(Side, int8_t, In, Out, Both);

ZtEnumNS(IOOp, int8_t, Send, Recv, Both);

class App : public ZmPolymorph, public ZcmdHost {

  class Mx : public ZuObject, public ZiMultiplex {
  public:
    Mx() : ZiMultiplex{ZvMxParams{}} { }
    Mx(const ZvCf *cf) : ZiMultiplex{ZvMxParams{"zproxy", cf}} { }
  };

  ZuDerive(ListenerHash,
    (ZmHash<ZmRef<Listener>,
      ZmHashKey<Listener::LocalPortAxor>>));

  ZuDerive(ProxyHash,
    (ZmHash<ZmRef<Proxy>,
      ZmHashKey<Proxy::SrcPortAxor>>));

public:
  App() : m_verbose(false) {
    m_listeners = new ListenerHash(ZmHashParams().bits(4).loadFactor(1.0));
    m_proxies = new ProxyHash(ZmHashParams().bits(8).loadFactor(1.0));
  }

  void init(const ZvCf *cf) {
    // cf->set("mx:debug", "1");
    ZcmdHost::init();
    m_mx = new Mx(cf->getCf("mx"));
    m_verbose = cf->getBool("verbose", 0);
    addCmd("proxy",
	"tag { param tag } "
	"suspend { flag suspend } "
	"hold { flag hold } "
	"trace { flag trace } "
	"drop { flag drop } "
	"latency { param latency } "
	"frag { param frag } "
	"pack { param pack } "
	"delay { param delay } "
	"reconnect { param reconnect }",
	ZcmdFn{this, ZmFnPtr<&App::proxyCmd>{}},
	"establish TCP proxy",
	"Usage: proxy [LOCALIP:]LOCALPORT [REMOTEIP:]REMOTEPORT "
	    "[[SRCIP:][SRCPORT]] [OPTION]...\n\n"
	    "Options:\n"
	    "  --tag=TAG\t- apply name tag (\"#default\" if unspecified)\n"
	    "  --suspend\t- suspend I/O initially\n"
	    "  --hold\t- hold connections open until released\n"
	    "  --trace\t- hex dump traffic\n"
	    "  --drop\t- drop (discard) incoming traffic\n"
	    "  --latency=N\t- introduce latency of N seconds\n"
	    "  --frag=N\t- fragment packets into N fragments\n"
	    "  --pack=N\t- consolidate packets into N bytes\n"
	    "  --delay=N\t- delay each receive by N seconds\n"
	    "  --reconnect=N\t- retry connect every N seconds (0 - disabled)");
    addCmd("stop", "",
	ZcmdFn{this, ZmFnPtr<&App::stopListeningCmd>{}},
	"stop listening (do not disconnect open connections)",
	"Usage: stop #TAG|LOCALPORT");
    addCmd("hold", "",
	ZcmdFn{this, ZmFnPtr<&App::holdCmd>{}},
	"hold [one side] open",
	"Usage: hold SRCPORT|#TAG|all [in|out]");
    addCmd("release", "",
	ZcmdFn{this, ZmFnPtr<&App::releaseCmd>{}},
	"release [one side], permit disconnect\n"
	"Note: remote-initiated disconnects always occur regardless",
	"Usage: release SRCPORT|#TAG|all [in|out]");
    addCmd("disc", "",
	ZcmdFn{this, ZmFnPtr<&App::discCmd>{}},
	"disconnect SRCPORT",
	"disc SRCPORT|#TAG|all");
    addCmd("suspend", "",
	ZcmdFn{this, ZmFnPtr<&App::suspendCmd>{}},
	"suspend I/O",
	"Usage: suspend SRCPORT|#TAG|all [in|out [send|recv]]");
    addCmd("resume", "",
	ZcmdFn{this, ZmFnPtr<&App::resumeCmd>{}},
	"resume I/O",
	"resume SRCPORT|#TAG|all [in|out [send|recv]]");
    addCmd("trace", "",
	ZcmdFn{this, ZmFnPtr<&App::traceCmd>{}},
	"hex dump traffic (0 - off, 1 - on)",
	"trace SRCPORT|#TAG|all [0|1 [in|out]]");
    addCmd("drop", "",
	ZcmdFn{this, ZmFnPtr<&App::dropCmd>{}},
	"drop (discard) incoming traffic (0 - off, 1 - on)",
	"drop SRCPORT|#TAG|all [0|1 [in|out]]");
    addCmd("verbose", "",
	ZcmdFn{this, ZmFnPtr<&App::verboseCmd>{}},
	"log connection setup and teardown (0 - off, 1 - on)",
	"verbose 0|1");
    addCmd("status", "",
	ZcmdFn{this, ZmFnPtr<&App::statusCmd>{}},
	"list listeners and open connections (including queue sizes)",
	"status [#TAG]");
    addCmd("quit", "",
	ZcmdFn{this, ZmFnPtr<&App::quitCmd>{}},
	"shutdown and exit", "");
  }
  void final() {
    ZcmdHost::final();
    m_listeners->clean();
    m_proxies->clean();
  }

  Mx *mx() const { return m_mx; }
  bool verbose() const { return m_verbose; }

  int start() {
    return m_mx->start() ? Zi::OK : Zi::IOError;
  }

  void stop() {
    m_mx->stop();
  }

  void wait() { m_done.wait(); }
  void post() { m_done.post(); }

  int exec(ZtString<> cmd) {
    if (!cmd) return 0;
    ZtArray<ZtString<>> args = ZvCf::parseCLI(cmd);
    if (!args) return 0;
    ZcmdContext ctx{.host = this, .interactive = true};
    processCmd(&ctx, args);
    m_executed.wait();
    return ctx.code;
  }

  using ZcmdHost::executed;
  void executed(ZcmdContext *ctx) {
    if (const auto &out = ctx->out)
      fwrite(out.data(), 1, out.length(), stdout);
    fflush(stdout);
    m_executed.post();
  }

  void add(Proxy *proxy) {
    m_proxies->add(proxy);
  }
  void del(Proxy *proxy) {
    m_proxies->del(Proxy::SrcPortAxor(proxy));
  }

  void proxyCmd(ZcmdContext *ctx) {
    const auto &args = ctx->args;
    auto &out = ctx->out;
    ZmRef<Listener> listener;
    ZiIP localIP, remoteIP, srcIP;
    ZuCSpan tag;
    uint16_t localPort, remotePort, srcPort;
    uint32_t cxnFlags = 0;
    double cxnLatency = 0;
    uint32_t cxnFrag = 0;
    uint32_t cxnPack = 0;
    double cxnDelay = 0;
    unsigned reconnectFreq = 1;
    try {
      parseAddr(args->get("1"), localIP, localPort);
      if (!localPort) throw ZeError();
      parseAddr(args->get("2"), remoteIP, remotePort);
      if (!remotePort) throw ZeError();
      if (!remoteIP) remoteIP = "127.0.0.1";
      parseAddr(args->get("3"), srcIP, srcPort);
      tag = args->get("tag");
      if (tag) {
	if (!validateTag(tag)) throw ZeError();
      } else
	tag = "#default";
      if (args->getBool("suspend"))
	cxnFlags |= Connection::SuspRecv | Connection::SuspSend;
      if (args->getBool("hold"))
	cxnFlags |= Connection::Hold;
      if (args->getBool("trace"))
	cxnFlags |= Connection::Trace;
      if (args->getBool("drop"))
	cxnFlags |= Connection::Drop;
      cxnLatency = args->getDbl("latency", 0, 3600, 0);
      cxnFrag = args->getInt("frag", INT_MIN, INT_MAX, 0);
      cxnPack = args->getInt("pack", INT_MIN, INT_MAX, 0);
      cxnDelay = args->getDbl("delay", 0, 3600, 0);
      reconnectFreq = args->getInt("reconnect", 1, 3600, 1);
    } catch (...) {
      throw ZcmdUsage();
    }
    if (m_listeners->findVal(localPort)) {
      out << "already listening on port " << ZuBoxed(localPort) << '\n';
      executed(1, ctx);
      return;
    }
    listener = new Listener(
	this, cxnFlags, cxnLatency, cxnFrag, cxnPack, cxnDelay,
	localIP, localPort, remoteIP, remotePort, srcIP, srcPort, tag,
	reconnectFreq);
    int code = 0;
    if (listener->start() == Zi::OK)
      m_listeners->add(listener);
    else
      code = 1;
    out << listener->status() << '\n';
    executed(code, ctx);
  }

  void stopListeningCmd(ZcmdContext *ctx) {
    const auto &args = ctx->args;
    auto &out = ctx->out;
    ZuCSpan tag;
    unsigned localPort;
    bool isTag = false;
    try {
      tag = args->get("1");
      if (validateTag(tag))
        isTag = true;
      else
        localPort = args->getInt<true>("1", 1, 65535);
    } catch (...) {
      throw ZcmdUsage();
    }
    if (isTag) {
      auto i = m_listeners->iter();
      while (ZmRef<Listener> listener = i.val()) {
        if (listener->tag() != tag) continue;
        listener->stop();
        m_listeners->del(listener->localPort());
        out << listener->status() << '\n';
      }
      statusCmd(ctx);
      return;
    }
    ZmRef<Listener> listener = m_listeners->findVal(localPort);
    if (!listener) {
      out << "no listener on port " << ZuBoxed(localPort) << '\n';
      executed(1, ctx);
      return;
    }
    listener->stop();
    out << listener->status() << '\n';
    m_listeners->del(localPort);
    executed(0, ctx);
  }

  void holdCmd(ZcmdContext *ctx) {
    const auto &args = ctx->args;
    auto &out = ctx->out;
    unsigned srcPort;
    int side;
    bool allProxies = false, isTag = false;
    ZuCSpan tag;
    try {
      tag = args->get("1");
      if (validateTag(tag))
        isTag = true;
      else if (args->get("1") == "all")
	allProxies = true;
      else
	srcPort = args->getInt<true>("1", 1, 65535);
      side = args->getEnum<Side::Map, int>("2", Side::Both);
    } catch (...) {
      throw ZcmdUsage();
    }
    if (allProxies || isTag) {
      auto i = m_proxies->citer();
      while (ZmRef<Proxy> proxy = i.val()) {
	ZmRef<Connection> connection;
        if (isTag && proxy->tag() != tag)
          continue;
	if ((side == Side::In || side == Side::Both) &&
	    (connection = proxy->in()))
	  connection->hold();
	if ((side == Side::Out || side == Side::Both) &&
	    (connection = proxy->out()))
	  connection->hold();
      }
      statusCmd(ctx);
      return;
    }
    ZmRef<Proxy> proxy = m_proxies->findVal(srcPort);
    if (!proxy) {
      out << "no proxy on source port " << ZuBoxed(srcPort) << '\n';
      executed(1, ctx);
      return;
    }
    ZmRef<Connection> connection;
    if ((side == Side::In || side == Side::Both) &&
	(connection = proxy->in()))
      connection->hold();
    if ((side == Side::Out || side == Side::Both) &&
	(connection = proxy->out()))
      connection->hold();
    out << proxy->status() << '\n';
    executed(0, ctx);
  }

  void releaseCmd(ZcmdContext *ctx) {
    const auto &args = ctx->args;
    auto &out = ctx->out;
    ZuCSpan tag;
    unsigned srcPort;
    int side;
    bool isTag = false, allProxies = false;
    try {
      tag = args->get("1");
      if (validateTag(tag))
        isTag = true;
      else if (args->get("1") == "all")
	allProxies = true;
      else
	srcPort = args->getInt<true>("1", 1, 65535);
      side = args->getEnum<Side::Map, int>("2", Side::Both);
    } catch (...) {
      throw ZcmdUsage();
    }
    if (allProxies || isTag) {
      auto i = m_proxies->citer();
      while (ZmRef<Proxy> proxy = i.val()) {
	ZmRef<Connection> connection;
        if (isTag && proxy->tag() != tag)
          continue;
	if ((side == Side::In || side == Side::Both) &&
	    (connection = proxy->in()))
	  connection->release();
	if ((side == Side::Out || side == Side::Both) &&
	    (connection = proxy->out()))
	  connection->release();
      }
      statusCmd(ctx);
      return;
    } else {
      ZmRef<Proxy> proxy = m_proxies->findVal(srcPort);
      if (!proxy) {
	out << "no proxy on source port " << ZuBoxed(srcPort) << '\n';
	executed(1, ctx);
	return;
      }
      ZmRef<Connection> connection;
      if ((side == Side::In || side == Side::Both) &&
	  (connection = proxy->in()))
	connection->release();
      if ((side == Side::Out || side == Side::Both) &&
	  (connection = proxy->out()))
	connection->release();
      out << proxy->status() << '\n';
    }
    executed(0, ctx);
  }

  void discCmd(ZcmdContext *ctx) {
    const auto &args = ctx->args;
    auto &out = ctx->out;
    ZuCSpan tag;
    unsigned srcPort;
    bool isTag = false, allProxies = false;
    try {
      tag = args->get("1");
      if (validateTag(tag))
        isTag = true;
      else if (args->get("1") == "all")
	allProxies = true;
      else
	srcPort = args->getInt<true>("1", 1, 65535);
    } catch (...) {
      throw ZcmdUsage();
    }
    if (allProxies || isTag) {
      auto i = m_proxies->citer();
      while (ZmRef<Proxy> proxy = i.val()) {
	ZmRef<Connection> connection;
        if (isTag && proxy->tag() != tag)
          continue;
	if (connection = proxy->in()) connection->disconnect();
	if (connection = proxy->out()) connection->disconnect();
      }
      statusCmd(ctx);
      return;
    } else {
      ZmRef<Proxy> proxy = m_proxies->findVal(srcPort);
      if (!proxy) {
	out << "no proxy on source port " << ZuBoxed(srcPort) << '\n';
	executed(1, ctx);
	return;
      }
      ZmRef<Connection> connection;
      if (connection = proxy->in()) connection->disconnect();
      if (connection = proxy->out()) connection->disconnect();
      out << proxy->status() << '\n';
    }
    executed(0, ctx);
  }

  void suspendCmd(ZcmdContext *ctx) {
    const auto &args = ctx->args;
    auto &out = ctx->out;
    ZuCSpan tag;
    unsigned srcPort;
    int side, op;
    bool isTag, allProxies = false;
    try {
      tag = args->get("1");
      if (validateTag(tag))
        isTag = true;
      else if (args->get("1") == "all")
	allProxies = true;
      else
	srcPort = args->getInt<true>("1", 1, 65535);
      side = args->getEnum<Side::Map, int>("2", Side::Both);
      op = args->getEnum<IOOp::Map, int>("3", IOOp::Both);
    } catch (...) {
      throw ZcmdUsage();
    }
    if (allProxies || isTag) {
      auto i = m_proxies->citer();
      while (ZmRef<Proxy> proxy = i.val()) {
	ZmRef<Connection> connection;
        if (isTag && proxy->tag() != tag)
          continue;
	if ((side == Side::In || side == Side::Both) &&
	    (connection = proxy->in())) {
	  if (op == IOOp::Send || op == IOOp::Both)
	    connection->suspSend();
	  if (op == IOOp::Recv || op == IOOp::Both)
	    connection->suspRecv();
	}
	if ((side == Side::Out || side == Side::Both) &&
	    (connection = proxy->out())) {
	  if (op == IOOp::Send || op == IOOp::Both)
	    connection->suspSend();
	  if (op == IOOp::Recv || op == IOOp::Both)
	    connection->suspRecv();
	}
      }
      statusCmd(ctx);
      return;
    } else {
      ZmRef<Proxy> proxy = m_proxies->findVal(srcPort);
      if (!proxy) {
	out << "no proxy on source port " << ZuBoxed(srcPort) << '\n';
	executed(1, ctx);
	return;
      }
      ZmRef<Connection> connection;
      if ((side == Side::In || side == Side::Both) &&
	  (connection = proxy->in())) {
	if (op == IOOp::Send || op == IOOp::Both) connection->suspSend();
	if (op == IOOp::Recv || op == IOOp::Both) connection->suspRecv();
      }
      if ((side == Side::Out || side == Side::Both) &&
	  (connection = proxy->out())) {
	if (op == IOOp::Send || op == IOOp::Both) connection->suspSend();
	if (op == IOOp::Recv || op == IOOp::Both) connection->suspRecv();
      }
      out << proxy->status() << '\n';
    }
    executed(0, ctx);
  }

  void resumeCmd(ZcmdContext *ctx) {
    const auto &args = ctx->args;
    auto &out = ctx->out;
    ZuCSpan tag;
    unsigned srcPort;
    int side, op;
    bool isTag = false, allProxies = false;
    try {
      tag = args->get("1");
      if (validateTag(tag))
        isTag = true;
      else if (args->get("1") == "all")
	allProxies = true;
      else
	srcPort = args->getInt<true>("1", 1, 65535);
      side = args->getEnum<Side::Map, int>("2", Side::Both);
      op = args->getEnum<IOOp::Map, int>("3", IOOp::Both);
    } catch (...) {
      throw ZcmdUsage();
    }
    if (allProxies || isTag) {
      auto i = m_proxies->citer();
      while (ZmRef<Proxy> proxy = i.val()) {
	ZmRef<Connection> connection;
        if (isTag && proxy->tag() != tag)
          continue;
	if ((side == Side::In || side == Side::Both) &&
	    (connection = proxy->in())) {
	  if (op == IOOp::Send || op == IOOp::Both) connection->resSend();
	  if (op == IOOp::Recv || op == IOOp::Both) connection->resRecv();
	}
	if ((side == Side::Out || side == Side::Both) &&
	    (connection = proxy->out())) {
	  if (op == IOOp::Send || op == IOOp::Both) connection->resSend();
	  if (op == IOOp::Recv || op == IOOp::Both) connection->resRecv();
	}
      }
      statusCmd(ctx);
      return;
    } else {
      ZmRef<Proxy> proxy = m_proxies->findVal(srcPort);
      if (!proxy) {
	out << "no proxy on source port " << ZuBoxed(srcPort) << '\n';
	executed(1, ctx);
	return;
      }
      ZmRef<Connection> connection;
      if ((side == Side::In || side == Side::Both) &&
	  (connection = proxy->in())) {
	if (op == IOOp::Send || op == IOOp::Both) connection->resSend();
	if (op == IOOp::Recv || op == IOOp::Both) connection->resRecv();
      }
      if ((side == Side::Out || side == Side::Both) &&
	  (connection = proxy->out())) {
	if (op == IOOp::Send || op == IOOp::Both) connection->resSend();
	if (op == IOOp::Recv || op == IOOp::Both) connection->resRecv();
      }
      out << proxy->status() << '\n';
    }
    executed(0, ctx);
  }

  void traceCmd(ZcmdContext *ctx) {
    const auto &args = ctx->args;
    auto &out = ctx->out;
    ZuCSpan tag;
    unsigned srcPort;
    int side;
    bool on;
    bool isTag = false, allProxies = false;
    try {
      tag = args->get("1");
      if (validateTag(tag))
        isTag = true;
      else if (args->get("1") == "all")
	allProxies = true;
      else
	srcPort = args->getInt<true>("1", 1, 65535);
      on = args->getBool("2", true);
      side = args->getEnum<Side::Map, int>("3", Side::Both);
    } catch (...) {
      throw ZcmdUsage();
    }
    if (allProxies || isTag) {
      auto i = m_proxies->citer();
      while (ZmRef<Proxy> proxy = i.val()) {
	ZmRef<Connection> connection;
        if (isTag && proxy->tag() != tag)
          continue;
	if ((side == Side::In || side == Side::Both) &&
	    (connection = proxy->in()))
	  connection->trace(on);
	if ((side == Side::Out || side == Side::Both) &&
	    (connection = proxy->out()))
	  connection->trace(on);
      }
      statusCmd(ctx);
      return;
    } else {
      ZmRef<Proxy> proxy = m_proxies->findVal(srcPort);
      if (!proxy) {
	out << "no proxy on source port " << ZuBoxed(srcPort) << '\n';
	executed(1, ctx);
	return;
      }
      ZmRef<Connection> connection;
      if ((side == Side::In || side == Side::Both) &&
	  (connection = proxy->in()))
	connection->trace(on);
      if ((side == Side::Out || side == Side::Both) &&
	  (connection = proxy->out()))
	connection->trace(on);
      out << proxy->status() << '\n';
    }
    executed(0, ctx);
  }

  void dropCmd(ZcmdContext *ctx) {
    const auto &args = ctx->args;
    auto &out = ctx->out;
    ZuCSpan tag;
    unsigned srcPort;
    int side;
    bool on;
    bool isTag = false, allProxies = false;
    try {
      tag = args->get("1");
      if (validateTag(tag))
        isTag = true;
      else if (args->get("1") == "all")
	allProxies = true;
      else
	srcPort = args->getInt<true>("1", 1, 65535);
      on = args->getBool("2", true);
      side = args->getEnum<Side::Map, int>("3", Side::Both);
    } catch (...) {
      throw ZcmdUsage();
    }
    if (allProxies || isTag) {
      auto i = m_proxies->citer();
      while (ZmRef<Proxy> proxy = i.val()) {
	ZmRef<Connection> connection;
        if (isTag && proxy->tag() != tag)
          continue;
	if ((side == Side::In || side == Side::Both) &&
	    (connection = proxy->in()))
	  connection->drop(on);
	if ((side == Side::Out || side == Side::Both) &&
	    (connection = proxy->out()))
	  connection->drop(on);
      }
      statusCmd(ctx);
      return;
    } else {
      ZmRef<Proxy> proxy = m_proxies->findVal(srcPort);
      if (!proxy) {
	out << "no proxy on source port " << ZuBoxed(srcPort) << '\n';
	executed(1, ctx);
	return;
      }
      ZmRef<Connection> connection;
      if ((side == Side::In || side == Side::Both) &&
	  (connection = proxy->in()))
	connection->drop(on);
      if ((side == Side::Out || side == Side::Both) &&
	  (connection = proxy->out()))
	connection->drop(on);
      out << proxy->status() << '\n';
    }
    executed(0, ctx);
  }

  void verboseCmd(ZcmdContext *ctx) {
    const auto &args = ctx->args;
    auto &out = ctx->out;
    bool on;
    try {
      on = args->getBool("1", true);
    } catch (...) {
      throw ZcmdUsage();
    }
    m_verbose = on;
    out = on ? "verbose on\n" : "verbose off\n";
    executed(0, ctx);
  }

  void statusCmd(ZcmdContext *ctx) {
    const auto &args = ctx->args;
    auto &out = ctx->out;
    ZuCSpan tag;
    bool isTag = false;
    try {
      tag = args->get("1");
      isTag = validateTag(tag);
    } catch (...) {
      throw ZcmdUsage();
    }
    {
      auto i = m_listeners->iter();
      while (ZmRef<Listener> listener = i.val()) {
        if (isTag && listener->tag() != tag)
          continue;
	if (out.length()) out << '\n';
	out << listener->status();
      }
    }
    {
      auto i = m_proxies->citer();
      while (ZmRef<Proxy> proxy = i.val()) {
	if (out.length()) out << '\n';
	out << proxy->status();
      }
    }
    executed(0, ctx);
  }

  void quitCmd(ZcmdContext *ctx) {
    auto &out = ctx->out;
    post();
    out << "shutting down\n";
    executed(0, ctx);
  }

private:
  ZmRef<Mx>		m_mx;

  ZmSemaphore		m_done;
  ZmSemaphore		m_executed;

  ZmRef<ListenerHash>	m_listeners;
  ZmRef<ProxyHash>	m_proxies;
  bool			m_verbose;
};

void IOBuf::recv(ZiIOContext *io) {
  if (!io)
    m_connection->ZiConnection::recv(
	ZiIOFn{ZmMkRef(this), ZmFnPtr<&IOBuf::recv_>{}});
  else
    recv_(*io);
}
bool IOBuf::recv_(ZiIOContext &io)
{
  io.init(ZiIOFn{ZmMkRef(this), ZmFnPtr<&IOBuf::rcvd_>{}},
      m_buf.data(), m_buf.size(), 0);
  return true;
}
bool IOBuf::rcvd_(ZiIOContext &io)
{
  m_buf.length(io.offset += io.length);
  m_stamp = Zm::now();
  m_connection->recv_(this, io);
  return true;
}

void IOBuf::send(ZiIOContext *io)
{
  if (!io)
    m_connection->ZiConnection::send(
	ZiIOFn{ZmMkRef(this), ZmFnPtr<&IOBuf::send_>{}});
  else
    send_(*io);
}
bool IOBuf::send_(ZiIOContext &io)
{
  io.init(ZiIOFn{ZmMkRef(this), ZmFnPtr<&IOBuf::sent_>{}},
      m_buf.data(), m_buf.length(), 0);
  return true;
}
bool IOBuf::sent_(ZiIOContext &io)
{
  if ((io.offset += io.length) >= io.size)
    m_connection->send_(this, io);
  return true;
}

Connection::Connection(Proxy *proxy, uint32_t flags,
    double latency, uint32_t frag, uint32_t pack, double delay,
    const ZiCxnInfo &ci) :
  ZiConnection{proxy->mx(), ci},
  m_mx{proxy->mx()}, m_proxy{proxy}, m_peer{nullptr},
  m_flags{flags}, m_latency{latency},
  m_frag{frag}, m_pack{pack}, m_delay{delay}
{
}

void Connection::connected(ZiIOContext &io)
{
  io.complete();
  m_mx->run([this]() { connected_(); });
}

void Connection::connected_()
{
  if (ZmRef<Proxy> proxy = m_proxy) proxy->connected(this);
}

void Connection::disconnected(bool)
{
  if (m_peer && m_peer->up() && !(m_peer->m_flags & Hold)) {
    if (m_latency) {
      // double the latency to avoid overtaking pending delayed sends
      ZuTime next = Zm::now(m_latency * ZuDecimal{2});
      m_mx->add(&m_discTimer, next, ZmScheduler::Update,
	  [this](auto &&arm) {
	    return arm([peer = ZmMkRef(m_peer)]() { peer->disconnect(); });
	  });
    } else
      m_peer->disconnect();
  }
  if (ZmRef<Proxy> proxy = m_proxy) proxy->disconnected(this);
}

void Connection::recv() { recv(0); }

void Connection::recv(ZiIOContext *io)
{
  if (ZuUnlikely(m_flags & SuspRecv)) {
    if (io) io->complete();
    return;
  }

  if (ZuUnlikely(m_delay)) {
    if (io) io->complete();
    m_mx->add(&m_recvTimer, Zm::now(m_delay), ZmScheduler::Update,
	[this](auto &&arm) { return arm([this]() { recv(); }); });
    return;
  }

  ZmRef<IOBuf> ioBuf = new IOBuf(this);
  ioBuf->recv(io);
}

void Connection::recv_(ZmRef<IOBuf> ioBuf, ZiIOContext &io)
{
  if (m_flags & Trace) {
    ZiLOG(Info, "zproxy",
	(ZtHexDump{ZtString<>{} << *this, ioBuf->data(), ioBuf->length()}));
  }

  if (!(m_flags & Drop)) m_peer->send(ZuMv(ioBuf));

  recv(&io);
}

void Connection::send(ZmRef<IOBuf> ioBuf)
{
  Guard guard(m_lock);

  if (uint32_t frag = m_frag) {
    if (!(frag = ioBuf->length() / frag)) frag = 1;
    while (ioBuf->length() > frag) {
      ZmRef<IOBuf> ioBuf_ = new IOBuf(this, ioBuf->stamp());
      ioBuf->splice(ioBuf_->buf(), 0, frag);
      m_queue.push(ZuMv(ioBuf_));
    }
  }
    
  if (ioBuf->length()) m_queue.push(ZuMv(ioBuf));

  if (m_sendPending) return;

  if (m_latency) {
    ZuTime now = Zm::now();
    ZuTime next = m_queue.tail()->stamp() + m_latency;
    if (next > now) {
      m_sendPending = true;
      m_mx->add(&m_sendTimer, next, ZmScheduler::Update,
	  [this](auto &&arm) { return arm([this]() { delayedSend(); }); });
      return;
    }
  }

  send(guard, 0);
}

void Connection::delayedSend()
{
  Guard guard(m_lock);
  m_sendPending = false;
  send(guard, 0);
}

void Connection::send() { send(0); }

void Connection::send(ZiIOContext *io)
{
  Guard guard(m_lock);
  send(guard, io);
}

void Connection::send(Guard &guard, ZiIOContext *io)
{
  if (m_sendPending) return;

  if (m_flags & SuspSend) return;

  ZmRef<IOBuf> ioBuf = m_queue.shift();

  if (!ioBuf) return;

  if (m_pack)
    while (ioBuf->length() < m_pack)
      if (ZmRef<IOBuf> ioBuf_ = m_queue.shift()) {
	unsigned length = m_pack - ioBuf->length();
	if (length > ioBuf_->length()) length = ioBuf_->length();
	ioBuf->append(ioBuf_->data(), length);
	if (length < ioBuf_->length()) {
	  ioBuf_->splice(0, length);
	  m_queue.push(ZuMv(ioBuf_));
	  break;
	}
      } else
	break;

  m_sendPending = true;

  ioBuf->connection(this);

  ioBuf->send(io);
}

void Connection::send_(IOBuf *ioBuf, ZiIOContext &io)
{
  if (m_flags & Trace) {
    ZiLOG(Info, "zproxy",
	(ZtHexDump{ZtString<>{} << *this, ioBuf->data(), ioBuf->length()}));
  }

  Guard guard(m_lock);

  m_sendPending = false;

  if (m_flags & SuspSend) { io.complete(); return; }

  send(guard, &io);
}

void Connection::release()
{
  m_flags &= ~Hold;
  if (!up()) {
    if (m_peer && m_peer->up()) m_peer->disconnect();
    if (ZmRef<Proxy> proxy = m_proxy) proxy->disconnected(this);
  } else {
    if (m_peer && !m_peer->up()) disconnect();
  }
}

unsigned Proxy::SrcPortAxor(Proxy *proxy)
{
  if (!proxy->m_out) return 0;
  return proxy->m_out->info().localPort;
}

Proxy::Proxy(Listener *listener) :
    m_mx(listener->mx()), m_app(listener->app()), m_listener(listener),
    m_tag(listener->tag())
{
}

void Proxy::connected(Connection *connection)
{
  if (connection->flags() & Connection::In) {
    m_in = connection;
    connect2();
  } else {
    m_out = connection;
    m_app->add(this);
    m_listener->del(this);
    if (m_app->verbose()) { ZiLOG(Info, "zproxy", status()); }
    m_in->peer(m_out);
    m_out->peer(m_in);
    m_in->recv();
    m_out->recv();
  }
}

void Proxy::connect2()
{
  if (!m_listener) return;
  if (m_app->verbose()) { ZiLOG(Info, "zproxy", status()); }
  m_mx->connect(
      ZiConnectFn{this, ZmFnPtr<&Proxy::connected2>{}},
      ZiFailFn{this, ZmFnPtr<&Proxy::failed2>{}},
      m_listener->srcIP(), m_listener->srcPort(),
      m_listener->remoteIP(), m_listener->remotePort());
}

void Proxy::failed2(bool transient)
{
  if (transient) {
    m_mx->add(&m_connectTimer, Zm::now(m_listener->reconnectFreq()),
	ZmScheduler::Update,
	[this](auto &&arm) { return arm([this]() { connect2(); }); });
  } else {
    if (m_app->verbose()) { ZiLOG(Info, "zproxy", status()); }
    m_in->proxy(0);
    m_in->disconnect();
    m_listener->del(this);
    m_listener = 0;
  }
}

ZiConnection *Proxy::connected2(const ZiCxnInfo &ci)
{
  return new Connection(this, m_listener->cxnFlags(),
      m_listener->cxnLatency(),
      m_listener->cxnFrag(), m_listener->cxnPack(),
      m_listener->cxnDelay(), ci);
}

void Proxy::disconnected(Connection *connection)
{
  if ((!m_in || !m_in->up()) && (!m_out || !m_out->up())) {
    if (m_in) m_in->proxy(0);
    if (m_out) m_out->proxy(0);
    m_app->del(this);
    if (m_listener) {
      m_listener->del(this);
      m_listener = 0;
    }
  }
}

void Proxy::status_(ZuVStream &s) const
{
  if (ZmRef<Connection> cxn = m_in) {
    s << *cxn;
    if (ZuBoxed(cxn->latency()).fgt(0))
      s << " (latency=" << ZuBoxed(cxn->latency()) << ')';
    if (uint32_t frag = cxn->frag())
      s << " (frag=" << ZuBoxed(frag) << ')';
    if (uint32_t pack = cxn->pack())
      s << " (pack=" << ZuBoxed(pack) << ')';
    if (ZuBoxed(cxn->delay()).fgt(0))
      s << " (delay=" << ZuBoxed(cxn->delay()) << ')';
    if (uint32_t queueSize = cxn->queueSize())
      s << " (" << ZuBoxed(queueSize) << " queued)";
  } else {
    if (m_listener)
      s << m_listener->printIn();
    else
      s << "NC:NC !> NC:NC (" << m_tag << ')';
  }
  s << " =\n\t";
  if (ZmRef<Connection> cxn = m_out) {
    s << *cxn;
    if (uint32_t queueSize = cxn->queueSize())
      s << " (" << ZuBoxed(queueSize) << " queued)";
  } else {
    if (m_listener)
      s << m_listener->printOut();
    else
      s << "NC:NC !> NC:NC (" << m_tag << ')';
  }
}

Listener::Listener(App *app, uint32_t cxnFlags,
    double cxnLatency, uint32_t cxnFrag, uint32_t cxnPack, double cxnDelay,
    ZiIP localIP, unsigned localPort, ZiIP remoteIP, unsigned remotePort,
    ZiIP srcIP, unsigned srcPort, ZuCSpan tag, unsigned reconnectFreq) :
  m_mx(app->mx()), m_app(app),
  m_cxnFlags(cxnFlags), m_cxnLatency(cxnLatency),
  m_cxnFrag(cxnFrag), m_cxnPack(cxnPack), m_cxnDelay(cxnDelay),
  m_localIP(localIP), m_localPort(localPort),
  m_remoteIP(remoteIP), m_remotePort(remotePort),
  m_srcIP(srcIP), m_srcPort(srcPort), m_listening(false), m_tag(tag),
  m_reconnectFreq(reconnectFreq)
{
  m_proxies = new ProxyHash();
}

int Listener::start()
{
  m_mx->listen(
      ZiListenFn{this, ZmFnPtr<&Listener::ok>{}},
      ZiFailFn{this, ZmFnPtr<&Listener::failed>{}},
      ZiConnectFn{this, ZmFnPtr<&Listener::accepted>{}},
      m_localIP, m_localPort, 8);
  m_started.wait();
  return m_listening ? Zi::OK : Zi::IOError;
}

void Listener::ok(const ZiListenInfo &)
{
  m_listening = true;
  m_started.post();
}

void Listener::failed(bool transient)
{
  m_listening = false;
  m_started.post();
}

void Listener::stop()
{
  m_mx->stopListening(m_localIP, m_localPort);
  m_listening = false;
}

ZiConnection *Listener::accepted(const ZiCxnInfo &ci)
{
  ZmRef<Proxy> proxy = new Proxy(this);
  if (m_app->verbose()) { ZiLOG(Info, "zproxy", status()); }
  add(proxy);
  return new Connection(proxy, Connection::In | m_cxnFlags,
      m_cxnLatency, m_cxnFrag, m_cxnPack, m_cxnDelay, ci);
}

void Listener::status_(ZuVStream &s) const
{
  s << *this;
  auto i = m_proxies->citer();
  while (ZmRef<Proxy> proxy = i.val())
    s << '\n' << proxy->status();
}

ZmRef<App> app;

void sigint() { if (app) app->post(); }

int main(int argc, char **argv)
{
  ZmRef<ZvCf> options = new ZvCf{};

  options->fromString(
    "verbose v v { param verbose }\n"
    "nThreads t t { param mx.nThreads }\n");

  static const char *usage =
    "Usage: zproxy [OPTION]...\n"
    "\n"
    "Options:\n"
    "  -v, --verbose\t- log connection setup and teardown events\n"
    "  -t, --n-threads=N\t- set number of threads\n";

  bool interactive = Zrl::interactive();

  ZmRef<App> app = new App();
  ZmRef<ZvCf> args = new ZvCf();

  try {
    if (args->fromArgs(options, ZvCf::args(argc, argv)) != 1) {
      std::cerr << usage << std::flush;
      return 1;
    }
    app->init(args);
  } catch (...) {
    std::cerr << usage << std::flush;
    return 1;
  }

  ZmTrap::sigintFn(sigint);
  ZmTrap::trap();

  app->start();

  ZiLog::init("zproxy");
  ZiLog::level(0);

  if (interactive) {
    Zrl::Globber globber;
    Zrl::History history{100};
    Zrl::CLI cli;
    cli.init({
      .error = [](ZuCSpan s) { std::cerr << s << '\n'; },
      .prompt = [](ZtArray<uint8_t> &s) { if (!s) s = "zproxy] "; },
      .enter = [app = app.ptr()](ZuCSpan cmd) -> bool {
	app->exec(ZtString<>{cmd}); // ignore result code
	return false;
      },
      .sig = [app = app.ptr()](int sig) -> bool {
	switch (sig) {
	  case SIGINT:
	    app->post();
	    return true;
#ifdef _WIN32
	  case SIGQUIT:
	    GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, 0);
	    return true;
#endif
	  case SIGTSTP:
	    raise(sig);
	    return false;
	  default:
	    return false;
	}
      },
      .compInit = globber.initFn(),
      .compFinal = globber.finalFn(),
      .compStart = globber.startFn(),
      .compSubst = globber.substFn(),
      .compNext = globber.nextFn(),
      .histSave = history.saveFn(),
      .histLoad = history.loadFn()
    });
    if (cli.open()) {
      ZiLog::sink(ZiLog::lambdaSink([&cli](ZeLogBuf &buf, const ZeEventInfo &) {
	buf << '\n';
	cli.print([&buf]() { std::cout << buf << std::flush; });
      }));
      ZiLog::start();
      cli.start();
      cli.join();
      ZiLog::stop();
      cli.stop();
      cli.close();
    }
    cli.final();
  } else {
    ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
    ZiLog::start();
    ZtString<> cmd(1024);
    while (fgets(cmd.data(), cmd.size() - 1, stdin)) {
      cmd.calcLength();
      cmd.chomp();
      if (app->exec(cmd)) break;
    }
    app->wait();
    ZiLog::stop();
  }

  app->stop();

  app->final();

  return 0;
}
