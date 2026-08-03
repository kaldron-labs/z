//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <stdio.h>
#include <signal.h>

#include <zlib/ZtRegex.hh>

#include <zlib/ZuTime.hh>
#include <zlib/ZmTrap.hh>

#include <zlib/ZtArray.hh>

#include <zlib/ZiLog.hh>

#include <zlib/ZiMultiplex.hh>

#include "global.hh"

class Mx;

const char Request[] =
  "GET / HTTP/1.0\r\n"
  "User-Agent: zimxclient/1.0\r\n"
  "Accept: */*\r\n"
  "Host: localhost\r\n"
  "\r\n";

class Connection : public ZiConnection {
public:
  Connection(ZiMultiplex *mx, const ZiCxnInfo &ci, ZuTime now) :
      ZiConnection(mx, ci), m_headerLen(0), m_connectTime(now) { }
  ~Connection() { }

  Mx *mx() { return (Mx *)ZiConnection::mx(); }

  void disconnected(bool);

  void connected(ZiIOContext &io) {
    m_header.size(4096);
    io.init(
      ZiIOFn{this, ZmFnPtr<&Connection::recvHeader>{}},
      m_header.data(), m_header.size(), 0);
    send(ZiIOFn{this, ZmFnPtr<&Connection::sendRequest>{}});
  }

  bool sendRequest(ZiIOContext &io) {
    m_sendTime = Zm::now();
    Global::timeInterval(0).add(m_sendTime - m_connectTime);
    //fwrite(Request, 1, len, stdout); fflush(stdout);
    io.init(ZiIOFn{this, ZmFnPtr<&Connection::sendComplete>{}},
	(void *)Request, strlen(Request), 0);
    return true;
  }
  bool sendComplete(ZiIOContext &io) {
    if (io.length >= 0 && (io.offset += io.length) < io.size) return true;
    io.complete();
    Global::timeInterval(1).add(Zm::now() - m_sendTime);
    Global::sent(io.offset);
    return true;
  }

#include "httpheader.hh"

  bool recvHeader(ZiIOContext &io) {
    m_recvTime = Zm::now();
    {
      bool incomplete = !httpHeaderEnd(io.ptr, io.offset, io.length);
      io.offset += io.length;
      if (incomplete) return true; // completed reading the available data
    }
    {
      ZtRegex::Captures c(3);
      int i;

      m_header.length(m_headerLen, false);
      try {
	i = ZtREGEX("\bContent-Length:\s+(\d+)").m(m_header, c);
      } catch (...) { i = 0; }
      if (i < 2) {
	ZiLOG(Error, "zimxclient", "could not parse Content-Length");
	io.disconnect();
	return true;
      }
      //{ printf("Content-Length: %d\n", contentLength); fflush(stdout); }
      m_content.size(ZuBox<unsigned>(c[2]));
    }
    if (io.offset > m_headerLen) {
      memcpy(m_content.data(),
	  m_header.data() + m_headerLen, io.offset - m_headerLen);
      if ((io.offset - m_headerLen) >= m_content.size()) {
	contentRcvd(io.offset);
	io.disconnect();
	return true;
      }
    }
    io.init(
      ZiIOFn{this, ZmFnPtr<&Connection::recvContent>{}},
      m_content.data(), m_content.size(), io.offset - m_headerLen);
    return true;
  }

  bool recvContent(ZiIOContext &io) {
    if ((io.offset += io.length) >= io.size) {
      contentRcvd(io.offset);
      io.disconnect();
    }
    return true;
  }

  // process HTTP content
  void contentRcvd(unsigned n) {
    // { puts("Content received"); fflush(stdout); }
#if 0
    {
      fwrite(m_content, 1, m_content.size(), stdout);
      fflush(stdout);
      putchar('\n');
    }
#endif
    m_completedTime = Zm::now();
    Global::timeInterval(2).add(m_completedTime - m_recvTime);
    Global::rcvd(n);
  }

private:
  ZtArray<char>	m_header;
  unsigned	m_headerLen;
  ZtArray<char>	m_content;
  ZuTime	m_connectTime;
  ZuTime	m_sendTime;
  ZuTime	m_recvTime;
  ZuTime	m_completedTime;
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
    m_nDisconnects(0) { }
  ~Mx() { }

  ZiConnection *connected(const ZiCxnInfo &ci) {
    return new Connection(this, ci, Zm::now());
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
	ZiIP(), 0, m_ip, m_port, m_options);
  }

  unsigned maxRecv() const { return m_maxRecv; }

private:
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
	  fprintf(stderr, "%s: IP address unresolvable (%s)\n",
	      argv[i], e.message());
	  Zm::exit(1);
	} catch (...) {
	  fprintf(stderr, "%s: IP address unresolvable\n", argv[i]);
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
