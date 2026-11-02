//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <stdlib.h>
#include <signal.h>

#include <zlib/ZuTime.hh>
#include <zlib/ZmRandom.hh>
#include <zlib/ZmTrap.hh>

#include <zlib/ZtArray.hh>

#include <zlib/ZiLog.hh>

#include <zlib/ZiMultiplex.hh>

#include "global.hh"

class Mx;

const char ResponseStart[] =
  "HTTP/1.1 200 OK\r\n"
  "Date: Thu, 01 Jan 1970 09:00:00 PST\r\n"
  "Server: zimxclient\r\n"
  "Content-Type: application/octet-stream\r\n"
  "Content-Length: ";
const char ResponseEnd[] =
  "\r\n"
  "Connection: close\r\n"
  "\r\n";

class Connection : public ZiConnection {
public:
  Connection(ZiMultiplex *mx, const ZiCxnInfo &ci, ZuTime now, Zi::IOBufAllocator *allocator) :
      ZiConnection(mx, ci), m_headerLen(0), m_acceptTime(now), m_allocator(allocator) { }
  ~Connection() { }

  Mx *mx() { return (Mx *)ZiConnection::mx(); }

  void timeout() { disconnect(); }

  void disconnected(bool);

  void connected(ZiIOContext &io) {
    io.init(
      ZiIOFn{this, ZmFnPtr<&Connection::recvRequest>{}});
    m_recvTime = Zm::now();
    Global::timeInterval(0).add(m_recvTime - m_acceptTime);
  }

#include "httpheader.hh"

  // read HTTP request
  bool recvRequest(ZiIOContext &io) {
    {
      bool incomplete = !httpHeaderEnd(io.buf->data(), m_rcvd, io.buf->length);
      m_rcvd += io.buf->length;
      io.buf = nullptr;
      if (incomplete) return true;
    }
    Global::timeInterval(1).add(Zm::now() - m_recvTime);
    Global::rcvd(m_rcvd);
    io.complete();
    send(ZiIOFn{this, ZmFnPtr<&Connection::sendHeader>{}});
    return true;
  }

  // create random-length content (normally distributed, mean 16K + 8K)
  unsigned createContent(ZiIOBuf *buf) {
    int len = ZmRand::randNorm(16384, 8192) + 8192; // Note: must be signed
    if (len < 1) len = 1;
    auto data = buf->ensure(len);
    for (int i = 0; i < len - 12; i += 12)
      memcpy(data + i, "Hello World ", 12);
    if (len >= 12)
      memcpy(data + len - 12, " G'bye World", 12);
    else
      memcpy(data, (const char *)" G'bye World" + (12 - len), len);
    buf->length = len;
    return len;
  }

  // Reserve the maximum decimal length plus constant HTTP framing, then
  // generate payload and prepend framing in this final wire allocation.
  bool sendHeader(ZiIOContext &io) {
    enum { DecDigits = 10 }; // uint32_t decimal width
    enum { HeadRoom = sizeof(ResponseStart) + sizeof(ResponseEnd) - 2 + DecDigits };
    auto buf = m_allocator->alloc();
    buf->skip = HeadRoom;
    unsigned len = createContent(buf);
    ZtString<ZtStringBuiltin<DecDigits + 1>> digits;
    digits << len;
    auto header = buf->prepend(sizeof(ResponseStart) + sizeof(ResponseEnd) - 2 + digits.length());
    memcpy(header, ResponseStart, sizeof(ResponseStart) - 1);
    header += sizeof(ResponseStart) - 1;
    memcpy(header, digits.data(), digits.length());
    header += digits.length();
    memcpy(header, ResponseEnd, sizeof(ResponseEnd) - 1);
    m_sendTime = Zm::now();
    io.init(ZiIOFn{this, ZmFnPtr<&Connection::sendComplete>{}}, ZuMv(buf));
    return true;
  }
  bool sendComplete(ZiIOContext &io) {
    if (!io.buf) { io.complete(); return true; }
    m_completedTime = Zm::now();
    Global::timeInterval(2).add(m_completedTime - m_sendTime);
    Global::sent(io.offset);
    io.disconnect();
    return true;
  }

private:
  int			m_headerLen;
  unsigned		m_headerState = 0;
  unsigned		m_rcvd = 0;
  ZuTime		m_acceptTime;
  ZuTime		m_recvTime;
  ZuTime		m_sendTime;
  ZuTime		m_completedTime;
  Zi::IOBufAllocator *m_allocator;
};

class Mx : public ZiMultiplex {
friend Connection;

public:
  Mx(ZiIP ip, unsigned port, unsigned nAccepts, const ZiCxnOptions &options,
      unsigned nConnections, unsigned maxSend, int reconnInterval,
      ZiMxParams params) :
    ZiMultiplex(ZuMv(params)),
    m_ip(ip), m_port(port), m_nAccepts(nAccepts), m_options(options),
    m_maxDisconnects(nConnections), m_maxSend(maxSend),
    m_reconnInterval(reconnInterval), m_nDisconnects(0) { }
  ~Mx() { }

  ZiConnection *connected(const ZiCxnInfo &ci) {
    return new Connection(this, ci, Zm::now(), m_allocator.ptr());
  }

  void disconnected(Connection *) {
    if (++m_nDisconnects >= m_maxDisconnects) Global::post();
  }

  void listening(const ZiListenInfo &) {
    std::cerr << "listening\n" << std::flush;
  }

  void failed(bool transient) {
    if (transient && m_reconnInterval > 0) {
      std::cerr << "bind to " << m_ip << ':' << ZuBoxed(m_port) <<
	" failed, retrying...\n" << std::flush;
      add(&m_reconnTimer, Zm::now(m_reconnInterval), ZmScheduler::Update,
	  [this](auto &&arm) { return arm([this]() { listen(); }); });
    } else {
      std::cerr << "listen failed\n" << std::flush;
      Global::post();
    }
  }

  void listen() {
    ZiMultiplex::listen(
	ZiListenFn{this, ZmFnPtr<&Mx::listening>{}},
	ZiFailFn{this, ZmFnPtr<&Mx::failed>{}},
	ZiConnectFn{this, ZmFnPtr<&Mx::connected>{}},
	m_ip, m_port, m_nAccepts, m_options);
  }

  unsigned maxSend() const { return m_maxSend; }

private:
  // Covers the example's 24 KiB mean response and normal variation.
  enum { TxSize = 64<<10 };
  ZmRef<Zi::IOBufAllocator> m_allocator = new ZiTxTCPBufAllocator<TxSize>;
  ZiIP			m_ip;
  unsigned		m_port;
  unsigned		m_nAccepts;
  ZiCxnOptions		m_options;
  unsigned		m_maxDisconnects;
  unsigned		m_maxSend;
  int			m_reconnInterval;
  ZmScheduler::Timer	m_reconnTimer;
  ZmAtomic<unsigned>	m_nDisconnects;
};

void Connection::disconnected(bool)
{
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
    "Usage: zimxserver [OPTION]... IP PORT\n"
    "\nOptions:\n"
    "  -t N\t- use N threads (default: 3 - Rx + Tx + Worker)\n"
    "  -c N\t- exit after N connections (default: 1)\n"
    "  -l N\t- use N listener accept queue length (default: 1)\n"
    "  -d N\t- disconnect early after sending N bytes\n"
    "  -i N\t- rebind with interval N secs (default: 1, <=0 disables)\n"
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
  int nAccepts = 1;
  int maxSend = 0;
  int reconnInterval = 1;
  ZmSchedParams schedParams;
  ZiMxParams params;

  for (int i = 1; i < argc; i++) {
    if (argv[i][0] != '-' || argv[i][2]) {
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
      case 'l':
	if ((nAccepts = atoi(argv[++i])) <= 0) usage();
	break;
      case 'd':
	if ((maxSend = atoi(argv[++i])) <= 0) usage();
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
	  params.epollQuantum(j);
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

  ZiLog::init("zimxserver");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::debugSink());
  ZiLog::start();

  Mx mx(ip, port, nAccepts, options, nConnections, maxSend,
      reconnInterval, ZuMv(params));

  ZmTrap::sigintFn(Global::post);
  ZmTrap::trap();

  if (!mx.start()) Zm::exit(1);

  mx.listen();

  Global::wait();
  mx.stop();
  dumpTimers();
  Global::dumpStats();

  ZiLog::stop();
  return 0;
}
