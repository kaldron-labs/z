//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <stdio.h>
#include <signal.h>

#include <zlib/ZuTime.hh>
#include <zlib/ZmRandom.hh>
#include <zlib/ZmTrap.hh>

#include <zlib/ZtArray.hh>

#include <zlib/ZiLog.hh>

#include <zlib/ZiMultiplex.hh>

#include "Global.hh"

class Mx;

const char Response[] =
  "HTTP/1.1 200 OK\r\n"
  "Date: Thu, 01 Jan 1970 09:00:00 PST\r\n"
  "Server: ZiMxClient\r\n"
  "Content-Type: application/octet-stream\r\n"
  "Content-Length: %u\r\n"
  "Connection: close\r\n"
  "\r\n";

class Connection : public ZiConnection {
public:
  Connection(ZiMultiplex *mx, const ZiCxnInfo &ci, ZuTime now) :
      ZiConnection(mx, ci), m_headerLen(0), m_acceptTime(now) { }
  ~Connection() { }

  Mx *mx() { return (Mx *)ZiConnection::mx(); }

  void timeout() { disconnect(); }

  void disconnected();

  void connected(ZiIOContext &io) {
    m_request.size(4096);
    io.init(
      ZiIOFn{this, ZmFnPtr<&Connection::recvRequest>{}},
      m_request.data(), m_request.size(), 0);
    m_recvTime = Zm::now();
    Global::timeInterval(0).add(m_recvTime - m_acceptTime);
  }

#include "HttpHeader.hh"

  // read HTTP request
  bool recvRequest(ZiIOContext &io) {
    {
      bool incomplete = !httpHeaderEnd(io.ptr, io.offset, io.length);
      io.offset += io.length;
      if (incomplete) return true;
    }
    Global::timeInterval(1).add(Zm::now() - m_recvTime);
    Global::rcvd(io.offset);
    io.complete();
    send(ZiIOFn{this, ZmFnPtr<&Connection::sendHeader>{}});
    return true;
  }

  // create random-length content (normally distributed, mean 16K + 8K)
  unsigned createContent() {
    int len = ZmRand::randNorm(16384, 8192) + 8192; // Note: must be signed
    if (len < 1) len = 1;
    m_content.size(len);
    for (int i = 0; i < len - 12; i += 12)
      memcpy(m_content.data() + i, "Hello World ", 12);
    if (len >= 12)
      memcpy(m_content.data() + len - 12, " G'bye World", 12);
    else
      memcpy(m_content.data(), (const char *)" G'bye World" + (12 - len), len);
    m_content.length(len);
    return len;
  }

  // send HTTP header
  bool sendHeader(ZiIOContext &io) {
    //fwrite(m_request, 1, len, stdout); fflush(stdout);
    m_response.sprintf(Response, createContent());
    m_sendTime = Zm::now();
    io.init(ZiIOFn{this, ZmFnPtr<&Connection::sendContent>{}},
	m_response.data(), m_response.length(), 0);
    return true;
  }
  // send HTTP content
  bool sendContent(ZiIOContext &io) {
    if (io.length < 0) return sendComplete(io);
    if ((io.offset += io.length) < io.size) return true;
    io.init(ZiIOFn{this, ZmFnPtr<&Connection::sendComplete>{}},
	m_content.data(), m_content.length(), 0);
    return true;
  }
  bool sendComplete(ZiIOContext &io) {
    //{ printf("Content Length: %d\n", m_content.size()); fflush(stdout); }
    if (io.length >= 0 && (io.offset += io.length) < io.size) return true;
    m_completedTime = Zm::now();
    Global::timeInterval(2).add(m_completedTime - m_sendTime);
    Global::sent(io.size);
    io.disconnect();
    return true;
  }

private:
  ZtArray<char>		m_request;
  ZtString<>		m_response;
  int			m_headerLen;
  ZtArray<char>		m_content;
  ZuTime		m_acceptTime;
  ZuTime		m_recvTime;
  ZuTime		m_sendTime;
  ZuTime		m_completedTime;
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
    return new Connection(this, ci, Zm::now());
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

void Connection::disconnected()
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
    "Usage: ZiMxServer [OPTION]... IP PORT\n"
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

  ZiLog::init("ZiMxServer");
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
