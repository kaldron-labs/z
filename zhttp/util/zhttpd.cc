//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// reusable static HTTP server application

#include <iostream>
#include <string.h>

#ifndef _WIN32
#include <unistd.h>
#endif

#include <zlib/ZuAssert.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuUnion.hh>

#include <zlib/ZmBitmap.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmTrap.hh>

#include <zlib/ZtPlatform.hh>

#include <zlib/ZtcHash.hh>
#include <zlib/ZtcHeap.hh>

#include <zlib/ZiDaemon.hh>
#include <zlib/ZiHashTune.hh>
#include <zlib/ZiHeapTune.hh>

#include <zlib/ZhttpServer.hh>

#include "runtime.hh"
#include "zhttpd.hh"
#include "zhttpput.hh"

using namespace Zhttpd;

constexpr uint64_t ReqBodyMax = 1<<20;

void usage(int code = 1)
{
  std::cerr <<
    "Usage: zhttpd /path/to/wwwroot [OPTION]...\n\n"
    "Options:\n"
    "  --port number              listen port, default 8080\n"
    "  --addr ip                  listen address, default all interfaces\n"
    "  --ipv6                     listen on IPv6 address\n"
    "  --daemon                   detach and run in background\n"
    "  --pidfile filename         write PID beneath the temporary directory\n"
    "  --maxconn number           maximum concurrent accepted connections\n"
    "  --log filename             append access log to file, '-' for stdout\n"
    "  --syslog                   send access log to syslog\n"
    "  --index filename           directory index file, default index.html\n"
    "  --no-listing               disable generated directory listings\n"
    "  --mimetypes filename       extension to MIME map\n"
    "  --default-mimetype string  default MIME type, application/octet-stream\n"
    "  --no-keepalive             disable HTTP keep-alive\n"
    "  --single-file              serve only the specified file\n"
    "  --hide-dotfiles            reject dotfiles\n"
    "  --forward host,url         301 redirect by Host, repeatable\n"
    "  --forward-all url          301 redirect all requests\n"
    "  --forward-https            redirect HTTP requests to HTTPS\n"
    "  --no-server-id             omit server identity headers/listings\n"
    "  --timeout secs             idle connection timeout, 30 default, 0 disables\n"
    "  --auth username:password   Basic authentication\n"
    "  --http                     enable HTTP/1.1 over TCP, default\n"
    "  --https                    enable HTTP/1.1 or HTTP/2 over TLS\n"
    "  --http2=MODE               HTTP/2 mode within TLS: force, prefer,\n"
    "                             disable; default prefer\n"
    "  --http3                    enable HTTP/3 over QUIC\n"
    "  --cert path                TLS certificate for --https/--http3\n"
    "  --key path                 TLS private key for --https/--http3\n"
    "  --key-log path             append HTTP/3 TLS secrets for tshark/Wireshark;\n"
    "                             defaults to SSLKEYLOGFILE when set\n"
    "  --quic-migration=MODE      QUIC migration policy: disabled, passive,\n"
    "                             active; default passive\n"
    "  --quic-heartbeat=N         send QUIC PING after N idle seconds, 0 disables\n"
    "  --quic-migration-cid-reserve=N\n"
    "                             peer CID reserve for QUIC migration, default 1\n"
    "  --quic-migration-close-on-failure\n"
    "                             close active migration attempts on failure\n"
    "  --debug                    enable ZiMultiplex and HTTP/3 debug logging\n"
    "  --frag                     fragment ZiMultiplex I/O in debug builds\n"
    "  --yield                    yield in ZiMultiplex in debug builds\n"
#ifdef ZiMultiplex_FILTER
    "  --quic-rx-drop=N%          randomly drop N% of received QUIC UDP packets\n"
    "  --quic-tx-drop=N%          randomly drop N% of transmitted QUIC UDP packets\n"
#endif
#ifdef Zquic_DEBUG
    "  --quic-diag=N              print HTTP/3 QUIC counters every N seconds\n"
#endif
    "  --mem-diag=N               print memory counters every N seconds\n"
    "  --event-fd=N               emit process-supervision events\n"
    "  -h, --help                 show help\n" << std::flush;
  ::exit(code);
}

#ifdef ZiMultiplex_FILTER
bool parseDrop(ZuCSpan s, double &drop)
{
  drop = 0.0;
  if (!s) return true;
  if (s.length() < 2 || s[s.length() - 1] != '%') return false;
  ZuBox<double> pct;
  if (pct.scan(s.data(), s.length() - 1) != s.length() - 1) return false;
  double v = pct;
  if (v < 0.0 || v > 100.0) return false;
  drop = v * 0.01;
  return true;
}
#endif

Zhttp::Migration::T migrationMode(const Options &options)
{
  return Zhttp::migrationMode(options.quicMigration);
}

ZuTime quicHeartbeat(const Options &options)
{
  return options.quicHeartbeat ? ZuTime{options.quicHeartbeat} : ZuTime{};
}

bool loadOptions(Options &options, int argc, const char *const *argv)
{
  bool help = false;
  if (!Zhttpd::loadOptions(options, argc, argv, help)) return false;
  if (help) usage(0);
  if (Zhttp::migrationMode(options.quicMigration, -1) < 0) return false;
#ifdef ZiMultiplex_FILTER
  double drop;
  if (!parseDrop(options.quicRxDrop, drop)) return false;
  if (!parseDrop(options.quicTxDrop, drop)) return false;
#endif
  return true;
}

void printMemDiag()
{
  ZiLOG(Info, "zhttpd", ([](auto &s) {
    s << "Hash Tables:\n" << Ztc::hashCSV();
    s << "Heaps:\n" << Ztc::heapCSV();
  }));
}

bool prepareProcess(Options &options)
{
  Zi::Path pidName;
  if (options.pidfile) pidName = options.pidfile;
  int rc = ZiDaemon::init(nullptr, nullptr, -1, options.daemon, pidName);
  if (rc == ZiDaemon::Running) {
    ZiLOG(Error, "zhttpd", "PID file names a running process");
    return false;
  }
  if (rc != ZiDaemon::OK) {
    ZiLOG(Error, "zhttpd", "daemon initialization failed");
    return false;
  }
  return true;
}

using ReqHeaderList = ZhttpHeaders(
  "host",
  "authorization",
  "range",
  "if-modified-since",
  "connection",
  "referer",
  "user-agent");

using FixedRespHeaderList = ZhttpHeaders(
  "content-type",
  "date",
  "server",
  "last-modified",
  "accept-ranges",
  "content-range",
  "location",
  "www-authenticate",
  "allow",
  "connection",
  "content-length");
ZhttpHdrCatalogDerive(ReqHeaders, ReqHeaderList);
ZhttpHdrCatalogImpl(ReqHeaders)
ZhttpHdrCatalogDerive(FixedRespHeaders, FixedRespHeaderList);
ZhttpHdrCatalogImpl(FixedRespHeaders)

struct ResBuilder_ : public ZmObject, public Zhttp::ResBuilder {
  struct Mode { enum { Empty, Fixed, Generated, JSON, File }; };
  struct EmitProbe {
    template <typename Body>
    Zhttp::WriteOutcome::T operator ()(Body &) const;
  };

  using HdrCatalog = FixedRespHeaders;
  using ContentLength = ZuStringT<"content-length">;
  using FileBuf = ZiIOBufAlloc<
    FileChunk, FileChunk, "Zhttpd.FileBody">;
  using ProducerClose = ZmFn<void(),
    ZmFnHeapID<"zhttpd.FileProducerClose">>;

  template <typename Emit, typename Heap = ZuVoid>
  struct FileProducer_ : public Heap, public ZmPolymorph {
    using Self = FileProducer_;
    using Emitter = ZuUnion<void, Emit>;

    FileProducer_(
      State *state_, Emit emit_, const ZiFile &handle_,
      uint64_t offset_, uint64_t length_) :
      state{state_}, emitter{ZuMv(emit_)}, offset{offset_}, length{length_} {
      (void)handle.dup(handle_, ZiFile::GC);
    }

    bool start_() { return read_(); }
    void close_() { emitter = {}; }

    bool read_() {
      if (!emitter.template ptr<Emit>()) return false;
      unsigned n = length > FileChunk ? FileChunk : unsigned(length);
      if (!n) return false;
      ZiFile file;
      if (file.dup(handle, ZiFile::GC) != Zi::OK) return false;
      uint64_t offset_ = offset;
      bool final = n == length;
      offset += n;
      length -= n;
      state->mx->run([
	self = ZmRef<Self>{this}, file = ZuMv(file),
	offset_, n, final]() mutable {
	ZmRef<ZiIOBuf> buf = new FileBuf{};
	int r = file.pread(offset_, buf->data(), n);
	if (r == int(n)) buf->length = n;
	State *state_ = self->state;
	unsigned txThread = state_->txThread;
	state_->mx->run([
	  self = ZuMv(self), buf = ZuMv(buf), final]() mutable {
	  self->write_(ZuMv(buf), final);
	}, txThread);
      }, state->fileThread);
      return true;
    }

    void write_(ZmRef<ZiIOBuf> buf, bool final) {
      if (!emitter.template ptr<Emit>()) return;
      if (!buf || !buf->length) {
	(void)emit_([](auto &) { return Zhttp::WriteOutcome::Failed; });
	return;
      }

      (void)emit_([self = ZmRef<Self>{this}, buf = ZuMv(buf), final](
	  auto &body) mutable {
	body << buf->cspan();
	if (final) return Zhttp::WriteOutcome::End;
	if (!self->read_()) return Zhttp::WriteOutcome::Failed;
	return Zhttp::WriteOutcome::Stream;
      });
    }

    template <typename Write>
    bool emit_(Write &&write) {
      auto emitter_ = emitter.template ptr<Emit>();
      if (!emitter_) return false;
      Emit local{*emitter_};
      return local(ZuFwd<Write>(write));
    }

    State	*state;
    Emitter	emitter;
    ZiFile	handle;
    uint64_t	offset;
    uint64_t	length;
  };

  template <typename Emit>
  ZuDerive(FileProducerHeap,
    (ZmHeap<"zhttpd.FileProducer", FileProducer_<Emit>>));
  template <typename Emit>
  ZuDerive(FileProducer, (FileProducer_<Emit, FileProducerHeap<Emit>>));

  Zhttp::BodyPolicy::T bodyPolicy() const {
    if (!plan.sendBody) return Zhttp::BodyPolicy::None;
    switch (mode) {
      case Mode::Fixed:
      case Mode::Generated:
      case Mode::JSON:
	return Zhttp::BodyPolicy::Fixed;
      case Mode::File:
	return Zhttp::BodyPolicy::Stream;
      default:
	return Zhttp::BodyPolicy::None;
    }
  }
  Zhttp::Method::T method() const { return method_; }
  unsigned status() const { return plan.status; }
  template <typename Key, typename L>
  void header(L &&l) const {
    if constexpr (Key{}() == "content-type") {
      if (plan.contentType) l(plan.contentType);
    } else if constexpr (Key{}() == "date") {
      if (plan.date) l(plan.date);
    } else if constexpr (Key{}() == "server") {
      if (plan.server) l(plan.server);
    } else if constexpr (Key{}() == "last-modified") {
      if (plan.lastModified) l(plan.lastModified);
    } else if constexpr (Key{}() == "accept-ranges") {
      if (mode == Mode::File) l("bytes");
    } else if constexpr (Key{}() == "content-range") {
      if (plan.contentRange) l(plan.contentRange);
    } else if constexpr (Key{}() == "location") {
      if (plan.location) l(plan.location);
    } else if constexpr (Key{}() == "www-authenticate") {
      if (plan.wwwAuthenticate) l(plan.wwwAuthenticate);
    } else if constexpr (Key{}() == "allow") {
      if (plan.allow) l(plan.allow);
    } else if constexpr (Key{}() == "connection") {
      if (plan.connection) l(plan.connection);
    } else if constexpr (Key{}() == "content-length") {
      if (bodyPolicy() == Zhttp::BodyPolicy::Fixed)
	l(Zhttp::contentLengthPad());
      else if (!Zhttp::BodyPolicy::streaming(bodyPolicy()) &&
	  (plan.contentLength || plan.sendBody))
	l(ZuBoxed(plan.contentLength));
    }
  }
  template <typename L> void header(L &&) const { }
  bool disconnect() const { return plan.close; }

  template <typename Emit>
  void body(Emit &&emit) {
    if (mode == Mode::File) {
      using EmitResult = decltype(
	ZuDeclVal<Emit &>()(ZuDeclVal<EmitProbe>()));
      if constexpr (ZuIsSame<EmitResult, bool>{}) {
	if (!plan.fileLength) {
	  emit([](auto &) { return Zhttp::WriteOutcome::End; });
	  return;
	}
	using Producer = FileProducer<ZuDecay<Emit>>;
	ZmRef<Producer> producer = new Producer{
	  state, emit, plan.fileHandle, plan.fileOffset, plan.fileLength};
	ZmRef<Producer> start{producer};
	producerClose = ProducerClose{ProducerClose::mvFn(
	  ZuMv(producer), [](ZmRef<Producer> producer_) {
	    producer_->close_();
	  })};
	emit([start = ZuMv(start)](auto &) mutable {
	  return start->start_() ? Zhttp::WriteOutcome::Stream :
	    Zhttp::WriteOutcome::Failed;
	});
      } else {
	emit([](auto &) { return Zhttp::WriteOutcome::Failed; });
      }
      return;
    }
    emit([this](auto &body) {
      switch (mode) {
	case Mode::JSON:
	  ZfJSON::save(body, record);
	  break;
	case Mode::Fixed:
	case Mode::Generated:
	  sendSpanChunks(body, plan.body.data(), plan.body.length());
	  break;
	default:
	  break;
      }
      body.flush();
      contentLength = body.produced();
      return Zhttp::WriteOutcome::End;
    });
  }

  void close() {
    auto fn = ZuMv(producerClose);
    producerClose = {};
    if (fn) fn();
  }
  template <typename L>
  void bodyHdrs(L &&l) const {
    Zhttp::contentLengthSet(l, contentLength);
  }

  ResponsePlan		plan;
  ZhttpPut::Record	record;
  State			*state = nullptr;
  Zhttp::Method::T	method_ = -1;
  unsigned		contentLength = 0;
  int8_t		mode = Mode::Empty;
  ProducerClose		producerClose;
};

using ResBuilderQ = ZmList<ResBuilder_,
  ZmListNode<ResBuilder_, ZmListHeapID<"zhttpd.ResBuilder">>>;
using ResBuilder = ResBuilderQ::Node;
ZuAssert((ZuIsSame<ResBuilderQ::HeapID, ZuStringT<"zhttpd.ResBuilder">>{}));

struct App;

template <typename T, typename = void>
struct HasHttp10 : public ZuFalse { };
template <typename T>
struct HasHttp10<T, decltype(ZuDeclVal<const T &>().http10(), void())> :
  public ZuTrue { };

struct Parser : public Zhttp::Parser {
  using HdrCatalog = ReqHeaders;

  void init(App &app_) { app = &app_; }

  bool operation(
      Zhttp::Method::T method, Zhttp::Target &target) {
    request.method = method;
    request.target = target.raw;
    request.authority = target.authority.raw;
    request.protocol = target.protocol;
    request.form = target.form;
    int q = target.path.find([](auto c) { return c == '?'; });
    request.path_ = q < 0 ? target.path :
      ZuBSpan{target.path.data(), unsigned(q)};
    request.query_ = q < 0 ? ZuBSpan{} : ZuBSpan{
      target.path.data() + q + 1,
      target.path.length() - unsigned(q + 1)};
    request.hasQuery = q >= 0;
    return true;
  }
    bool bodyInfo(Zhttp::BodyType::T, uint64_t) { return true; }
    template <typename Key>
    void header(Zhttp::FieldSection::T, ZuSpan<uint8_t> value) {
      if constexpr (Key{}() == "host") request.host = value;
      else if constexpr (Key{}() == "authorization")
	request.authorization = value;
      else if constexpr (Key{}() == "range") request.range = value;
      else if constexpr (Key{}() == "if-modified-since")
	request.ifModifiedSince = value;
      else if constexpr (Key{}() == "connection")
	request.connection = value;
      else if constexpr (Key{}() == "referer")
	request.referer = value;
      else if constexpr (Key{}() == "user-agent")
	request.userAgent = value;
    }
    template <typename Rx>
    bool body(Rx &rx) {
      return Zhttp::bodyEach(rx,
	[this](ZuSpan<uint8_t> span) {
	  request.bodyData << span;
	  request.bodyReceived += span.length();
	  request.bodyConsumed += span.length();
	});
    }

  template <typename Link>
  void complete(Link *link, bool ok);

  void reset() {
    app = nullptr;
    request = {};
  }

  App		*app = nullptr;
  RequestData	request;
};

struct App {
  using Parser = ::Parser;
  using ResBuilderQ = ::ResBuilderQ;
  using WorkFn = ZmFn<void(), ZmFnHeapID<"zhttpd.WorkFn">>;

  struct Work_ : public ZmObject {
    Work_(WorkFn run_) : run{ZuMv(run_)} { }

    WorkFn run;
  };
  using WorkQ = ZmList<Work_,
    ZmListNode<Work_, ZmListHeapID<"zhttpd.Work">>>;

  App(State *state_) : state{state_} { }

  void enqueue(WorkFn fn) {
    ++m_pending;
    bool post = false;
    {
      ZmGuard<ZmLock> guard(m_workLock);
      m_work.push(ZuMv(fn));
      if (!m_working) {
	m_working = true;
	post = true;
      }
    }
    if (post)
      state->mx->run([this]() { work_(); }, state->fileThread);
  }

  template <typename LinkRef>
  void respond(LinkRef link, RequestData request) {
    ++state->requests;
    ZmRef<ResBuilder> response = new ResBuilder{};
    response->state = state;
    response->method_ = request.method;
    if (request.method == Zhttp::Method::PUT) {
      ZhttpPut::Record record;
      if (request.bodyReceived != request.bodyData.length() ||
	  request.bodyConsumed != request.bodyData.length() ||
	  request.bodyReset || request.bodyDiscarded ||
	  !ZhttpPut::load(record, request.bodyData)) {
	response->plan.status = 400;
      } else {
	response->plan.status = 200;
	response->plan.contentType = "application/json";
	response->plan.sendBody = true;
	response->plan.generated = true;
	response->record = ZuMv(record);
	response->mode = ResBuilder::Mode::JSON;
      }
    } else {
      StaticPlanner planner{state};
      response->plan = planner.plan(request);
    }
    if (response->mode != ResBuilder::Mode::JSON) {
      if (response->plan.file)
	response->mode = ResBuilder::Mode::File;
      else if (response->plan.generated)
	response->mode = ResBuilder::Mode::Generated;
      else if (response->plan.sendBody)
	response->mode = ResBuilder::Mode::Fixed;
    }
    state->log.write(request, request, response->plan, request.remoteIP);
    link->send(ZuMv(response));
    completed_();
  }

  void completed_() {
#ifndef _WIN32
    if (state->options.eventFD >= 0) {
      uint8_t value = 0x80;
      (void)::write(state->options.eventFD, &value, 1);
    }
#endif
  }
  void listening(int transport, unsigned port) {
    ZiLOG(Info, "zhttpd", ([transport, port](auto &s) {
      switch (transport) {
	case Zhttp::Transport::QUIC: s << "h3"; break;
	case Zhttp::Transport::TLS: s << "https"; break;
	default: s << "http"; break;
      }
      s << " listening: " << port;
    }));
#ifndef _WIN32
    if (state->options.eventFD >= 0) {
      uint8_t value = uint8_t(transport);
      (void)::write(state->options.eventFD, &value, 1);
    }
#endif
  }
  void listenFailed(int, bool) {
    state->errors = 1;
    if (state->done) state->done();
  }
  void connected(int) { }
  void disconnected(int) { }

private:
  void work_() {
    for (;;) {
      typename WorkQ::NodeMvRef work;
      {
	ZmGuard<ZmLock> guard(m_workLock);
	work = m_work.shift();
	if (!work) {
	  m_working = false;
	  return;
	}
      }
      work->run();
      if (!--m_pending) m_drained.post();
    }
  }

public:
  void drain() {
    while (m_pending.load_()) m_drained.wait();
  }

  State *state = nullptr;
private:
  ZmLock	m_workLock;
  WorkQ		m_work;
  ZmSemaphore	m_drained;
  ZmAtomic<unsigned> m_pending = 0;
  bool		m_working = false;
};

template <typename Link>
void Parser::complete(Link *link, bool ok)
{
  if (!ok) {
    ZiLOG(Error, "zhttpd", ([target = ZuMv(request.target)](auto &s) {
      s << "request parse failed";
      if (target) s << ": " << target;
    }));
    return;
  }
  request.remoteIP = link->remoteIP();
  request.secure = Link::TLS;
  using ServerParser = ZuDecay<decltype(link->rxState().parser)>;
  if constexpr (HasHttp10<ServerParser>{})
    request.http10 = link->rxState().parser.http10();
  auto link_ = ZmRef(link);
  auto request_ = ZuMv(request);
  app->enqueue(App::WorkFn{
    [app = app, link = ZuMv(link_), request = ZuMv(request_)]() mutable {
      app->respond(ZuMv(link), ZuMv(request));
    }});
}

ZiMxParams mxParams(const Options &options)
{
  auto params = ZiMxParams()
    .scheduler([](auto &s) {
      s.nThreads(5)
	.thread(1, [](auto &t) { t.isolated(1); })
	.thread(2, [](auto &t) { t.isolated(1); })
	.thread(3, [](auto &t) { t.isolated(1); })
	.thread(4, [](auto &t) { t.isolated(1); })
	.thread(5, [](auto &t) { t.isolated(1); });
    })
    .rxThread(1).txThread(2);
#ifdef ZiMultiplex_DEBUG
  if (options.debug) params.debug(true);
  if (options.frag) params.frag(true);
  if (options.yield) params.yield(true);
#else
  (void)options;
#endif
  return params;
}

using Server = Zhttp::Server<App>;

struct Zhttpd::Application::Impl {
  Impl(Options options) :
    mx{mxParams(options)}, app{&state}
  {
    state.options = ZuMv(options);
  }

  State		state;
  ZiMultiplex	mx;
  App		app;
  Server	server;
  bool		mxStarted = false;
  bool		serverInited = false;
  bool		serverStarted = false;
};

Zhttpd::Application::Application() = default;

Zhttpd::Application::~Application()
{
  final();
}

bool Zhttpd::Application::init(Options options, ZeString &error)
{
  if (m_impl) return false;
  m_impl = new Impl{ZuMv(options)};
  auto &state = m_impl->state;
  if (!initFileState(state, error)) {
    final();
    return false;
  }
  state.mime.init(state.options);
  if (!state.log.init(state.options)) {
    error = "failed to open access log";
    final();
    return false;
  }
  return true;
}

void Zhttpd::Application::done(
  ZmFn<void(), ZmFnHeapID<"zhttpd.Done">> fn)
{
  if (m_impl) m_impl->state.done = ZuMv(fn);
}

bool Zhttpd::Application::startMultiplex()
{
  if (!m_impl || m_impl->mxStarted || !m_impl->mx.start()) return false;
  m_impl->mxStarted = true;
  m_impl->state.mx = &m_impl->mx;
  m_impl->state.txThread = m_impl->mx.sid("4");
  m_impl->state.fileThread = m_impl->mx.sid("5");
  return true;
}

bool Zhttpd::Application::initServer()
{
  if (!m_impl || !m_impl->mxStarted || m_impl->serverInited) return false;
  auto &state = m_impl->state;
  auto &options = state.options;
  auto config = Zhttp::ServerConfig()
    .localIP(ZiIP(options.addr))
    .port(options.port)
    .idleTimeout(options.timeout)
    .maxConnections(options.maxconn)
    .retainedBodyMax(ReqBodyMax);
  if (options.http) config.tcp();
  if (options.https) {
    int8_t policy;
    switch (options.http2) {
      case Http2Mode::force: policy = Zhttp::H2Policy::Force; break;
      case Http2Mode::disable: policy = Zhttp::H2Policy::Disable; break;
      default: policy = Zhttp::H2Policy::Prefer; break;
    }
    config.tls(Zhttp::H2Config{}
      .certPath(options.cert).keyPath(options.key).policy(policy));
  }
  if (options.http3) {
    double rxDrop = 0, txDrop = 0;
#ifdef ZiMultiplex_FILTER
    (void)parseDrop(options.quicRxDrop, rxDrop);
    (void)parseDrop(options.quicTxDrop, txDrop);
#endif
    config.quic(Zhttp::QUICConfig{}
      .certPath(options.cert).keyPath(options.key)
      .keyLogPath(options.keyLog)
      .heartbeat(quicHeartbeat(options))
      .migration(migrationMode(options))
      .migrationCIDReserve(options.quicMigrationCIDReserve)
      .migrationCloseOnFailure(options.quicMigrationCloseOnFailure)
      .rxDrop(rxDrop).txDrop(txDrop));
  }
  m_impl->server.txErrorFn(ZiTxErrorFn{[](ZeException &e) {
    ZiLOG(Error, "zhttpd", ([e](auto &s) {
      s << "transmit error: " << e;
    }));
    return false;
  }});
  m_impl->serverInited = m_impl->server.init(
    Zhttp::HubConfig{&m_impl->mx, "3", "4"},
    ZuMv(config), &m_impl->app);
  return m_impl->serverInited;
}

bool Zhttpd::Application::startServer()
{
  if (!m_impl || !m_impl->serverInited || m_impl->serverStarted)
    return false;
  m_impl->serverStarted = m_impl->server.start();
  return m_impl->serverStarted;
}

bool Zhttpd::Application::stopServer()
{
  if (!m_impl || !m_impl->serverInited) return true;
  bool ok = m_impl->server.stop();
  m_impl->app.drain();
  m_impl->serverStarted = false;
  return ok;
}

void Zhttpd::Application::finalServer()
{
  if (!m_impl || !m_impl->serverInited) return;
  m_impl->server.final();
  m_impl->serverInited = false;
  m_impl->serverStarted = false;
}

void Zhttpd::Application::stopMultiplex()
{
  if (!m_impl || !m_impl->mxStarted) return;
  m_impl->mx.stop();
  m_impl->mxStarted = false;
}

void Zhttpd::Application::final()
{
  if (!m_impl) return;
  if (m_impl->serverStarted) (void)stopServer();
  finalServer();
  stopMultiplex();
  m_impl->state.log.final();
  delete m_impl;
  m_impl = nullptr;
}

unsigned Zhttpd::Application::errors() const
{
  return m_impl ? unsigned(m_impl->state.errors.load_()) : 1;
}

#ifdef Zquic_DEBUG
void Zhttpd::Application::printQUICDiag()
{
  if (m_impl && m_impl->serverInited) m_impl->server.printQUICDiag();
}
#endif

#ifndef ZHTTPD_LIBRARY
int main(int argc, char **argv)
{
  ZiHeapTune::load();
  ZiHashTune::load();

  Options options;
  try {
    if (!::loadOptions(options, argc, argv)) usage();
  } catch (const ZeException &e) {
    std::cerr << e << '\n';
    usage();
  }

  ZeString error;
  if (!validate(options, error)) {
    std::cerr << "zhttpd: " << error << '\n' << std::flush;
    return 1;
  }

  ZiLog::init("zhttpd", options.syslog ? "daemon" : "user");
  ZiLog::level(options.debug ? Ze::Debug :
#ifdef Zquic_DEBUG
      options.quicDiag ? Ze::Info :
#endif
      options.memDiag ? Ze::Debug : Ze::Info);
  if (options.syslog)
    ZiLog::sink(ZiLog::sysSink());
  else if (options.logPath && options.logPath != "-")
    ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path(options.logPath)));
  else
    ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();

  ZmTrap::sigintFn(ZhttpUtil::Runtime::post);
  ZmTrap::trap();

  if (!prepareProcess(options)) {
    ZiLog::stop();
    return 1;
  }

  State state;
  state.options = options;
  state.done = []() { ZhttpUtil::Runtime::post(); };
  if (!initFileState(state, error)) {
    ZiLOG(Error, "zhttpd", ([error = ZuMv(error)](auto &s) mutable {
      s << "zhttpd: " << error;
    }));
    ZiLog::stop();
    return 1;
  }
  state.mime.init(state.options);
  if (!state.log.init(state.options)) {
    ZiLOG(Error, "zhttpd", "failed to open access log");
    ZiLog::stop();
    return 1;
  }

  ZiMultiplex mx(mxParams(options));
  if (!mx.start()) {
    ZiLOG(Error, "zhttpd", "ZiMultiplex start failed");
    state.log.final();
    ZiLog::stop();
    return 1;
  }
  state.mx = &mx;
  state.txThread = mx.sid("4");
  state.fileThread = mx.sid("5");
  App app{&state};
  Server server;
  server.txErrorFn(ZiTxErrorFn{[](ZeException &e) {
    ZiLOG(Error, "zhttpd", ([e](auto &s) {
      s << "transmit error: " << e;
    }));
    return false;
  }});
  auto serverConfig = Zhttp::ServerConfig()
    .localIP(ZiIP(state.options.addr))
    .port(state.options.port)
    .idleTimeout(state.options.timeout)
    .maxConnections(state.options.maxconn)
    .retainedBodyMax(ReqBodyMax);
  if (state.options.http) {
    serverConfig.tcp();
  }
  if (state.options.https) {
    int8_t policy;
    switch (state.options.http2) {
      case Http2Mode::force: policy = Zhttp::H2Policy::Force; break;
      case Http2Mode::disable: policy = Zhttp::H2Policy::Disable; break;
      default: policy = Zhttp::H2Policy::Prefer; break;
    }
    serverConfig.tls(Zhttp::H2Config{}
      .certPath(state.options.cert).keyPath(state.options.key)
      .policy(policy));
  }
#ifdef Zquic_DEBUG
  bool h3Enabled = false;
#endif
  if (state.options.http3) {
    double rxDrop = 0, txDrop = 0;
#ifdef ZiMultiplex_FILTER
    (void)parseDrop(state.options.quicRxDrop, rxDrop);
    (void)parseDrop(state.options.quicTxDrop, txDrop);
#endif
    serverConfig.quic(Zhttp::QUICConfig{}
	    .certPath(state.options.cert).keyPath(state.options.key)
	    .keyLogPath(state.options.keyLog)
	    .heartbeat(quicHeartbeat(state.options))
	    .migration(migrationMode(state.options))
	    .migrationCIDReserve(state.options.quicMigrationCIDReserve)
	    .migrationCloseOnFailure(
	      state.options.quicMigrationCloseOnFailure)
	    .rxDrop(rxDrop).txDrop(txDrop));
#ifdef Zquic_DEBUG
    h3Enabled = true;
#endif
  }
  bool serverInited = server.init(
    Zhttp::HubConfig{&mx, "3", "4"},
    ZuMv(serverConfig), &app);
  if (!serverInited) {
    ZiLOG(Error, "zhttpd", "HTTP server initialization failed");
    server.final();
    mx.stop();
    state.log.final();
    ZiLog::stop();
    return 1;
  }
  if (!server.start()) {
    ZiLOG(Error, "zhttpd", "HTTP hub start failed");
    (void)server.stop();
    server.final();
    mx.stop();
    state.log.final();
    ZiLog::stop();
    return 1;
  }
  unsigned memElapsed = 0;
#ifdef Zquic_DEBUG
  unsigned quicElapsed = 0;
#endif
  for (;;) {
    unsigned step = state.options.memDiag ?
      state.options.memDiag - memElapsed : 0;
#ifdef Zquic_DEBUG
    if (h3Enabled && state.options.quicDiag) {
      unsigned left = state.options.quicDiag - quicElapsed;
      if (!step || left < step) step = left;
    }
#endif
    if (!step) {
      ZhttpUtil::Runtime::wait();
      break;
    }
    if (ZhttpUtil::Runtime::wait(step)) break;
    if (state.options.memDiag &&
	(memElapsed += step) >= state.options.memDiag) {
      memElapsed = 0;
      printMemDiag();
    }
#ifdef Zquic_DEBUG
    if (h3Enabled && state.options.quicDiag &&
	(quicElapsed += step) >= state.options.quicDiag) {
      quicElapsed = 0;
      server.printQUICDiag();
    }
#endif
  }
  if (!server.stop()) state.errors = 1;
  app.drain();
  server.final();
  mx.stop();
  state.log.final();
  ZiLog::stop();
  return state.errors ? 1 : 0;
}
#endif
