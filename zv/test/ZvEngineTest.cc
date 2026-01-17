//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ZvEngine connectivity framework unit smoke test

#include <zlib/ZvEngine.hh>

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

  ZmRef<ZvAnyLink> createLink(ZuID id);
};

enum { Connected, Disconnected, Reconnect }; // actions

class Engine : public ZvEngine {
public:
  // Engine() { }

  void init(Mgr *mgr, App *app, ZiMultiplex *mx, const ZvCf *cf);

  void up() { ZiLOG(Info, "ZvEngineTest", "UP"); }
  void down() { ZiLOG(Info, "ZvEngineTest", "DOWN"); }

  ZuTime reconnInterval() { return m_reconnInterval; }
  ZuTime reReqInterval() { return m_reReqInterval; }

  int action() const { return m_action; }
  void action(int v) { m_action = v; }

  void connected() { m_connected.post(); }
  void waitConnected() { m_connected.wait(); }
  void disconnected() { m_disconnected.post(); }
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
  ZiLOG(Info, "ZvEngineTest", ([=, id = id()](auto &s) { s << msg; }))

class Link : public ZvLink<Link, ZvTxPool<Link>> {
public:
  using Pool = ZvTxPool<Link>;
  using Base = ZvLink<Link, Pool>;

  Link(ZuID id) : Base{id} { }

  ZuInline Engine *engine() {
    return static_cast<Engine *>(ZvAnyLink::engine()); // actually ZvAnyTx
  }

  // ZvLink CTRP
  ZuTime reconnInterval(unsigned) { return engine()->reconnInterval(); }

  // ZvAnyLink virtual
  void update(const ZvCf *cf) { }
  void reset(ZvSeqNo rxSeqNo, ZvSeqNo txSeqNo) { }

  void connect() {
    linkINFO("connect(): " << id);
    switch (engine()->action()) {
      case Connected:
	connected();
	engine()->connected();
	break;
      case Disconnected:
	disconnected();
	engine()->disconnected();
	break;
      case Reconnect:
	reconnect(false);
	engine()->reconnect();
	break;
    }
  }
  void disconnect() {
    linkINFO("disconnect(): " << id);
    disconnected();
    engine()->disconnected();
  }

  // ZvLink Rx CRTP
  void process(ZvIOMsg *msg) { }
  ZuTime reReqInterval() { return engine()->reReqInterval(); }
  void request(const ZvIOQueue::Gap &prev, const ZvIOQueue::Gap &now) { }
  void reRequest(const ZvIOQueue::Gap &now) { }

  // ZvLink Tx CRTP
  void loaded_(ZvIOMsg *) { }
  void unloaded_(ZvIOMsg *) { }

  bool send_(ZvIOMsg *, bool more) { return true; }
  bool resend_(ZvIOMsg *, bool more) { return true; }
  void aborted_(ZvIOMsg *msg) { }

  bool sendGap_(const ZvIOQueue::Gap &gap, bool more) { return true; }
  bool resendGap_(const ZvIOQueue::Gap &gap, bool more) { return true; }

  // ZvIOQueueTx CRTP
  void archive_(ZvIOMsg *msg) { archived(msg->seqNo + 1); }
  ZmRef<ZvIOMsg> retrieve_(ZvSeqNo, ZvSeqNo) { return nullptr; }
};

ZmRef<ZvAnyLink> App::createLink(ZuID id) { return new Link(id); }

void Engine::init(Mgr *mgr, App *app, ZiMultiplex *mx, const ZvCf *cf)
{
  ZvEngine::init(mgr, app, mx, cf);
  m_reconnInterval = cf->getDbl("reconnInterval", 0, 3600, 1);
  m_reReqInterval = cf->getDbl("reReqInterval", 0, 3600, 1);
  if (ZmRef<ZvCf> linksCf = cf->getCf("links")) {
    linksCf->all([this](ZvCfNode *node) {
      if (node->data.is<ZmRef<ZvCf>>())
	ZvEngine::updateLink(node->key, node->data.p<ZmRef<ZvCf>>());
    });
  }
}

int main()
{
  ZiLog::init("ZvEngineTest");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();

  ZmRef<ZvCf> cf = new ZvCf();
  cf->fromString(
      "id Engine\n"
      "mx {\n"
	"nThreads 4\n"		// thread IDs are 1-based
	"rxThread 1\n"		// I/O Rx
	"txThread 2\n"		// I/O Tx
	"threads { 1 { isolated 1 } 2 { isolated 1 } 3 { isolated 1 } }\n"
      "}\n"
      "rxThread 3\n"		// App Rx
      "txThread 2\n"		// App Tx (same as I/O Tx)
      "links { link1 { } }\n");

  ZuPtr<App> app = new App();
  ZuPtr<Mgr> mgr = new Mgr();
  ZmRef<Engine> engine = new Engine();
  ZuPtr<ZiMultiplex> mx = new ZiMultiplex{ZvMxParams{"mx", cf->getCf("mx")}};

  engine->init(mgr, app, mx, cf);

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
}
