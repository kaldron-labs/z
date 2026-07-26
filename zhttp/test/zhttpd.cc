//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// static HTTP server

#include <iostream>
#include <signal.h>
#include <string.h>

#include <zlib/ZmBitmap.hh>

#include <zlib/ZtcHash.hh>
#include <zlib/ZtcHeap.hh>

#include <zlib/ZiDaemon.hh>
#include <zlib/ZiHashCSV.hh>
#include <zlib/ZiHeapCSV.hh>

#include <zlib/Zhttp.hh>

#include "Zhttpd.hh"

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
    "  --pidfile filename         write PID to file\n"
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
    "  --https                    enable HTTP/1.1 over TLS\n"
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

int8_t migrationMode(const Options &options)
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

struct IntervalMonitor {
  bool active() const {
    return memDiag
#ifdef Zquic_DEBUG
      || quicDiag
#endif
      ;
  }
  unsigned nextStep() const {
    unsigned step = 0;
    auto limit = [&step](unsigned v) {
      if (!step || v < step) step = v;
    };
    if (memDiag) limit(memDiag - memElapsed);
#ifdef Zquic_DEBUG
    if (quicDiag) limit(quicDiag - quicElapsed);
#endif
    return step;
  }
  void advance(unsigned step) {
    if (memDiag) memElapsed += step;
#ifdef Zquic_DEBUG
    if (quicDiag) quicElapsed += step;
#endif
  }
  template <typename MemFn>
  void intervals(MemFn memFn) {
    if (memDiag && memElapsed >= memDiag) {
      memElapsed = 0;
      memFn();
    }
  }
#ifdef Zquic_DEBUG
  template <typename MemFn, typename QuicFn>
  void intervals(MemFn memFn, QuicFn quicFn) {
    if (quicDiag && quicElapsed >= quicDiag) {
      quicElapsed = 0;
      quicFn();
    }
    intervals(memFn);
  }
#endif

  uint32_t	memDiag = 0;
#ifdef Zquic_DEBUG
  uint32_t	quicDiag = 0;
#endif
  unsigned	memElapsed = 0;
#ifdef Zquic_DEBUG
  unsigned	quicElapsed = 0;
#endif
};

static ZmSemaphore *sigDone_;

static void sigHandler(int)
{
  if (sigDone_) sigDone_->post();
}

static void installSigHandlers(
  ZmSemaphore *done, struct sigaction &oldInt, struct sigaction &oldTerm)
{
  sigDone_ = done;
  struct sigaction action{};
  action.sa_handler = sigHandler;
  sigemptyset(&action.sa_mask);
  sigaction(SIGINT, &action, &oldInt);
  sigaction(SIGTERM, &action, &oldTerm);
}

static void restoreSigHandlers(
  const struct sigaction &oldInt, const struct sigaction &oldTerm)
{
  sigaction(SIGINT, &oldInt, nullptr);
  sigaction(SIGTERM, &oldTerm, nullptr);
  sigDone_ = nullptr;
}

bool prepareProcess(Options &options)
{
  const char *pidfile = options.pidfile ? options.pidfile.data() : nullptr;
  int rc = ZiDaemon::init(nullptr, nullptr, -1, options.daemon, pidfile);
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

using ReqHeaders = ZhttpHeaders(
  "host",
  "authorization",
  "range",
  "if-modified-since",
  "connection",
  "referer",
  "user-agent");

using RespHeaders = ZhttpHeaders(
  "content-type",
  "date",
  "server",
  "last-modified",
  "accept-ranges",
  "content-range",
  "location",
  "www-authenticate",
  "allow",
  "connection");

struct ReqSink {
  void reset() { req = {}; complete_ = false; failed = false; }
  void operation(Zhttp::Method::T method_, ZuBSpan target_) {
    req.method = method_;
    req.target = ZuCSpan{target_};
  }
  void version(ZuBSpan version_) { req.http10 = ZuCSpan{version_} == "HTTP/1.0"; }
  template <typename Key> void header(ZuBSpan value) {
    if constexpr (Key{}() == "host")
      req.host = ZuCSpan{value};
    else if constexpr (Key{}() == "authorization")
      req.authorization = ZuCSpan{value};
    else if constexpr (Key{}() == "range")
      req.range = ZuCSpan{value};
    else if constexpr (Key{}() == "if-modified-since")
      req.ifModifiedSince = ZuCSpan{value};
    else if constexpr (Key{}() == "connection")
      req.connection = ZuCSpan{value};
    else if constexpr (Key{}() == "referer")
      req.referer = ZuCSpan{value};
    else if constexpr (Key{}() == "user-agent")
      req.userAgent = ZuCSpan{value};
  }
  void contentLength(uint64_t) { }
  void body(ZuBSpan) { }
  template <typename ParserState>
  void complete(typename ParserState::T state) {
    complete_ = state == ParserState::Complete;
    failed = !complete_;
  }

  RequestData	req;
  bool		complete_ = false;
  bool		failed = false;
};

template <typename Impl, bool H3>
struct ReqParserBase_;
template <typename Impl>
struct ReqParserBase_<Impl, false> :
  public Zhttp::H1ReqParser<Impl, ReqHeaders, ReqBodyMax> { };
template <typename Impl>
struct ReqParserBase_<Impl, true> :
  public Zhttp::H3ReqParser<Impl, ReqHeaders, ReqBodyMax> { };

template <bool H3>
struct ReqParser :
  public ReqParserBase_<ReqParser<H3>, H3>,
  public ReqSink {
  using Base = ReqParserBase_<ReqParser<H3>, H3>;
  using State = typename Base::State;
  void reset() { Base::reset(); ReqSink::reset(); }
  void complete(State::T state) { ReqSink::template complete<State>(state); }
  using ReqSink::body;
  using ReqSink::contentLength;
  using ReqSink::header;
  using ReqSink::operation;
  using ReqSink::version;

};

struct RespOps {
  RespOps(const ResponsePlan *plan_) : plan{plan_} { }

  unsigned status() const { return plan->status; }
  template <typename L> void reason(L &&l) const { l(plan->reason); }
  uint64_t contentLength() const { return plan->contentLength; }
  template <typename Key, typename L>
  void header(L &&l) const {
    if constexpr (Key{}() == "content-type")
      l(plan->contentType);
    else if constexpr (Key{}() == "date")
      l(plan->date);
    else if constexpr (Key{}() == "server")
      l(plan->server);
    else if constexpr (Key{}() == "last-modified")
      l(plan->lastModified);
    else if constexpr (Key{}() == "accept-ranges")
      l(plan->file ? ZuCSpan{"bytes"} : ZuCSpan{});
    else if constexpr (Key{}() == "content-range")
      l(plan->contentRange);
    else if constexpr (Key{}() == "location")
      l(plan->location);
    else if constexpr (Key{}() == "www-authenticate")
      l(plan->wwwAuthenticate);
    else if constexpr (Key{}() == "allow")
      l(plan->allow);
    else if constexpr (Key{}() == "connection")
      l(plan->connection);
    else
      l("");
  }

  const ResponsePlan	*plan = nullptr;
};

struct H1RespBuilder :
  public Zhttp::H1RespBuilder<H1RespBuilder, RespHeaders, ZuTypeList<>, true>,
  public RespOps {
  using Base =
    Zhttp::H1RespBuilder<H1RespBuilder, RespHeaders, ZuTypeList<>, true>;
  H1RespBuilder(const ResponsePlan *plan_) : RespOps{plan_} { }
  using Base::body;
  using RespOps::contentLength;
  using RespOps::header;
  using RespOps::reason;
  using RespOps::status;
};

struct H3RespBuilder :
  public Zhttp::H3RespBuilder<H3RespBuilder, RespHeaders, ZuTypeList<>, true>,
  public RespOps {
  using Base =
    Zhttp::H3RespBuilder<H3RespBuilder, RespHeaders, ZuTypeList<>, true>;
  H3RespBuilder(const ResponsePlan *plan_) : RespOps{plan_} { }
  using Base::body;
  using RespOps::contentLength;
  using RespOps::header;
  using RespOps::reason;
  using RespOps::status;

};

template <typename Tx, typename Builder>
void sendBody(Tx &tx, Builder &builder, const ResponsePlan &resp) {
  if (!resp.sendBody) return;
  auto body = builder.body(tx);
  if (resp.generated) {
    sendSpanChunks(body, resp.body.data(), resp.body.length());
    return;
  }
  if (!resp.file || !resp.fileLength) return;
  ZiFile file;
  if (file.dup(resp.fileHandle, ZiFile::GC) != Zi::OK)
    return;
  char buf[FileChunk];
  uint64_t offset = resp.fileOffset;
  uint64_t left = resp.fileLength;
  while (left) {
    unsigned n = left > FileChunk ? FileChunk : unsigned(left);
    int r = file.pread(offset, buf, n);
    if (r <= 0) break;
    sendSpanChunks(body, buf, unsigned(r));
    offset += r;
    left -= r;
  }
}

template <typename Protocol> struct Message;
template <> struct Message<Zhttp::TCP> {
  using Parser = ReqParser<false>;
  using Builder = H1RespBuilder;
};
template <> struct Message<Zhttp::TLS> : public Message<Zhttp::TCP> { };
template <> struct Message<Zhttp::QUIC> {
  using Parser = ReqParser<true>;
  using Builder = H3RespBuilder;
};

template <typename Protocol>
struct StaticServer :
  public Zhttp::ServerSession<
    StaticServer<Protocol>, typename Message<Protocol>::Parser> {
  using Parser = typename Message<Protocol>::Parser;
  using Builder = typename Message<Protocol>::Builder;
  using Base = Zhttp::ServerSession<StaticServer, Parser>;
  using Base::parser;

  template <typename Link>
  int error(Link &link, Parser &) {
    link.app()->state->errors = 1;
    return -1;
  }

  template <typename Link>
  int request(Link &link, Parser &parser) {
    ++link.app()->state->requests;
    parser.req.h3 = Link::Multiplexed;
    parser.req.tls = Link::TLS;
    StaticPlanner planner{link.app()->state};
    auto resp = planner.plan(parser.req);
    Builder builder{&resp};
    auto tx = link.transmit(builder);
    builder.response(tx);
    sendBody(tx, builder, resp);
    builder.finish(tx);
    link.finish();
    link.app()->state->log.write(parser.req, resp, link.remote());
    return Link::Multiplexed ? 1 : (resp.close ? -1 : 1);
  }
};

ZiMxParams mxParams(const Options &options)
{
  auto params = ZiMxParams()
    .scheduler([](auto &s) {
      s.nThreads(4)
	.thread(1, [](auto &t) { t.isolated(1); })
	.thread(2, [](auto &t) { t.isolated(1); })
	.thread(3, [](auto &t) { t.isolated(1); })
	.thread(4, [](auto &t) { t.isolated(1); });
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

template <typename Protocol> struct AppServer;
template <typename Protocol> struct AppServerLink;

template <typename Protocol>
struct AppServer :
  public Zhttp::Server<AppServer<Protocol>, Protocol> {
  using Base = Zhttp::Server<AppServer<Protocol>, Protocol>;
  using Link = AppServerLink<Protocol>;

  State	*state = nullptr;

  AppServer(State *state_) : state{state_} { }

  ZiIP localIP() const { return ZiIP(state->options.addr); }
  unsigned localPort() const { return state->options.port; }
  unsigned idleTimeout() const { return state->options.timeout; }
  template <typename Info>
  bool admit(const Info &) {
    unsigned active = ++state->active;
    if (!state->options.maxconn || active <= state->options.maxconn)
      return true;
    --state->active;
    return false;
  }
  void release() { --state->active; }
  template <typename Info>
  void listening(const Info &info) {
    listening_(info.port);
  }
  void listening() {
    listening_(state->options.port);
  }
  void listenFailed(bool) {
    state->errors = 1;
    state->done.post();
  }

private:
  void listening_(unsigned port) {
    ZiLOG(Info, "zhttpd", ([port](auto &s) {
      if constexpr (ZuIsSame<Protocol, Zhttp::QUIC>{})
	s << "h3";
      else if constexpr (ZuIsSame<Protocol, Zhttp::TLS>{})
	s << "https";
      else
	s << "http";
      s << " listening: " << port;
    }));
  }
};

template <typename Protocol>
struct AppServerLink :
  public Zhttp::ServerLink<
    AppServer<Protocol>, AppServerLink<Protocol>, Protocol,
    StaticServer<Protocol>> {
  using Base = Zhttp::ServerLink<
    AppServer<Protocol>, AppServerLink<Protocol>, Protocol,
    StaticServer<Protocol>>;
  using Base::Base;
};

using HTTPServer = AppServer<Zhttp::TCP>;
using TLSServer = AppServer<Zhttp::TLS>;
using H3Server = AppServer<Zhttp::QUIC>;

int Zhttpd::run(int argc, const char *const *argv)
{
  ZiHeapCSV::init(::getenv("Z_HEAPTUNE"));
  ZiHashCSV::init(::getenv("Z_HASHTUNE"));

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

  if (!prepareProcess(options)) {
    ZiLog::stop();
    return 1;
  }

  State state;
  state.options = options;
  struct sigaction oldInt{};
  struct sigaction oldTerm{};
  installSigHandlers(&state.done, oldInt, oldTerm);
  if (!initFileState(state, error)) {
    ZiLOG(Error, "zhttpd", ([error = ZuMv(error)](auto &s) mutable {
      s << "zhttpd: " << error;
    }));
    restoreSigHandlers(oldInt, oldTerm);
    ZiLog::stop();
    return 1;
  }
  state.mime.init(state.options);
  if (!state.log.init(state.options)) {
    ZiLOG(Error, "zhttpd", "failed to open access log");
    restoreSigHandlers(oldInt, oldTerm);
    ZiLog::stop();
    return 1;
  }

  ZiMultiplex mx(mxParams(options));
  if (!mx.start()) {
    ZiLOG(Error, "zhttpd", "ZiMultiplex start failed");
    restoreSigHandlers(oldInt, oldTerm);
    state.log.final();
    ZiLog::stop();
    return 1;
  }
  HTTPServer http{&state};
  TLSServer tls{&state};
  H3Server h3{&state};
  Zhttp::Engines engines;
  if (state.options.http) {
    if (!engines.init(http,
	  Zhttp::EngineConfig{&mx, "3", "4"}, Zhttp::TCPConfig{})) {
      ZiLOG(Error, "zhttpd", "HTTP server initialization failed");
      restoreSigHandlers(oldInt, oldTerm);
      engines.final();
      mx.stop();
      state.log.final();
      ZiLog::stop();
      return 1;
    }
  }
  if (state.options.https) {
    if (!engines.init(tls,
	  Zhttp::EngineConfig{&mx, "3", "4"},
	  Zhttp::TLSConfig{}
	    .certPath(state.options.cert).keyPath(state.options.key))) {
      ZiLOG(Error, "zhttpd", "HTTPS server initialization failed");
      restoreSigHandlers(oldInt, oldTerm);
      engines.final();
      mx.stop();
      state.log.final();
      ZiLog::stop();
      return 1;
    }
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
    if (!engines.init(h3,
	  Zhttp::EngineConfig{&mx, "3", "4"},
	  Zhttp::QUICConfig{}
	    .certPath(state.options.cert).keyPath(state.options.key)
	    .keyLogPath(state.options.keyLog)
	    .heartbeat(quicHeartbeat(state.options))
	    .migration(migrationMode(state.options))
	    .migrationCIDReserve(state.options.quicMigrationCIDReserve)
	    .migrationCloseOnFailure(
	      state.options.quicMigrationCloseOnFailure)
	    .rxDrop(rxDrop).txDrop(txDrop))) {
      ZiLOG(Error, "zhttpd", "H3 server initialization failed");
      restoreSigHandlers(oldInt, oldTerm);
      engines.final();
      mx.stop();
      state.log.final();
      ZiLog::stop();
      return 1;
    }
#ifdef Zquic_DEBUG
    h3Enabled = true;
#endif
  }
  if (!engines.count()) {
    ZiLOG(Error, "zhttpd", "no transport enabled");
    restoreSigHandlers(oldInt, oldTerm);
    engines.final();
    mx.stop();
    state.log.final();
    ZiLog::stop();
    return 1;
  }
  if (!engines.start()) {
    ZiLOG(Error, "zhttpd", "HTTP engine start failed");
    restoreSigHandlers(oldInt, oldTerm);
    engines.final();
    mx.stop();
    state.log.final();
    ZiLog::stop();
    return 1;
  }
  IntervalMonitor mon{state.options.memDiag
#ifdef Zquic_DEBUG
    , h3Enabled ? state.options.quicDiag : 0
#endif
  };
  if (mon.active()) {
    for (;;) {
      unsigned step = mon.nextStep();
      if (!step) break;
      if (!state.done.timedwait(Zm::now(step))) break;
      mon.advance(step);
      mon.intervals(
	[]() { printMemDiag(); }
#ifdef Zquic_DEBUG
	, [&h3]() { h3.printDiag(); }
#endif
      );
    }
  } else
    state.done.wait();
  if (!engines.stop()) state.errors = 1;
  engines.final();
  restoreSigHandlers(oldInt, oldTerm);
  mx.stop();
  state.log.final();
  ZiLog::stop();
  return state.errors ? 1 : 0;
}

#ifndef ZHTTPD_NO_MAIN
int main(int argc, char **argv)
{
  return Zhttpd::run(argc, argv);
}
#endif
