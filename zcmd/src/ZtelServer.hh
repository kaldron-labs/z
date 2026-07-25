//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZtelServer_HH
#define ZtelServer_HH

#ifndef ZcmdLib_HH
#include <zlib/ZcmdLib.hh>
#endif

#include <zlib/ZuDerive.hh>
#include <zlib/ZuID.hh>
#include <zlib/ZuLambdaTraits.hh>

#include <zlib/ZmFn.hh>

#include <zlib/ZtcHash.hh>
#include <zlib/ZtcHeap.hh>
#include <zlib/ZtcThread.hh>

#include <zlib/ZtRegex.hh>

#include <zlib/ZvEngine.hh>

#include <zlib/Ztls.hh>

#include <zlib/Zfb.hh>

#include <zlib/Zdb.hh>

// #include <zlib/Zcmd.hh>
#include <zlib/Ztel.hh>

namespace Ztel {

using namespace Zcmd;

struct TelemetryCf {
  ZtString<>	thread;
  unsigned	minInterval = 10;
  ZtString<>	alertPrefix = "alerts";
  unsigned	alertMaxReplay = 10;
};

ZfStruct((TelemetryCf, Cf),
  (((thread)),						(String)),
  (((minInterval), ((Range<1U, 1000000U>))),		(UInt32, 10)),
  (((alertPrefix)),					(String, "alerts")),
  (((alertMaxReplay), ((Range<1U, 1000U>))),		(UInt32, 10)));

struct ServerCf {
  TelemetryCf telemetry;
};

ZfStruct((ServerCf, Cf),
  (((telemetry)), (UDT)));

enum { AckIOBufSize = 32 };
using AckIOBufAlloc = ZiIOBufAlloc<AckIOBufSize>;

using QueueFn = ZvEngineMgr::QueueFn;

// the AlertFile facility is, intentionally, an independently and directly
// implemented on-disk database of alerts:
// - alerts may relate to errors elsewhere in the framework, including
//   network and database connectivity - these must be reliably stored
//   with an independent mechanism
// - each alert file corresponds to a single 24-hour period
// - the data file is a sequence of flatbuffers that are ready to send
// - each data file has an associated index file (.idx)
// - the index file is a sequence of offsets in the data file, indexed by seqNo
// - intra-file sequence numbers reset to 0 every 24 hours
// - the most recent alerts within the current telemetry scan-interval
//   are held in memory in a dynamically-sized ring buffer (ZmQueue)
// - this mechanism provides guaranteed delivery up to alertMaxReplay days back
// - downstream telemetry consumers can fan-in, index and persist alerts for
//   dashboards, consolidated alerting, filtering/reporting, etc.

class AlertFile {
public:
  AlertFile() { }
  ~AlertFile() { close(); }

private:
  // do not call ZiLOG since that may well recurse back here, print to stderr
  template <typename Message>
  void error(bool index, const Message &message) {
    struct Fmt : public ZuDateTimeFmt::CSV { Fmt() { tzOffset(timezone); } };
    auto &dateFmt = ZmTLS<Fmt>();
    auto buf = ZmLocal(ZeLogBuf);
    if (ZuUnlikely(!buf)) return; // paranoia
    *buf << ZuDateTime{Zm::now()}.fmt(dateFmt) <<
      " FATAL [Ztel] " << m_path << (index ? ".idx" : "") <<
      ": " << message << '\n';
    std::cerr << *buf << std::flush;
  }

  void open(const Zi::Path &prefix, unsigned date, unsigned flags) {
    m_date = date;
    m_seqNo = 0;
    if (!prefix) return;
    m_path = prefix;
    m_path << '_' << m_date;
    if (m_file.open(m_path, flags) != Zi::OK) {
      auto e = m_file.error();
      if (e.errNo() != ZiENOENT || (flags & ZiFile::Create))
	error(false, e);
      return;
    }
    if (m_index.open(m_path + ".idx", flags) != Zi::OK) {
      m_file.close();
      auto e = m_index.error();
      error(true, e);
      return;
    }
    m_offset = m_file.size();
    m_seqNo = m_index.size() / sizeof(size_t);
  }

public:
  void close() {
    m_file.close();
    m_index.close();
    m_path.null();
    m_date = 0;
    m_offset = 0;
    m_seqNo = 0;
  }

  unsigned date() const { return m_date; }
  size_t offset() const { return m_offset; }
  unsigned seqNo() const { return m_seqNo; }

  bool isOpen() const { return m_file; }

  // returns seqNo
  unsigned alloc(const Zi::Path &prefix, unsigned date) {
    if (date != m_date) {
      close();
      open(prefix, date, ZiFile::Create);
    }
    return m_seqNo;
  }
  void write(const ZiIOBuf *buf) {
    if (m_file) {
      if (m_file.pwrite(m_offset, buf->data(), buf->length) != Zi::OK)
	error(false, m_file.error());
      else if (m_index.pwrite(
	  m_seqNo * sizeof(size_t), &m_offset, sizeof(size_t)) != Zi::OK)
	error(true, m_index.error());
    }
    m_seqNo++;
    m_offset += buf->length;
  }

  ZmRef<ZiIOBuf> read(
    const Zi::Path &prefix, unsigned date, unsigned seqNo, void *bufOwner)
  {
    if (date != m_date) {
      close();
      open(prefix, date, ZiFile::ReadOnly);
    }
    if (!m_file) return ZmRef<ZiIOBuf>{};
    if (seqNo >= m_seqNo) return ZmRef<ZiIOBuf>{};
    size_t offset, next;
    if (m_index.pread(
	seqNo * sizeof(size_t), &offset, sizeof(size_t)) != Zi::OK) {
      error(true, m_index.error());
      return ZmRef<ZiIOBuf>{};
    }
    if (offset >= m_offset) {
      error(true, "corrupt");
      return ZmRef<ZiIOBuf>{};
    }
    if (seqNo == m_seqNo - 1)
      next = m_offset;
    else {
      if (m_index.pread(
	  (seqNo + 1) * sizeof(size_t), &next, sizeof(size_t)) != Zi::OK) {
	error(true, m_index.error());
	return ZmRef<ZiIOBuf>{};
      }
    }
    if (next < offset || next > m_offset) {
      error(true, "corrupt");
      return ZmRef<ZiIOBuf>{};
    }
    ZmRef<ZiIOBuf> buf =
      new ZiIOBufAlloc<>{bufOwner, static_cast<unsigned>(next - offset)};
    if (m_file.pread(offset, buf->data(), buf->length) != Zi::OK) {
      error(false, m_file.error());
      return ZmRef<ZiIOBuf>{};
    }
    return buf;
  }

private:
  unsigned		m_date = 0; // YYYYMMDD
  size_t		m_offset = 0;
  unsigned		m_seqNo = 0;
  Zi::Path		m_path;
  ZiFile		m_file;
  ZiFile		m_index;
};

ZuDerive(AlertQueue, (ZmQueue<ZmRef<ZiIOBuf>>));

template <typename App_, typename Link_>
class Server : public ZmEngine<Server<App_, Link_>>, ZvEngineMgr {
friend ZmEngine<Server>;

public:
  using App = App_;
  using Link = Link_;

  using ZmEngine<Server>::start;
  using ZmEngine<Server>::stop;

  const App *app() const { return static_cast<const App *>(this); }
  App *app() { return static_cast<App *>(this); }

  static App *app(const Link *link) {
    return static_cast<const App *>(link->app());
  }

  Server() {
    for (unsigned i = 0; i < ReqType::N; i++)
      m_watchLists[i].server = this;
  }

  bool init(ZiMultiplex *mx, const ZfCf::AnyNode *cf) {
    return ZmEngine<Server>::lock(
	ZmEngineState::Stopped, [this, mx, cf]() -> bool {
      m_mx = mx;

      if (!cf) {
	m_thread = mx->txThread();
	m_minInterval = 10;
	m_alertPrefix = "alerts";
	m_alertMaxReplay = 10;
	return true;
      }

      auto config = ZfCf::handler<ServerCf>(cf).ctor();
      if (config.telemetry.thread)
	m_thread = mx->sid(config.telemetry.thread);
      else
	m_thread = mx->txThread();

      m_minInterval = config.telemetry.minInterval;
      m_alertPrefix = ZuMv(config.telemetry.alertPrefix);
      // unit of alertMaxReplay is days
      m_alertMaxReplay = config.telemetry.alertMaxReplay;

      return true;
    });
  }
  bool final() {
    return ZmEngine<Server>::lock(ZmEngineState::Stopped, [this]() {
      stop_();

      for (unsigned i = 0; i < ReqType::N; i++)
	m_watchLists[i].clean();

      m_alertQueue.clean();
      m_alertFile.close();

      m_queues.clean();
      m_engines.clean();
      return true;
    });
  }

  template <typename ...Args>
  void run(Args &&...args) const {
    m_mx->run(ZuFwd<Args>(args)..., m_thread);
  }
  template <typename ...Args>
  void invoke(Args &&...args) const {
    m_mx->invoke(ZuFwd<Args>(args)..., m_thread);
  }
  bool invoked() const { return m_mx->invoked(m_thread); }

private:
  template <typename L>
  bool spawn(L &&l) {
    if (!m_mx || !m_mx->running()) return false;
    run(ZuFwd<L>(l));
    return true;
  }

  void wake() {
    if (ZuUnlikely(!m_mx || !m_mx->running())) return;
    run([this]() { this->stopped(); });
  }

public:
  void process(Link *link, ZmRef<ZiIOBuf> buf) {
    invoke([
      this, link = ZmMkRef(link), buf = ZuMv(buf)
    ]() mutable { process_(ZuMv(link), ZuMv(buf)); });
  }
  void process_(ZmRef<Link> link, ZmRef<ZiIOBuf> buf) {
    auto req = Zfb::GetRoot<Ztel::fbs::Request>(buf->data());
    unsigned interval = req->interval();
    if (interval && interval < m_minInterval) interval = m_minInterval;
    switch (int(req->type())) {
      case ReqType::Heap:	heapQuery(ZuMv(link), req, interval); break;
      case ReqType::HashTbl:	hashQuery(ZuMv(link), req, interval); break;
      case ReqType::Thread:	threadQuery(ZuMv(link), req, interval); break;
      case ReqType::Mx:		mxQuery(ZuMv(link), req, interval); break;
      case ReqType::Queue:	queueQuery(ZuMv(link), req, interval); break;
      case ReqType::Engine:	engineQuery(ZuMv(link), req, interval); break;
      case ReqType::DB:		dbQuery(ZuMv(link), req, interval); break;
      case ReqType::App:	appQuery(ZuMv(link), req, interval); break;
      case ReqType::Alert:	alertQuery(ZuMv(link), req, interval); break;
      default: break;
    }
  }

  void disconnected(Link *link) {
    invoke([this, link = ZmMkRef(link)]() { disconnected_(link); });
  }

  // EngineMgr functions

  void updEngine(ZvEngine *engine) {
    invoke([this, engine = ZmMkRef(engine)]() { engineScan(engine); });
  }
  void updLink(ZvAnyLink *link) {
    invoke([this, link = ZmMkRef(link)]() { linkScan(link); });
  }

  void addEngine(ZvEngine *engine) {
    invoke([this, engine = ZmMkRef(engine)]() mutable {
      if (!m_engines.find(engine->id())) m_engines.add(ZuMv(engine));
    });
  }
  void delEngine(ZvEngine *engine) {
    invoke([this, id = ZuID{engine->id()}]() {
      m_engines.del(id);
    });
  }
  void addQueue(unsigned type, ZuCSpan id, QueueFn queueFn) {
    invoke([this, type, id = ZuID{id}, queueFn = ZuMv(queueFn)]() mutable {
      auto key = ZuFwdTuple(type, id);
      if (!m_queues.find(key)) m_queues.add(key, ZuMv(queueFn));
    });
  }
  void delQueue(unsigned type, ZuCSpan id) {
    invoke([this, type, id = ZuID{id}]() {
      auto key = ZuFwdTuple(type, id);
      m_queues.del(key);
    });
  }

  // DB registration
 
  void addDB(Zdb *db) {
    invoke([this, db = ZmMkRef(db)]() { m_db = ZuMv(db); });
  }
  void delDB() {
    invoke([this]() { m_db = nullptr; });
  }

  // app RAG updates

  void appUpdated() {
    invoke([this]() { m_appUpdated = true; });
  }

  // alerts

  template <typename L>
  void alert(ZeEvent<L> e) {
    invoke([this, e = ZuMv(e)]() mutable {
      alert_(ZuMv(e));
    });
  }

private:
  void start_() {
    for (int i = 0; i < ReqType::N; i++) {
      auto &list = m_watchLists[i];
      switch (i) {
	case int(fbs::ReqType::Heap):
	  reschedule(list, [](Server *server) { server->heapScan(); });
	  break;
	case int(fbs::ReqType::HashTbl):
	  reschedule(list, [](Server *server) { server->hashScan(); });
	  break;
	case int(fbs::ReqType::Thread):
	  reschedule(list, [](Server *server) { server->threadScan(); });
	  break;
	case int(fbs::ReqType::Mx):
	  reschedule(list, [](Server *server) { server->mxScan(); });
	  break;
	case int(fbs::ReqType::Queue):
	  reschedule(list, [](Server *server) { server->queueScan(); });
	  break;
	case int(fbs::ReqType::Engine):
	  reschedule(list, [](Server *server) { server->engineScan(); });
	  break;
	case int(fbs::ReqType::DB):
	  reschedule(list, [](Server *server) { server->dbScan(); });
	  break;
	case int(fbs::ReqType::App):
	  reschedule(list, [](Server *server) { server->appScan(); });
	  break;
	case int(fbs::ReqType::Alert):
	  reschedule(list, [](Server *server) { server->alertScan(); });
	  break;
      }
    }
  }

  void stop_() {
    for (unsigned i = 0; i < ReqType::N; i++) {
      auto &list = m_watchLists[i];
      m_mx->del(&list.timer);
    }
  }

  template <typename L>
  void alert_(ZeEvent<L> alert) {
    m_alertBuf.length(0);
    m_alertBuf << alert;
    ZuDateTime date{alert.time};
    unsigned yyyymmdd = date.yyyymmdd();
    unsigned seqNo = m_alertFile.alloc(m_alertPrefix, yyyymmdd);
    auto date_ = dateTime(date);
    Zfb::IOBuilder fbb(new ZiIOBufAlloc<>()); // FIXME - default buffer
    fbb.Finish(fbs::CreateTelemetry(m_fbb,
	  fbs::TelData::Alert,
	  fbs::CreateAlert(m_fbb,
	    &date_, seqNo, alert.tid,
	    static_cast<fbs::Severity>(alert.severity),
	    Zfb::Save::str(m_fbb, m_alertBuf)).Union()));
    ZmRef<ZiIOBuf> buf = fbb.buf();
    m_alertFile.write(buf);
    m_alertQueue.push(ZuMv(buf));
  }

  ZuDerive(Queues,
    (ZmRBTreeKV<ZuTuple<unsigned, ZuID>, QueueFn,
      ZmRBTreeUnique<true>>));

  static ZuCSpan EngineIDAxor(const ZvEngine *engine) { return engine->id(); }
  ZuDerive(Engines,
    (ZmRBTree<ZmRef<ZvEngine>,
      ZmRBTreeKey<EngineIDAxor,
	ZmRBTreeUnique<true>>>));

  struct Watch_ {
    Link	*link = nullptr;
    ZuID	filter;
  };
  ZuDerive(WatchList_, (ZmList<Watch_, ZmListNode<Watch_>>)); // FIXME - HeapID
  using Watch = typename WatchList_::Node;
  struct WatchList {
    WatchList_		list;
    unsigned		interval = 0;	// in millisecs
    ZmScheduler::Timer	timer;
    Server		*server = nullptr;

    template <typename P>
    void push(P &&v) { list.pushNode(ZuFwd<P>(v)); }
    void clean() { list.clean(); }
    auto count() const { return list.count_(); }
    auto iter() { return list.iter(); }
    auto citer() const { return list.citer(); }
  };

  template <auto Fn>
  void subscribe(WatchList &list, Watch *watch, unsigned interval) {
    bool reschedule = false;
    if (!list.interval || interval < list.interval)
      if (list.interval != interval) {
	reschedule = true;
	list.interval = interval;
      }
    list.push(watch);
    if (reschedule) this->reschedule_<Fn>(list);
  }
  template <auto Fn>
  void reschedule(WatchList &list) {
    if (!list.interval) return;
    this->reschedule_<Fn>(list);
  }
  template <auto Fn>
  void reschedule_(WatchList &list) {
    m_mx->add(&list.timer,
	Zm::now(ZuTime{ZuTime::Nano{int128_t(list.interval) * 1000000}}),
	ZmScheduler::Advance,
	[list = &list](auto &&arm) {
	  return arm([list]() {
	    ZuInvoke<Fn>(list->server);
	    list->server->template reschedule_<Fn>(*list);
	  });
	}, m_thread);
  }

  void unsubscribe(WatchList &list, Link *link, ZuCSpan filter) {
    {
      auto i = list.iter();
      while (auto watch = i())
	if (watch->link == link && (!filter || watch->filter == filter))
	  i.del();
    }
    if (!list.count() && list.interval) {
      list.interval = 0;
      m_mx->del(&list.timer);
    }
  }

  void disconnected_(Link *link) {
    for (unsigned i = 0; i < ReqType::N; i++)
      unsubscribe(m_watchLists[i], link, ZuCSpan());
  }

  bool match(ZuCSpan filter, ZuCSpan id) {
    if (!filter || filter[0] == '*') return true; // null or "*"
    unsigned n = filter.length() - 1;
    if (filter[n] == '*') { // "prefix*"
      if (id.length() < n) return false;
      return !memcmp(&filter[0], &id[0], n);
    }
    return filter == id;
  }
  bool matchThread(ZuCSpan filter, ZuCSpan name, unsigned tid) {
    if (!filter || filter[0] == '*') return true; // null or "*"
    unsigned n = filter.length() - 1;
    if (filter[n] == '*') { // "prefix*"
      if (name.length() < n) return false;
      return !memcmp(&filter[0], &name[0], n);
    }
    if (filter == name) return true;
    return ZuBox<unsigned>(filter) == tid;
  }
  bool matchQueue(ZuCSpan filter, unsigned type, ZuCSpan id) {
    if (!filter || filter[0] == '*') return true; // null or "*"
    unsigned n = filter.length() - 1;
    for (unsigned i = 0; i <= n; i++)
      if (filter[i] == ':') { // "type:id"
	if (!i || (i == 1 && filter[0] == '*')) { // ":id" or "*:id"
	  if (i == n || (i == n - 1 && filter[n] == '*'))
	    return true; // ":" or ":*" or "*:" or "*:*"
	  return ZuCSpan(&filter[i + 1], n - i) == id;
	}
	int ftype = ZvQueueType::lookup(ZuCSpan(&filter[0], i));
	if (ftype != (int)type) return false;
	if (i == n || (i == n - 1 && filter[n] == '*'))
	  return true; // "type:" or "type:*"
	if (filter[n] == '*') { // "type:prefix*"
	  n -= (i + 1);
	  if (id.length() < n) return false;
	  return !memcmp(&filter[i + 1], &id[0], n);
	}
	n -= i;
	return ZuCSpan(&filter[i + 1], n) == id;
      }
    if (filter[n] == '*') { // id*
      if (id.length() < n) return false;
      return !memcmp(&filter[0], &id[0], n);
    }
    return filter == id; // id
  }

  // heap processing

  void heapQuery(
    ZmRef<Link> link, const fbs::Request *req, unsigned interval)
  {
    auto &list = m_watchLists[ReqType::Heap];
    if (interval && !req->subscribe()) {
      unsubscribe(list, ZuMv(link), Zfb::Load::str(req->filter()));
      return;
    }
    auto watch = new Watch{ZuMv(link), Zfb::Load::str(req->filter())};
    if (interval)
      this->subscribe<[](Server *server) { server->heapScan(); }>(
	  list, watch, interval);
    Ztc::HeapMgr::all(Ztc::HeapMgr::AllFn{
      watch, [](Watch *watch, Ztc::Heap *heap) {
	watch->link->app()->heapQuery_(watch, heap);
      }});
    if (!interval) delete watch;
  }
  void heapQuery_(Watch *watch, const Ztc::Heap *heap) {
    Heap data;
    heap->telemetry(data);
    if (!match(watch->filter, data.id)) return;
    m_fbb.Finish(fbs::CreateTelemetry(m_fbb,
	  fbs::TelData::Heap, ZfbStruct::save(m_fbb, data).Union()));
    watch->link->sendTelemetry(m_fbb.buf());
  }

  void heapScan() {
    if (!m_watchLists[ReqType::Heap].list.count_()) return;
    Ztc::HeapMgr::all(Ztc::HeapMgr::AllFn{
      this, [](Server *server, Ztc::Heap *heap) {
	server->heapScan(heap);
      }});
  }
  void heapScan(const Ztc::Heap *heap) {
    Heap data;
    heap->telemetry(data);
    auto i = m_watchLists[ReqType::Heap].list.citer();
    while (auto watch = i()) {
      if (!match(watch->filter, data.id)) continue;
      m_fbb.Finish(fbs::CreateTelemetry(m_fbb,
	  fbs::TelData::Heap, ZfbStruct::saveUpd(m_fbb, data).Union()));
      watch->link->sendTelemetry(m_fbb.buf());
    }
  }

  // hash table processing

  void hashQuery(
    ZmRef<Link> link, const fbs::Request *req, unsigned interval)
  {
    auto &list = m_watchLists[ReqType::HashTbl];
    if (interval && !req->subscribe()) {
      unsubscribe(list, ZuMv(link), Zfb::Load::str(req->filter()));
      return;
    }
    auto watch = new Watch{ZuMv(link), Zfb::Load::str(req->filter())};
    if (interval)
      this->subscribe<[](Server *server) { server->hashScan(); }>(
	  list, watch, interval);
    Ztc::HashMgr::all(Ztc::HashMgr::AllFn{
      watch, [](Watch *watch, Ztc::Hash *tbl) {
	watch->link->app()->hashQuery_(watch, tbl);
      }});
    if (!interval) delete watch;
  }
  void hashQuery_(Watch *watch, const Ztc::Hash *tbl) {
    HashTbl data;
    tbl->telemetry(data);
    if (!match(watch->filter, data.id)) return;
    m_fbb.Finish(fbs::CreateTelemetry(m_fbb,
	  fbs::TelData::HashTbl, ZfbStruct::save(m_fbb, data).Union()));
    watch->link->sendTelemetry(m_fbb.buf());
  }

  void hashScan() {
    if (!m_watchLists[ReqType::HashTbl].list.count_()) return;
    Ztc::HashMgr::all(Ztc::HashMgr::AllFn{
      this, [](Server *server, Ztc::Hash *tbl) {
	server->hashScan(tbl);
      }});
  }
  void hashScan(const Ztc::Hash *tbl) {
    HashTbl data;
    tbl->telemetry(data);
    auto i = m_watchLists[ReqType::HashTbl].list.citer();
    while (auto watch = i()) {
      if (!match(watch->filter, data.id)) continue;
      m_fbb.Finish(fbs::CreateTelemetry(m_fbb,
	    fbs::TelData::HashTbl, ZfbStruct::saveUpd(m_fbb, data).Union()));
      watch->link->sendTelemetry(m_fbb.buf());
    }
  }

  // thread processing

  void threadQuery(
    ZmRef<Link> link, const fbs::Request *req, unsigned interval)
  {
    auto &list = m_watchLists[ReqType::Thread];
    if (interval && !req->subscribe()) {
      unsubscribe(list, ZuMv(link), Zfb::Load::str(req->filter()));
      return;
    }
    auto watch = new Watch{ZuMv(link), Zfb::Load::str(req->filter())};
    if (interval)
      this->subscribe<[](Server *server) { server->threadScan(); }>(
	  list, watch, interval);
    Ztc::ThreadMgr::all(Ztc::ThreadMgr::AllFn{
      watch, [](Watch *watch, Ztc::Thread *thread) {
	watch->link->app()->threadQuery_(watch, thread);
      }});
    if (!interval) delete watch;
  }
  void threadQuery_(Watch *watch, const Ztc::Thread *thread) {
    Thread data;
    thread->telemetry(data);
    if (!matchThread(watch->filter, data.name, data.tid)) return;
    m_fbb.Finish(fbs::CreateTelemetry(m_fbb,
	  fbs::TelData::Thread, ZfbStruct::save(m_fbb, data).Union()));
    watch->link->sendTelemetry(m_fbb.buf());
  }

  void threadScan() {
    if (!m_watchLists[ReqType::Thread].list.count_()) return;
    Ztc::ThreadMgr::all(Ztc::ThreadMgr::AllFn{
      this, [](Server *server, Ztc::Thread *thread) {
	server->threadScan(thread);
      }});
  }
  void threadScan(const Ztc::Thread *thread) {
    Thread data;
    thread->telemetry(data);
    auto i = m_watchLists[ReqType::Thread].list.citer();
    while (auto watch = i()) {
      if (!matchThread(watch->filter, data.name, data.tid)) continue;
      m_fbb.Finish(fbs::CreateTelemetry(m_fbb,
	    fbs::TelData::Thread, ZfbStruct::saveUpd(m_fbb, data).Union()));
      watch->link->sendTelemetry(m_fbb.buf());
    }
  }

  // mx processing

  void mxQuery(
    ZmRef<Link> link, const fbs::Request *req, unsigned interval)
  {
    auto &list = m_watchLists[ReqType::Mx];
    if (interval && !req->subscribe()) {
      unsubscribe(list, ZuMv(link), Zfb::Load::str(req->filter()));
      return;
    }
    auto watch = new Watch{ZuMv(link), Zfb::Load::str(req->filter())};
    if (interval)
      this->subscribe<[](Server *server) { server->mxScan(); }>(
	  list, watch, interval);
    ZiMxMgr::all([watch](ZiMultiplex *mx) {
      watch->link->app()->mxQuery_(watch, mx);
    });
    if (!interval) delete watch;
  }
  void mxQuery_(Watch *watch, ZiMultiplex *mx) {
    Mx data;
    mx->telemetry(data);
    if (!match(watch->filter, data.id)) return;
    m_fbb.Finish(fbs::CreateTelemetry(m_fbb,
	  fbs::TelData::Mx, ZfbStruct::save(m_fbb, data).Union()));
    watch->link->sendTelemetry(m_fbb.buf());
    {
      uint64_t inCount, inBytes, outCount, outBytes;
      for (unsigned tid = 1, n = mx->params().nThreads(); tid <= n; tid++) {
	ZuID queueID;
	queueID << mx->params().id() << '.'
	  << mx->params().thread(tid).name();
	{
	  const auto &ring = mx->ring(tid);
	  ring.stats(inCount, inBytes, outCount, outBytes);
	  m_fbb.Finish(fbs::CreateTelemetry(m_fbb,
		fbs::TelData::Queue,
		fbs::CreateQueue(m_fbb,
		  Zfb::Save::str(m_fbb, queueID), 0, ring.count_(),
		  inCount, inBytes, outCount, outBytes,
		  ring.params().size, ring.full(),
		  fbs::QueueType::Thread).Union()));
	  watch->link->sendTelemetry(m_fbb.buf());
	}
	if (queueID.length() < ZuIDSize - 1)
	  queueID << '_';
	else
	  queueID[ZuIDSize - 2] = '_';
	{
	  const auto &queue = mx->queue(tid);
	  queue.stats(inCount, outCount);
	  m_fbb.Finish(fbs::CreateTelemetry(m_fbb,
		fbs::TelData::Queue,
		fbs::CreateQueue(m_fbb,
		  str(m_fbb, queueID), 0, queue.count_(),
		  inCount, inCount * sizeof(ZmFn<>),
		  outCount, outCount * sizeof(ZmFn<>),
		  queue.size_(), false,
		  fbs::QueueType::Thread).Union()));
	  watch->link->sendTelemetry(m_fbb.buf());
	}
      }
    }
    mx->allCxns([this, watch](ZiConnection *cxn) {
      Socket data;
      cxn->telemetry(data);
      m_fbb.Finish(fbs::CreateTelemetry(m_fbb,
	    fbs::TelData::Socket, ZfbStruct::save(m_fbb, data).Union()));
      watch->link->sendTelemetry(m_fbb.buf());
    });
  }

  void mxScan() {
    if (!m_watchLists[ReqType::Mx].list.count_()) return;
    ZiMxMgr::all([this](ZiMultiplex *mx) { mxScan(mx); });
  }
  // FIXME - these thread queues should be added to the tel server's 
  // queue container so they can be found and subscribed to by ID
  // independently of the multiplexer
  void mxScan(ZiMultiplex *mx) {
    Mx data;
    mx->telemetry(data);
    auto i = m_watchLists[ReqType::Mx].list.citer();
    while (auto watch = i()) {
      if (!match(watch->filter, data.id)) continue;
      m_fbb.Finish(fbs::CreateTelemetry(m_fbb,
	    fbs::TelData::Mx, ZfbStruct::saveUpd(m_fbb, data).Union()));
      watch->link->sendTelemetry(m_fbb.buf());
      {
	uint64_t inCount, inBytes, outCount, outBytes;
	for (unsigned tid = 1, n = mx->params().nThreads(); tid <= n; tid++) {
	  ZuID queueID;
	  queueID << mx->params().id() << '.'
	    << mx->params().thread(tid).name();
	  {
	    const auto &ring = mx->ring(tid);
	    ring.stats(inCount, inBytes, outCount, outBytes);
	    auto id_ = Zfb::Save::str(m_fbb, queueID);
	    fbs::QueueBuilder b(m_fbb);
	    b.add_id(id_);
	    b.add_count(ring.count_());
	    b.add_inCount(inCount);
	    b.add_inBytes(inBytes);
	    b.add_outCount(outCount);
	    b.add_outBytes(outBytes);
	    b.add_full(ring.full());
	    // FIXME - add_type()
	    m_fbb.Finish(fbs::CreateTelemetry(m_fbb,
		  fbs::TelData::Queue, b.Finish().Union()));
	    watch->link->sendTelemetry(m_fbb.buf());
	  }
	  if (queueID.length() < ZuIDSize)
	    queueID << '_';
	  else
	    queueID[ZuIDSize - 1] = '_';
	  {
	    const auto &queue = mx->queue(tid);
	    queue.stats(inCount, outCount);
	    auto id_ = Zfb::Save::str(m_fbb, queueID);
	    fbs::QueueBuilder b(m_fbb);
	    b.add_id(id_);
	    b.add_count(queue.count_());
	    b.add_inCount(inCount);
	    b.add_inBytes(inCount * sizeof(ZmFn<>));
	    b.add_outCount(outCount);
	    b.add_outBytes(outCount * sizeof(ZmFn<>));
	    b.add_full(0);
	    // FIXME - add_type()
	    m_fbb.Finish(fbs::CreateTelemetry(m_fbb,
		  fbs::TelData::Queue, b.Finish().Union()));
	    watch->link->sendTelemetry(m_fbb.buf());
	  }
	}
      }
      mx->allCxns([this, watch](ZiConnection *cxn) {
	Socket data;
	cxn->telemetry(data);
	m_fbb.Finish(fbs::CreateTelemetry(m_fbb,
	      fbs::TelData::Socket, ZfbStruct::saveUpd(m_fbb, data).Union()));
	watch->link->sendTelemetry(m_fbb.buf());
      });
    }
  }

  // queue processing
  // LATER - old queue code - used by caller of addQueue()
#if 0
    ZvQueue *queue;
    queue->stats(data.inCount, data.inBytes, data.outCount, data.outBytes);

    // I/O queues (link, etc.)
	  cxn->transmit(queue(

	    // IPC queues (ZiRing)
	    ring->params().name(), (uint64_t)0, (uint64_t)count,
	    inCount, inBytes, outCount, outBytes,
	    (uint32_t)ring->full(), (uint32_t)ring->params().size(),
	    (uint8_t)QueueType::IPC));
#endif

  void queueQuery(
    ZmRef<Link> link, const fbs::Request *req, unsigned interval)
  {
    auto &list = m_watchLists[ReqType::Queue];
    if (interval && !req->subscribe()) {
      unsubscribe(list, ZuMv(link), Zfb::Load::str(req->filter()));
      return;
    }
    auto watch = new Watch{ZuMv(link), Zfb::Load::str(req->filter())};
    if (interval)
      this->subscribe<[](Server *server) { server->queueScan(); }>(
	  list, watch, interval);
    auto i = m_queues.citer();
    while (auto node = i()) queueQuery_(watch, node->val());
    if (!interval) delete watch;
  }
  void queueQuery_(Watch *watch, const QueueFn &fn) {
    Queue data;
    fn(data);
    if (!matchQueue(watch->filter, data.type, data.id)) return;
    m_fbb.Finish(fbs::CreateTelemetry(m_fbb,
	  fbs::TelData::Queue, ZfbStruct::save(m_fbb, data).Union()));
    watch->link->sendTelemetry(m_fbb.buf());
  }
  void queueScan() {
    if (!m_watchLists[ReqType::Queue].list.count_()) return;
    auto i = m_queues.citer();
    while (auto node = i()) queueScan(node->val());
  }
  void queueScan(const QueueFn &fn) {
    Queue data;
    fn(data);
    auto i = m_watchLists[ReqType::Queue].list.citer();
    while (auto watch = i()) {
      if (!matchQueue(watch->filter, data.type, data.id)) continue;
      m_fbb.Finish(fbs::CreateTelemetry(m_fbb,
	    fbs::TelData::Queue, ZfbStruct::saveUpd(m_fbb, data).Union()));
      watch->link->sendTelemetry(m_fbb.buf());
    }
  }

  // engine processing

  void engineQuery(
    ZmRef<Link> link, const fbs::Request *req, unsigned interval)
  {
    auto &list = m_watchLists[ReqType::Engine];
    if (interval && !req->subscribe()) {
      unsubscribe(list, ZuMv(link), Zfb::Load::str(req->filter()));
      return;
    }
    auto watch = new Watch{ZuMv(link), Zfb::Load::str(req->filter())};
    if (interval)
      this->subscribe<[](Server *server) { server->engineScan(); }>(
	  list, watch, interval);
    {
      auto i = m_engines.citer();
      while (auto node = i()) engineQuery_(watch, node->val());
    }
    if (!interval) delete watch;
  }
  void engineQuery_(Watch *watch, ZvEngine *engine) {
    Engine data;
    engine->telemetry(data);
    if (!match(watch->filter, data.id)) return;
    m_fbb.Finish(fbs::CreateTelemetry(m_fbb,
	  fbs::TelData::Engine, ZfbStruct::save(m_fbb, data).Union()));
    watch->link->sendTelemetry(m_fbb.buf());
    engine->allLinks<ZvAnyLink>([this, watch](ZvAnyLink *link) {
      Ztel::Link data;
      link->telemetry(data);
      m_fbb.Finish(fbs::CreateTelemetry(m_fbb,
	    fbs::TelData::Link, ZfbStruct::save(m_fbb, data).Union()));
      watch->link->sendTelemetry(m_fbb.buf());
      return true;
    });
  }
  void engineScan() {
    if (!m_watchLists[ReqType::Engine].list.count_()) return;
    auto i = m_engines.citer();
    while (auto node = i()) engineScan(node->val());
  }
  void engineScan(ZvEngine *engine) {
    Engine data;
    engine->telemetry(data);
    auto i = m_watchLists[ReqType::Engine].list.citer();
    while (auto watch = i()) {
      if (!match(watch->filter, data.id)) continue;
      m_fbb.Finish(fbs::CreateTelemetry(m_fbb,
	    fbs::TelData::Engine, ZfbStruct::saveUpd(m_fbb, data).Union()));
      watch->link->sendTelemetry(m_fbb.buf());
      engine->allLinks<ZvAnyLink>([this, watch](ZvAnyLink *link) {
	linkScan(link, watch);
	return true;
      });
    }
  }
  void linkScan(const ZvAnyLink *link) {
    auto i = m_watchLists[ReqType::Engine].list.citer();
    while (auto watch = i()) linkScan(link, watch);
  }
  void linkScan(const ZvAnyLink *link, Watch *watch) {
    Ztel::Link data;
    link->telemetry(data);
    m_fbb.Finish(fbs::CreateTelemetry(m_fbb,
	  fbs::TelData::Link, ZfbStruct::saveUpd(m_fbb, data).Union()));
    watch->link->sendTelemetry(m_fbb.buf());
  }

  // DB processing

  void dbQuery(
    ZmRef<Link> link, const fbs::Request *req, unsigned interval)
  {
    auto &list = m_watchLists[ReqType::DB];
    if (interval && !req->subscribe()) {
      unsubscribe(list, ZuMv(link), Zfb::Load::str(req->filter()));
      return;
    }
    auto watch = new Watch{ZuMv(link), Zfb::Load::str(req->filter())};
    if (interval)
      this->subscribe<[](Server *server) { server->dbScan(); }>(
	  list, watch, interval);
    dbQuery_(watch);
    if (!interval) delete watch;
  }
  void dbQuery_(Watch *watch, bool update = false) {
    if (!m_db) return;
    // these callbacks can execute async
    m_db->invoke([
      db = m_db, link = ZmMkRef(watch->link), update
    ]() mutable {
      Zfb::IOBuilder fbb;
      auto offset = db->telemetry(fbb, update);
      fbb.Finish(fbs::CreateTelemetry(fbb, fbs::TelData::DB, offset));
      link->sendTelemetry(fbb.buf());
      db->allHosts([link, update](const ZdbHost *host) {
	Zfb::IOBuilder fbb;
	auto offset = host->telemetry(fbb, update);
	fbb.Finish(fbs::CreateTelemetry(fbb, fbs::TelData::DBHost, offset));
	link->sendTelemetry(fbb.buf());
      });
      db->all([link, update](
	  const ZdbAnyTable *table, Zdb::DB::AllTableFn done) {
	Zfb::IOBuilder fbb;
	auto offset = table->telemetry(fbb, update);
	fbb.Finish(fbs::CreateTelemetry(fbb, fbs::TelData::DBTable, offset));
	link->sendTelemetry(fbb.buf());
	done(true);
      });
    });
  }
  void dbScan() {
    if (!m_watchLists[ReqType::DB].list.count_()) return;
    if (!m_db) return;
    auto i = m_watchLists[ReqType::DB].list.citer();
    while (auto watch = i()) dbQuery_(watch, true);
  }

  // app processing

  void appQuery(
    ZmRef<Link> link, const fbs::Request *req, unsigned interval)
  {
    auto &list = m_watchLists[ReqType::App];
    if (interval && !req->subscribe()) {
      unsubscribe(list, ZuMv(link), Zfb::Load::str(req->filter()));
      return;
    }
    auto watch = new Watch{ZuMv(link), Zfb::Load::str(req->filter())};
    if (interval)
      this->subscribe<[](Server *server) { server->appScan(); }>(
	  list, watch, interval);
    appQuery_(watch);
    if (!interval) delete watch;
  }
  void appQuery_(Watch *watch) {
    Ztel::App data;
    app()->telemetry(data);
    m_fbb.Finish(fbs::CreateTelemetry(m_fbb,
	  fbs::TelData::App,
	  ZfbStruct::save(m_fbb, data).Union()));
    watch->link->sendTelemetry(m_fbb.buf());
  }
  void appScan() {
    if (!m_appUpdated) return;
    m_appUpdated = false;
    if (!m_watchLists[ReqType::App].list.count_()) return;
    Ztel::App data;
    app()->telemetry(data);
    auto i = m_watchLists[ReqType::App].list.citer();
    while (auto watch = i()) {
      m_fbb.Finish(fbs::CreateTelemetry(m_fbb,
	    fbs::TelData::App, ZfbStruct::saveUpd(m_fbb, data).Union()));
      watch->link->sendTelemetry(m_fbb.buf());
    }
  }

  // alert processing

  void alertQuery(
    ZmRef<Link> link, const fbs::Request *req, unsigned interval)
  {
    auto &list = m_watchLists[ReqType::Alert];
    if (interval && !req->subscribe()) {
      unsubscribe(list, ZuMv(link), Zfb::Load::str(req->filter()));
      return;
    }
    auto watch = new Watch{ZuMv(link), Zfb::Load::str(req->filter())};
    if (interval)
      this->subscribe<[](Server *server) { server->alertScan(); }>(
	  list, watch, interval);
    alertQuery_(watch);
    if (!interval) delete watch;
  }
  void alertQuery_(Watch *watch) {
    // parse filter - yyyymmdd:seqNo
    ZtRegexCaptures(c, 2);
    ZuBox<unsigned> date = 0, seqNo = 0;
    if (ZtREGEX("^(\d{8}):(\d+)$").m(watch->filter, c) == 3) {
      date = c[2];
      seqNo = c[3];
    }
    // ensure date is within range
    unsigned now = ZuDateTime{Zm::now()}.yyyymmdd();
    ZmRef<ZiIOBuf> buf;
    if (!date)
      date = now;
    else if (date < now - m_alertMaxReplay)
      date = now - m_alertMaxReplay;
    // obtain date and seqNo of in-memory alert ring (today:UINT_MAX if empty)
    unsigned headDate = now;
    unsigned headSeqNo = UINT_MAX;
    {
      if (buf = m_alertQueue.head()) {
	auto alert = fbs::GetTelemetry(buf->data())->data_as_Alert();
	if (ZuLikely(alert)) {
	  headDate = ZfbTransform::DateTime::load(alert->time()).yyyymmdd();
	  headSeqNo = alert->seqNo();
	}
      }
    }
    // replay from file(s) up to alerts available in-memory (if any)
    {
      AlertFile replay;
      while (date < headDate) {
	while (buf = replay.read(m_alertPrefix, date, seqNo++, this))
	  watch->link->send(ZuMv(buf));
	seqNo = 0;
	++date;
      }
      while (buf = replay.read(m_alertPrefix, date, seqNo++, this)) {
	if (seqNo >= headSeqNo) break;
	watch->link->send(ZuMv(buf));
      }
    }
    // replay from memory remaining alerts requested, up to latest
    {
      auto i = m_alertQueue.iter();
      while (buf = i()) {
	auto alert = fbs::GetTelemetry(buf->data())->data_as_Alert();
	if (ZuLikely(alert)) {
	  unsigned alertDate =
	    ZfbTransform::DateTime::load(alert->time()).yyyymmdd();
	  unsigned alertSeqNo = alert->seqNo();
	  if (alertDate > date || (alertDate == date && alertSeqNo >= seqNo))
	    watch->link->send(ZuMv(buf));
	}
      }
    }
  }
  void alertScan() {
    // dequeue all alerts in-memory, send to all watchers
    while (ZmRef<ZiIOBuf> buf = m_alertQueue.shift()) {
      auto i = m_watchLists[ReqType::Alert].list.citer();
      while (auto watch = i()) watch->link->sendTelemetry(buf);
    }
  }

  ZiMultiplex		*m_mx = nullptr;
  unsigned		m_thread;
  unsigned		m_minInterval;	// min. refresh interval in millisecs
  Zi::Path		m_alertPrefix;	// prefix for alert files
  unsigned		m_alertMaxReplay;// max. replay in days

  // telemetry thread exclusive
  Zfb::IOBuilder	m_fbb;
  Queues		m_queues;
  Engines		m_engines;
  ZmRef<Zdb>		m_db;
  WatchList		m_watchLists[ReqType::N];
  AlertQueue		m_alertQueue;		// in-memory ring of alerts
  AlertFile		m_alertFile;		// current file being written
  ZeString		m_alertBuf;		// alert message buffer
  bool			m_appUpdated = false;
};

} // Ztel

#endif /* ZtelServer_HH */
