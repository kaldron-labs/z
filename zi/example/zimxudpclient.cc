//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <stdio.h>
#include <signal.h>

#include <zlib/ZuTime.hh>
#include <zlib/ZmTrap.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtRegex.hh>
#include <zlib/ZtHexDump.hh>

#include <zlib/ZiLog.hh>

#include <zlib/ZiMultiplex.hh>

#include "global.hh"

const char *Messages[] = {
  "Hello",
  "Nice to meet ya",
  "Time to go",
  "Good bye"
};

bool parseEndpoint(ZuCSpan s, ZiIP &ip, unsigned &port)
{
  ZuCSpan host, port_;
  if (s && s[0] == '[') {
    s.offset(1);
    auto close = s.find([](auto c) { return c == ']'; });
    if (close < 0) return false;
    host = s;
    host.trunc(close);
    s.offset(close + 1);
    if (!host || !s || s[0] != ':') return false;
    port_ = s;
    port_.offset(1);
  } else {
    int colon = -1;
    bool multiColon = false;
    for (unsigned i = 0, n = s.length(); i < n; ++i) {
      if (s[i] != ':') continue;
      if (colon >= 0) multiColon = true;
      colon = i;
    }
    if (colon < 0 || multiColon) return false;
    host = s;
    host.trunc(colon);
    port_ = s;
    port_.offset(colon + 1);
  }
  if (!port_) return false;
  unsigned p = ZuBox<unsigned>{port_};
  if (!p || p > 65535) return false;
  ip = host ? ZiIP{host} : ZiIP{};
  port = p;
  return true;
}

class Mx;

class Connection : public ZiConnection {
public:
  Connection(
      ZiMultiplex *mx, const ZiCxnInfo &ci, const ZiSockAddr &dest) :
    ZiConnection(mx, ci), m_counter(0), m_dest(dest) { }
  ~Connection() { }

  Mx *mx() { return (Mx *)ZiConnection::mx(); }

  void disconnected(bool) { Global::post(); }

  void connected(ZiIOContext &io) {
    recvEcho(io);
    sendEcho();
  }

  void recvEcho(ZiIOContext &io) {
    m_msg.size(strlen(Messages[index()]));
    io.init(ZiIOFn{this, ZmFnPtr<&Connection::recvComplete>{}},
	m_msg.data(), m_msg.size(), 0);
  }
  bool recvComplete(ZiIOContext &);

  void sendEcho() {
    send(ZiIOFn{this, ZmFnPtr<&Connection::sendEcho_>{}});
  }
  bool sendEcho_(ZiIOContext &io) {
    unsigned i = index();
    unsigned len = strlen(Messages[i]);
    io.init(
	ZiIOFn{this, ZmFnPtr<&Connection::sendComplete>{}},
	(void *)Messages[i], len, 0, m_dest);
    return true;
  }
  bool sendComplete(ZiIOContext &io) { io.complete(); return true; }

  unsigned index() {
    return m_counter % (sizeof(Messages) / sizeof(Messages[0]));
  }

private:
  unsigned   	m_counter;
  ZiSockAddr	m_dest;
  ZtArray<char>	m_msg;
  ZiSockAddr	m_addr;
};

class Mx : public ZiMultiplex {
public:
  Mx(ZiIP localIP, unsigned localPort, ZiIP remoteIP, unsigned remotePort,
      bool connect, const ZiCxnOptions &options, unsigned nMessages,
      ZiMxParams params) :
    ZiMultiplex(ZuMv(params)),
    m_localIP(localIP), m_localPort(localPort),
    m_remoteIP(remoteIP), m_remotePort(remotePort),
    m_connect(connect), m_options(options), m_nMessages(nMessages) { }
  ~Mx() { }

  ZiConnection *connected(const ZiCxnInfo &ci) {
    return new Connection(this, ci, m_dest);
  }

  void udp() {
    ZiIP remoteIP = m_remoteIP;
    unsigned remotePort = m_remotePort;
    m_dest = ZiSockAddr(remoteIP, remotePort);
    if (!m_connect) remoteIP = ZiIP(), remotePort = 0;
    ZiMultiplex::udp(
	ZiConnectFn{this, ZmFnPtr<&Mx::connected>{}},
	ZiFailFn{this, ZmFnPtr<&Mx::failed>{}},
	m_localIP, m_localPort, remoteIP, remotePort, m_options);
  }

	  void failed(bool transient) {
	    if (transient)
	      add(&m_retryTimer, Zm::now(1), ZmScheduler::Update,
		  [this](auto &&arm) { return arm([this]() { udp(); }); });
	    else
	      Global::post();
	  }

  unsigned nMessages() const { return m_nMessages; }

private:
  ZiIP		m_localIP;
  unsigned	m_localPort;
  ZiIP		m_remoteIP;
  unsigned	m_remotePort;
  bool		m_connect;
	  ZiCxnOptions	m_options;
	  unsigned	m_nMessages;
	  ZiSockAddr	m_dest;
	  ZmScheduler::Timer	m_retryTimer;
	};

void usage()
{
  std::cerr <<
    "Usage: zimxudpclient [OPTION]...\n\n"
    "Options:\n"
    "  -t N\t\t- use N threads (default: 3 - Rx + Tx + Worker)\n"
    "  -n N\t\t- exit after N messages (default: 1)\n"
    "  -f\t\t- fragment I/O\n"
    "  -y\t\t- yield (context switch) on every lock acquisition\n"
    "  -v\t\t- enable ZiMultiplex debug\n"
    "  -m N\t\t- epoll - N is max number of file descriptors (default: 8)\n"
    "  -q N\t\t- epoll - N is epoll_wait() quantum (default: 8)\n"
    "  -b [HOST:]PORT- bind to HOST:PORT (IPv6: [HOST]:PORT)\n"
    "  -d HOST:PORT\t- send to HOST:PORT (IPv6: [HOST]:PORT)\n"
    "  -c\t\t- connect() - filter packets received from other sources\n"
    "  -M\t\t- use multicast\n"
    "  -L\t\t- use multicast loopback\n"
    "  -D IP|N\t- multicast to interface IP or IPv6 interface index\n"
    "  -T N\t\t- multicast with TTL N\n"
    "  -G IP[/IF]\t- multicast subscribe to group IP on interface IF\n"
    "\t\t  IF is an IPv4 address or IPv6 interface index\n"
    "\t\t  IPv4 IF defaults to 0.0.0.0; IPv6 IF is required\n"
    "\t\t  -G can be specified multiple times\n"
    << std::flush;
  Zm::exit(1);
}

int main(int argc, const char *argv[])
{
  ZiIP localIP;
  unsigned localPort = 0;
  ZiIP remoteIP("127.0.0.1");
  unsigned remotePort = 27413;
  bool connect = false;
  ZiCxnOptions options;
  unsigned nMessages = 1;
  ZmSchedParams schedParams;
  ZiMxParams params;

  options.udp(true);

  for (int i = 1; i < argc; i++) {
    if (argv[i][0] != '-' || argv[i][2]) usage();
    switch (argv[i][1]) {
      case 't': {
	int j;
	if ((j = atoi(argv[++i])) <= 0) usage();
	schedParams.nThreads(j);
      } break;
      case 'n':
	if ((nMessages = atoi(argv[++i])) <= 0) usage();
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
      case 'b':
	{
	  try {
	    if (!parseEndpoint(argv[++i], localIP, localPort)) usage();
	  } catch (...) { usage(); }
	}
	break;
      case 'd':
	{
	  try {
	    if (!parseEndpoint(argv[++i], remoteIP, remotePort)) usage();
	  } catch (...) { usage(); }
	}
	break;
      case 'c':
	connect = true;
	break;
      case 'M':
	options.multicast(true);
	break;
      case 'L':
	options.loopBack(true);
	break;
      case 'D':
	{
	  ZiIP mif(argv[++i]);
	  if (!!mif) {
	    options.mif(mif);
	    break;
	  }
	  try {
	    options.mifIndex(ZuBox<unsigned>{argv[i]});
	  } catch (...) { usage(); }
	}
	break;
      case 'T':
	{
	  int ttl;
	  if ((ttl = atoi(argv[++i])) < 0) usage();
	  options.ttl(ttl);
	}
	break;
      case 'G':
	{
	  ZtRegexCaptures(c, 0);
	  try {
	    unsigned n = ZtREGEX("/").m(argv[++i], c);
	    auto addr = n ? ZiIP{c[0]} : ZiIP{argv[i]};
	    if (!addr.multicast()) usage();
	    switch (addr.type()) {
	      case ZiIPType::V4:
		{
		  ZiIP mif;
		  if (n) mif = c[1];
		  options.mreq(ZiMReq(addr, mif));
		}
		break;
	      case ZiIPType::V6:
		if (!n || !c[1].length()) usage();
		options.mreq(ZiMReq(addr, ZuBox<unsigned>{c[1]}));
		break;
	      default:
		usage();
		break;
	    }
	  } catch (...) { usage(); }
	}
	break;
      default:
	usage();
	break;
    }
  }

  ZiLog::init("zimxudpclient");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();

  Mx mx(localIP, localPort, remoteIP, remotePort, connect, options,
      nMessages, ZuMv(params));

  ZmTrap::sigintFn(Global::post);
  ZmTrap::trap();

  if (!mx.start()) Zm::exit(1);

  mx.udp();

  Global::wait();
  mx.stop();
  
  ZiLog::stop();
  return 0;
}

bool Connection::recvComplete(ZiIOContext &io)
{
  if (io.length < io.size) {
    ZiLOG(Error, "zimxudpclient", "recvEcho - short packet");
    io.disconnect();
    return true;
  }

  if (io.length != strlen(Messages[index()])) {
    ZiLOG(Error, "zimxudpclient", "recvEcho - bad packet size");
    io.disconnect();
    return true;
  }

  std::cout << ZtHexDump(
      ZtString<>{} << io.addr.ip() << ':' << ZuBoxed(io.addr.port()) << ' ' <<
      ZuCSpan(m_msg.data(), io.length), m_msg.data(), io.length);

  fflush(stdout);

  unsigned nMessages = mx()->nMessages();
  if (++m_counter >= nMessages) {
    io.disconnect();
    return true;
  }

  recvEcho(io);
  sendEcho();
  return true;
}
