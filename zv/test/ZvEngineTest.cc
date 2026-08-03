//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ZvEngine connectivity framework unit smoke test

#include <zlib/ZvEngine.hh>
#include <zlib/ZuTestUtil.hh>

using namespace ZuTestUtil;

// typically implemented by Ztel::Server
class Mgr : public ZvEngineMgr {
public:
  virtual ~Mgr() { }

  // Engine Management
  void addEngine(ZvEngine *) { }
  void delEngine(ZvEngine *) { }
  void updEngine(ZvEngine *engine) {
    ZiLOG(Info, "ZvEngineTest",
      ([id = engine->id(), next = engine->state()](auto &s) {
	s << "engine " << id << ' ' << ZvEngineState::name(next); }));
  }

  // Link Management
  void updLink(ZvAnyLink *link) {
    ZiLOG(Info, "ZvEngineTest", ([id = link->id(), next = link->state()](auto &s) {
      s << "link " << id << ' ' << ZvLinkState::name(next); }));
  }

  // Queue Management
  void addQueue(ZvQueue *) { }
  void delQueue(ZvQueueType::T, ZuCSpan) { }
};

class App : public ZvEngineApp {
public:
  App() { }
  virtual ~App() { }

  void final() { }

  ZmRef<ZvAnyLink> createLink(ZuCSpan id);
};

enum { Connected, Disconnected, Reconnect }; // actions

class Engine : public ZvEngine {
public:
  // Engine() { }

  void init(
    Mgr *mgr, App *app, ZiMultiplex *mx, const ZfCf::AnyNode *cf);

  void up() { ZiLOG(Info, "ZvEngineTest", "UP"); }
  void down() { ZiLOG(Info, "ZvEngineTest", "DOWN"); }

  ZuTime reconnInterval() { return m_reconnInterval; }
  ZuTime reReqInterval() { return m_reReqInterval; }

  int action() const { return m_action; }
  void action(int v) { m_action = v; }

  void connected() { m_connected.post(); }
  void waitConnected() { m_connected.wait(); }
  void disconnected(bool) { m_disconnected.post(); }
  void waitDisconnected() { m_disconnected.wait(); }
  void reconnect() { m_reconnect.post(); }
  void waitReconnect() { m_reconnect.wait(); }

private:
  ZuTime	m_reconnInterval;
  ZuTime	m_reReqInterval;
  
  int		m_action = Connected;

  ZmSemaphore	m_connected;
  ZmSemaphore	m_disconnected;
  ZmSemaphore	m_reconnect;
};

#define linkINFO(msg) \
  ZiLOG(Info, "ZvEngineTest", ([id = id()](auto &s) { s << msg; }))

class Link : public ZvLink<Link, ZvTxPool<Link>> {
public:
  using Pool = ZvTxPool<Link>;
  using Base = ZvLink<Link, Pool>;

  Link(ZuCSpan id) : Base{id} { }

  ZuInline Engine *engine() {
    return static_cast<Engine *>(ZvAnyLink::engine()); // actually ZvAnyTx
  }

  // ZvLink CTRP
  ZuTime reconnInterval(unsigned) { return engine()->reconnInterval(); }

  // ZvAnyLink virtual
  void update(const ZfCf::AnyNode *) { }
  void reset(ZvSeqNo rxSeqNo, ZvSeqNo txSeqNo) { }

  void connect() {
    linkINFO("connect(): " << id);
    switch (engine()->action()) {
      case Connected:
	connected();
	engine()->connected();
	break;
      case Disconnected:
	disconnected(false);
	engine()->disconnected(false);
	break;
      case Reconnect:
	reconnect(false);
	engine()->reconnect();
	break;
    }
  }
  void disconnect() {
    linkINFO("disconnect(): " << id);
    disconnected(false);
    engine()->disconnected(false);
  }

  // ZvLink Rx CRTP
  void process(ZvIOMsg *msg) { }
  ZuTime reReqInterval() { return engine()->reReqInterval(); }
  void request(const ZvIOQueue::Span &prev, const ZvIOQueue::Span &now) { }
  void reRequest(const ZvIOQueue::Span &now) { }

  // ZvLink Tx CRTP
  void loaded_(ZvIOMsg *) { }
  void unloaded_(ZvIOMsg *) { }

  bool send_(ZvIOMsg *, bool more) { return true; }
  bool resend_(ZvIOMsg *, bool more) { return true; }
  void aborted_(ZvIOMsg *msg) { }

  bool sendGap_(const ZvIOQueue::Span &gap, bool more) { return true; }
  bool resendGap_(const ZvIOQueue::Span &gap, bool more) { return true; }

  // ZvIOQueueTx CRTP
  void archive_(ZvIOMsg *msg) { archived(msg->seqNo + 1); }
  ZmRef<ZvIOMsg> retrieve_(ZvSeqNo, ZvSeqNo) { return nullptr; }
};

ZmRef<ZvAnyLink> App::createLink(ZuCSpan id) { return new Link(id); }

struct EngineCf {
  double reconnInterval = 1;
  double reReqInterval = 1;
};

ZfStruct((EngineCf, Cf),
  (((reconnInterval),	((Range<0.0, 3600.0>))),	(Float, 1)),
  (((reReqInterval),	((Range<0.0, 3600.0>))),	(Float, 1)));

void Engine::init(
    Mgr *mgr, App *app, ZiMultiplex *mx, const ZfCf::AnyNode *cf)
{
  ZvEngine::init(mgr, app, mx, cf);
  auto config = ZfCf::handler<EngineCf>(cf).ctor();
  m_reconnInterval = config.reconnInterval;
  m_reReqInterval = config.reReqInterval;
  if (auto links = cf->resolve("links")) {
    if (!links->has<ZfCf::AnyNode::Object>())
      throw ZfCf_EXCEPT(ZfCfError::badType(links, "object"));
    for (auto &field: links->data<ZfCf::AnyNode::Object>())
      ZvEngine::updateLink(field.p<0>(), field.p<1>());
  }
}

static void engine()
{
  ZuTestScope(engine);
  ZiLog::init("ZvEngineTest");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();

  auto scan = ZfCf::scan(
      "id: Engine,\n"
      "mx: {\n"
	"nThreads: 4,\n"		// thread IDs are 1-based
	"rxThread: 1,\n"		// I/O Rx
	"txThread: 2,\n"		// I/O Tx
	"threads: {\n"
	  "1: {isolated: true},\n"
	  "2: {isolated: true},\n"
	  "3: {isolated: true}\n"
	"}\n"
      "},\n"
      "rxThread: 3,\n"		// App Rx
      "txThread: 2,\n"		// App Tx (same as I/O Tx)
      "links: {link1: {}}\n");
  auto root = ZuMv(scan.p<1>());

  ZuPtr<App> app = new App();
  ZuPtr<Mgr> mgr = new Mgr();
  ZmRef<Engine> engine = new Engine();
  ZuPtr<ZiMultiplex> mx =
    new ZiMultiplex{ZvMxParams{"mx", root->resolve("mx")}};

  engine->init(mgr, app, mx, root);

  mx->start();

  engine->start();
  engine->waitConnected();
  engine->stop();
  engine->waitDisconnected();

  engine->action(Reconnect);
  engine->start();
  engine->waitReconnect();
  engine->stop();
  engine->waitDisconnected();

  engine->action(Disconnected);
  engine->start();
  engine->waitDisconnected();
  engine->stop();

  mx->stop();

  engine->final();
  engine = nullptr;

  mgr = nullptr;

  app->final();
  app = nullptr;

  ZiLog::stop();
  ZuCHECK(true, "connect, reconnect, and disconnect lifecycle");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(engine);
  return 0;
}
