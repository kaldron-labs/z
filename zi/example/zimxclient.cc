//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <stdlib.h>
#include <signal.h>
#include <iostream>

#include <zlib/ZtRegex.hh>

#include <zlib/ZuTime.hh>
#include <zlib/ZmAssert.hh>
#include <zlib/ZmTrap.hh>

#include <zlib/ZtArray.hh>

#include <zlib/ZiLog.hh>

#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiRx.hh>
#include <zlib/ZiTx.hh>
#include <zlib/ZiTxStream.hh>

#include "global.hh"

class Mx;

const char Request[] =
  "GET / HTTP/1.0\r\n"
  "User-Agent: zimxclient/1.0\r\n"
  "Accept: */*\r\n"
  "Host: localhost\r\n"
  "\r\n";

using RxAllocator = Zi::IOBufAllocator;
using TxAllocator = Zi::IOBufAllocator;

class Connection :
  public ZiConnection,
  public ZiRx<Connection, 100U << 20>,
  public ZiTx<Connection>
{
  using Rx = ZiRx<Connection, 100U << 20>;
  using Tx = ZiTx<Connection>;

  class Output : public ZiTxStream<Output> {
  public:
    Output(Connection *cxn, unsigned size) :
      ZiTxStream<Output>{
	cxn->info().mps < size ? cxn->info().mps : size, 0, 0},
      m_cxn{cxn} { }
    ~Output() { flush(); }
    ZmRef<ZiIOBuf> allocBuf_(unsigned headRoom) {
      auto buf = m_cxn->m_allocator->allocBuf(
	nullptr, 0, m_cxn->info().mps);
      if (ZuUnlikely(!buf)) return nullptr;
      buf->skip += headRoom;
      return buf;
    }
    bool sendBuf_(ZmRef<ZiIOBuf> buf, bool) {
      m_cxn->Tx::send(ZmRef<ZiTxBuf>::acquire(
        static_cast<ZiTxBuf *>(ZuMv(buf).release())));
      return true;
    }
  private:
    Connection *m_cxn;
  };
public:
  Connection(ZiMultiplex *mx, const ZiCxnInfo &ci, ZuTime now,
      RxAllocator *rxAllocator, TxAllocator *allocator) :
    ZiConnection(mx, ci), m_headerLen(0), m_connectTime(now),
    m_rxAllocator(rxAllocator), m_allocator(allocator) { }
  ~Connection() { }

  Mx *mx() { return (Mx *)ZiConnection::mx(); }
  RxAllocator *rxAllocator() const {
    return m_rxAllocator;
  }

  void disconnected(bool);

  void connected(ZiRxContext &io) {
    Rx::recv<&Connection::frame, &Connection::received>(io);
    // std::cout << Request << std::flush;
    Output out{this, m_allocator->capacity()};
    out << Request;
    if (!out.flush()) disconnect();
  }

  void sending(ZiTxContext &) {
    m_sendTime = Zm::now();
    Global::timeInterval(0).add(m_sendTime - m_connectTime);
  }

  void sent(ZmRef<ZiTxBuf>, bool ok) {
    if (!ok) { disconnect(); return; }
    Global::sent(sizeof(Request) - 1);
    Global::timeInterval(1).add(Zm::now() - m_sendTime);
  }

#include "httpheader.hh"

  int frame(const ZiRxContext &, ZiIOBuf *buf) {
    if (m_headerDone) {
      if (!buf->length) return INT_MAX;
      unsigned remaining = m_contentLen - m_contentRcvd;
      return int(buf->length < remaining ? buf->length : remaining);
    }
    bool complete = httpHeaderEnd(buf->data(), m_rcvd, buf->length - m_rcvd);
    m_rcvd = buf->length;
    return complete ? int(m_headerLen) : INT_MAX;
  }

  int received(const ZiRxContext &, ZmRef<ZiIOBuf> buf) {
    unsigned length = buf->length;
    if (!m_headerDone) {
      m_recvTime = Zm::now();
      m_header << ZuCSpan{buf->cspan()};
      ZtRegex::Captures c(3);
      int n;
      try { n = ZtREGEX("Content-Length:[[:space:]]+([0-9]+)").m(m_header, c); }
      catch (...) { n = 0; }
      if (n < 2) return -1;
      m_contentLen = ZuBox<unsigned>(c[2]);
      m_headerDone = true;
      if (m_contentLen) return int(length);
    } else {
      m_contentRcvd += length;
    }
    if (m_contentRcvd >= m_contentLen) {
      m_completedTime = Zm::now();
      Global::timeInterval(2).add(m_completedTime - m_recvTime);
      Global::rcvd(m_contentRcvd);
      disconnect();
      return 0;
    }
    return int(length);
  }

private:
  bool		m_headerDone = false;
  ZtString<>	m_header;
  unsigned	m_headerLen;
  unsigned	m_rcvd = 0;
  unsigned	m_contentLen = 0;
  unsigned	m_contentRcvd = 0;
  ZuTime	m_connectTime;
  ZuTime	m_sendTime;
  ZuTime	m_recvTime;
  ZuTime	m_completedTime;
  bool		m_notified = false;
  RxAllocator *m_rxAllocator;
  TxAllocator *m_allocator;
};

class Mx : public ZiMultiplex {
friend Connection;

public:
  Mx(ZiIP ip, unsigned port, const ZiCxnOptions &options,
      unsigned nConnections, unsigned nConcurrent,
      unsigned maxRecv, int reconnInterval,
      ZiMxParams params) :
    ZiMultiplex(ZuMv(params)),
    m_ip(ip), m_port(port), m_options(options),
    m_nConnections(nConnections), m_nConcurrent(nConcurrent),
    m_maxRecv(maxRecv), m_reconnInterval(reconnInterval),
    m_nDisconnects(0) {
  }
  ~Mx() { }

  ZiConnection *connected(const ZiCxnInfo &ci) {
    return new Connection(this, ci, Zm::now(), m_frameAllocator, m_allocator);
  }

  void disconnected(Connection *connection) {
    unsigned n = ++m_nDisconnects;
    if (n <= m_nConnections - m_nConcurrent)
      connect();
    if (n >= m_nConnections)
      Global::post();
  }

  void failed(bool transient) {
    if (transient && m_reconnInterval > 0) {
      std::cerr << "connect to " << m_ip << ':' << ZuBoxed(m_port) <<
	" failed, retrying...\n" << std::flush;
      add(&m_reconnTimer, Zm::now(m_reconnInterval), ZmScheduler::Update,
	  [this](auto &&arm) { return arm([this]() { connect(); }); });
    } else if (++m_nDisconnects >= m_nConnections) {
      std::cerr << "connect failed\n" << std::flush;
      Global::post();
    }
  }

  void connect() {
    ZiMultiplex::connect(
	ZiConnectFn{this, ZmFnPtr<&Mx::connected>{}},
	ZiFailFn{this, ZmFnPtr<&Mx::failed>{}},
	ZiIP(), 0, m_ip, m_port, m_rx, m_options);
  }

  unsigned maxRecv() const { return m_maxRecv; }

private:
  RxAllocator *m_rx = allocator<Zi::IOBufType::RxTCP, ZiRxBuf, 4096>();
  RxAllocator *m_frameAllocator = allocator<Zi::IOBufType::App, ZiRxBuf, 4096>();
  TxAllocator *m_allocator = allocator<Zi::IOBufType::TxTCP, ZiTxBuf, ZiIOBuf_DefltSize>();
  ZiIP			m_ip;
  unsigned		m_port;
  ZiCxnOptions		m_options;
  unsigned		m_nConnections;
  unsigned		m_nConcurrent;
  unsigned		m_maxRecv;
  int			m_reconnInterval;
  ZmScheduler::Timer	m_reconnTimer;
  ZmAtomic<unsigned>	m_nDisconnects;
};

void Connection::disconnected(bool)
{
  if (m_notified) return;
  m_notified = true;
  mx()->disconnected(this);
}

void dumpTimers()
{
  std::cout <<
    "connect: " << Global::timeInterval(0) << '\n' <<
    "send:    " << Global::timeInterval(1) << '\n' <<
    "recv:    " << Global::timeInterval(2) << '\n';
}

void usage()
{
  std::cerr <<
    "Usage: zimxclient [OPTION]... IP PORT\n"
    "\nOptions:\n"
    "  -t N\t- use N threads (default: 3 - Rx + Tx + Worker)\n"
    "  -c N\t- exit after N connections (default: 1)\n"
    "  -r N\t- run N connections concurrently (default: 1)\n"
    "  -d N\t- disconnect early after receiving N bytes\n"
    "  -i N\t- reconnect with interval N secs (default: 1, <=0 disables)\n"
    "  -f\t- fragment I/O\n"
    "  -y\t- yield (context switch) on every lock acquisition\n"
    "  -v\t- enable ZiMultiplex debug\n"
    "  -m N\t- epoll - N is max number of file descriptors (default: 8)\n"
    "  -q N\t- epoll - N is epoll_wait() quantum (default: 8)\n"
    "  -R N\t- receive buffer size (default: OS setting)\n"
    "  -S N\t- send buffer size (default: OS setting)\n"
    << std::flush;
  Zm::exit(1);
}

int main(int argc, char **argv)
{
  ZiIP ip;
  int port = 0;
  ZiCxnOptions options;
  int nConnections = 1;
  int nConcurrent = 1;
  int maxRecv = 0;
  int reconnInterval = 1;
  ZmSchedParams schedParams;
  ZiMxParams params;

  for (int i = 1; i < argc; i++) {
    if (argv[i][0] != '-') {
      if (!ip) {
	try {
	  ip = argv[i];
	} catch (const ZeError &e) {
	  std::cerr << argv[i] <<
	    ": IP address unresolvable (" << e.message() << ")\n";
	  Zm::exit(1);
	} catch (...) {
	  std::cerr << argv[i] <<
	    ": IP address unresolvable\n";
	  Zm::exit(1);
	}
	continue;
      }
      if (!port) { port = atoi(argv[i]); continue; }
      usage();
      break;
    }
    switch (argv[i][1]) {
      case 't': {
	int j;
	if ((j = atoi(argv[++i])) <= 0) usage();
	schedParams.nThreads(j);
      } break;
      case 'c':
	if ((nConnections = atoi(argv[++i])) <= 0) usage();
	break;
      case 'r':
	if ((nConcurrent = atoi(argv[++i])) <= 0) usage();
	break;
      case 'd':
	if ((maxRecv = atoi(argv[++i])) <= 0) usage();
	break;
      case 'i':
	reconnInterval = atoi(argv[++i]);
	break;
#ifdef ZiMultiplex_DEBUG
      case 'f':
	params.frag(true);
	break;
      case 'y':
	params.yield(true);
	break;
      case 'v':
	params.debug(true);
	break;
#endif
      case 'm':
	{
	  int j;
	  if ((j = atoi(argv[++i])) <= 0) usage();
#ifdef ZiMultiplex_EPoll
	  params.epollMaxFDs(j);
#endif
	}
	break;
      case 'q':
	{
	  int j;
	  if ((j = atoi(argv[++i])) <= 0) usage();
#ifdef ZiMultiplex_EPoll
	  params.rxQuantum(j);
#endif
	}
	break;
      case 'R':
	{
	  int j;
	  if ((j = atoi(argv[++i])) <= 0) usage();
	  params.rxBufSize(j);
	}
	break;
      case 'S':
	{
	  int j;
	  if ((j = atoi(argv[++i])) <= 0) usage();
	  params.txBufSize(j);
	}
	break;
      default:
	usage();
	break;
    }
  }
  if (!ip || !port) usage();

  ZiLog::init("zimxclient");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::debugSink());
  ZiLog::start();

  Mx mx(ip, port, options, nConnections, nConcurrent, maxRecv,
      reconnInterval, ZuMv(params));

  ZmTrap::sigintFn(Global::post);
  ZmTrap::trap();

  if (!mx.start()) Zm::exit(1);

  for (int i = 0; i < nConcurrent; i++) mx.connect();

  Global::wait();
  mx.stop();
  dumpTimers();
  Global::dumpStats();

  ZiLog::stop();

  return 0;
}
