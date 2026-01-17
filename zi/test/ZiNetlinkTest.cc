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

#include <zlib/ZtHexDump.hh>

#include <zlib/ZiLog.hh>

#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiNetlink.hh>
#include <zlib/ZiNetlinkMsg.hh>

#include "Global.hh"

const char TestRequest[] = "netlink-ping";

void error(const char *op, int result, ZeError e)
{
  ZiLOG(Error, "ZiNetlinkTest",
      ZtSprintf("%s - %s - %s", op, Zi::ioResult(result), e.message().data()));
}

class Mx;
class Message;

class Connection : public ZiConnection {
public:
  Connection(ZiMultiplex *mx, const ZiCxnInfo &ci, ZuTime now) :
    ZiConnection(mx, ci) { }
  ~Connection() { }

  Mx *mx() { return (Mx *)ZiConnection::mx(); }

  void disconnected();

  void connected() { 
    ZiLOG(Debug, "ZiNetlinkTest", ZtSprintf("CONNECTED: familyID: %u portID: %u",
			   this->info().familyID(), 
			   this->info().portID()));

    ZiNetlinkDataAttr data(strlen(TestRequest));
    ZiNetlinkOpCodeAttr opcode(0x1401);
    ZiGenericNetlinkHdr hdr(this, 0, data.len() + opcode.size());

    ZiVec vecs[4] = {
      { ZiGenericNetlinkHdr2Vec(hdr) },
      { (void *)opcode.data_(), opcode.size() },
      { ZiNelinkDataAttr2Vec(data) },
      { (void *)TestRequest, strlen(TestRequest) }
    };
    ZiLOG(Debug, "ZiNetlinkTest", "sending netlink ping...");
    writev(ZiCompletion::Member<&Connection::sent>::fn(this),
	   vecs, 4);
    // write(ZiCompletion::Member<&Connection::sent>::fn(this), 
    // 	  (void *)TestRequest, strlen(TestRequest));
  }

  void sent(ZiConnection *connection,
      int callFlags, int status, int len, ZeError e);

  void readNetlink();
  void read2(Message *msg, int flags, int status, int len, ZeError error);

};

class Message : public ZmObject {
public:
  Message(Connection *connection) : m_connection(connection) { 
    memset(m_buf, 0xff, sizeof(m_buf));
    new ((void *)this->hdr()) ZiGenericNetlinkHdr();
    new ((void *)this->attr()) ZiNetlinkAttr();
  }

  char *data() { return m_buf; }

  ZiGenericNetlinkHdr *hdr() { 
    return (ZiGenericNetlinkHdr *)m_buf; 
  }

  ZiNetlinkAttr *attr() { 
    return (ZiNetlinkAttr *)(m_buf + sizeof(ZiGenericNetlinkHdr)); 
  }

  char *info() {
    return m_buf + sizeof(ZiGenericNetlinkHdr) + sizeof(ZiNetlinkAttr);
  }
  
  int hdrSize() {
    return sizeof(ZiGenericNetlinkHdr) + sizeof(ZiNetlinkAttr);
  }

  void read2(ZiConnection *connection,
      int flags, int status, int len, ZeError error);

  bool frame(void *&buffer, int &size, int offset, int length);

private:
  Connection	*m_connection;
  char		m_buf[sizeof(ZiGenericNetlinkHdr) + sizeof(ZiNetlinkAttr) +100];
};

class Mx : public ZiMultiplex {
friend Connection;

public:
  Mx(int nThreads, int nConnections, int nConcurrent, int maxRecv,
     int nFragments, int yield, int debug, int epollMaxFDs, int epollQuantum) :
      ZiMultiplex(ZiMxParams().scheduler(
	  ZmSchedParams().nThreads(nThreads))
#ifdef ZiMultiplex_EPoll
	.epollMaxFDs(epollMaxFDs).epollQuantum(epollQuantum)
#endif
      ),
      m_nConnections(nConnections), m_nConcurrent(nConcurrent),
      m_maxRecv(maxRecv), m_nFragments(nFragments),
      m_nDisconnects(0) {
    if (nFragments) ZiMultiplex::frag(true);
    if (yield) ZiMultiplex::yield(true);
    if (debug) ZiMultiplex::debug(true);
  }
  ~Mx() { }

  ZiConnection *connected(int status, const ZiCxnInfo &ci, ZeError e) {
    if (status != Zi::OK) {
      if (status == Zi::NotReady) return 0;
      ::error("connected", status, e);
      disconnected(0);
      return 0;
    }
    return new Connection(this, ci, Zm::now());
  }

  void disconnected(Connection *connection) {
    if (connection) {
      ZiConnection::Stats s;
      Global::stats(s);
    }
    int n = ++m_nDisconnects;
    if (n <= m_nConnections - m_nConcurrent)
      add(ZmFn<>::Member<&Mx::connect>::fn(this));
    if (n >= m_nConnections)
      Global::post();
  }

  void connect() {
    ZiCxnOptions opt; 
    opt.netlink(true); opt.familyName("MxRGW.00");
    ZiMultiplex::connect(
	ZiConnected::Member<&Mx::connected>::fn(this),
	ZiIP(), 0, ZiIP(), 0, opt);
  }

  int maxRecv() const { return m_maxRecv; }

  int nFragments() const { return m_nFragments; }

  void error(ZeError e) { ::error("error", Zi::IOError, e); }

private:
  int		m_nConnections;
  int		m_nConcurrent;
  int		m_maxRecv;
  int		m_nFragments;
  ZmAtomic<int>	m_nDisconnects;
};

void Connection::disconnected()
{
  mx()->disconnected(this);
}

void Connection::sent(ZiConnection *connection,
    int callFlags, int status, int len, ZeError e) {
  if (status != Zi::OK) {
    error("writeRequest", status, e);
    disconnect();
    return;
  }
  ZiLOG(Debug, "ZiNetlinkTest", "netlink ping sent.");
  mx()->add(ZmFn<>::Member<&Connection::readNetlink>::fn(this));
}

void Connection::readNetlink()
{
  ZmRef<Message> msg = new Message(this);
  ZiLOG(Debug, "ZiNetlinkTest", "reading netlink pong...");
  read(ZiCompletion::Member<&Message::read2>::fn(msg), msg->data(), 
       msg->hdrSize() + 100, 0,
       ZiFrameFn{msg, ZmFnPtr<&Message::frame>{}});
}

bool Message::frame(void *&buffer, int &size, int offset, int length)
{
  ZiLOG(Debug, "ZiNetlinkTest", ZtSprintf("frame: size: %d offset: %d length: %d",
			 size, offset, length));
  ZiLOG(Debug, "ZiNetlinkTest", ZtHexDump("frame", buffer, length));
  if ((size_t)length < sizeof(ZiGenericNetlinkHdr)) {
    ZiLOG(Debug, "ZiNetlinkTest", "frame: try again, didnt read header");
    return false;
  }
  ZiGenericNetlinkHdr *hdr = (ZiGenericNetlinkHdr *)buffer;
  return (uint16_t)length >= hdr->len();
}

void Message::read2(ZiConnection *connection,
    int flags, int status, int len, ZeError error)
{
  m_connection->read2(this, flags, status, len, error);
}

void Connection::read2(Message *msg, int flags, int status, 
		       int len, ZeError e)
{
  ZiLOG(Debug, "ZiNetlinkTest", ZtSprintf("read %d bytes", len));
  if (status != Zi::OK) {
    if (status == Zi::IOError) error("read2", status, e);
    return;
  }
  ZiLOG(Debug, "ZiNetlinkTest", msg->hdr()->toString());
  ZiLOG(Debug, "ZiNetlinkTest", msg->attr()->toString());
  ZiLOG(Debug, "ZiNetlinkTest", ZtHexDump("data:", msg->info(), 100 - msg->hdrSize()));
}

void usage()
{
  std::cerr <<
    "Usage: ZiMxClient [OPTION]...\n"
    "Options:\n"
    "  -h\t- Display this information\n"
    "  -t N\t- use N threads (default: 1)\n"
    "  -c N\t- exit after N connections (default: 1)\n"
    "  -r N\t- run N connections concurrently (default: 1)\n"
    "  -d N\t- disconnect early after receiving N bytes\n"
    "  -f N\t- fragment I/O into N fragments\n"
    "  -y\t- yield (context switch) on every lock acquisition\n"
    "  -v\t- enable ZiMultiplex debug\n"
    "  -m N\t- epoll - N is max number of file descriptors (default: 8)\n"
    "  -q N\t- epoll - N is epoll_wait() quantum (default: 8)\n"
    "  -a S\t- netlink - S is familyID (default: MxRGW.00)\n"
    << std::flush;
  Zm::exit(1);
}

int main(int argc, char **argv)
{
  int nThreads = 1;
  int nConnections = 1;
  int nConcurrent = 1;
  int maxRecv = -1;
  int nFragments = 0;
  int yield = 0;
  int debug = 0;
  int epollMaxFDs = 8;
  int epollQuantum = 8;

  for (int i = 1; i < argc; i++) {
    if (argv[i][0] != '-' || argv[i][2]) usage();
    switch (argv[i][1]) {
      case 'h':
	usage();
	break;
      case 't':
	if ((nThreads = atoi(argv[++i])) <= 0) usage();
	break;
      case 'c':
	if ((nConnections = atoi(argv[++i])) <= 0) usage();
	break;
      case 'r':
	if ((nConcurrent = atoi(argv[++i])) <= 0) usage();
	break;
      case 'd':
	if ((maxRecv = atoi(argv[++i])) < 0) usage();
	break;
      case 'f':
	if ((nFragments = atoi(argv[++i])) < 0) usage();
	break;
      case 'y':
	yield = 1;
	break;
      case 'v':
	debug = 1;
	break;
      case 'm':
	if ((epollMaxFDs = atoi(argv[++i])) <= 0) usage();
	break;
      case 'q':
	if ((epollQuantum = atoi(argv[++i])) <= 0) usage();
	break;
      default:
	usage();
	break;
    }
  }

  ZiLog::init("ZiNetlinkTest");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::debugSink());
  ZiLog::start();

  Mx mx(nThreads, nConnections, nConcurrent, maxRecv, nFragments, yield, debug,
	epollMaxFDs, epollQuantum);

  ZmTrap::sigintFn(Global::post);
  ZmTrap::trap();

  if (!mx.start()) {
    ZiLOG(Fatal, "ZiNetlinkTest", "multiplexer failed to start");
    Zm::exit(1);
  }
  try {
    for (int i = 0; i < nConcurrent; i++) mx.connect();
  } catch (const ZeError &e) {
    ZiLOG(Fatal, "ZiNetlinkTest", e);
    Global::post();
  } catch (...) {
    Global::post();
  }

  Global::wait();
  mx.stop();
  Global::dumpStats();

  ZiLog::stop();
  return 0;
}
