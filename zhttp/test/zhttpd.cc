//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// static HTTP server

#include <iostream>
#include <string.h>

#include <zlib/ZmBitmap.hh>

#include <zlib/ZtcHash.hh>
#include <zlib/ZtcHeap.hh>

#include <zlib/ZiDaemon.hh>
#include <zlib/ZiHashCSV.hh>
#include <zlib/ZiHeapCSV.hh>

#include <zlib/Zhttp.hh>
#include <zlib/ZhttpService.hh>

#include "Zhttpd.hh"
#include "ZhttpPut.hh"

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

using FixedRespHeaders = ZhttpHeaders(
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

struct ResponseCompletion : public ZmObject {
  unsigned	status = 0;
  uint64_t	contentLength = 0;
};

template <typename Policy_, typename Headers_>
struct ResponseBuilderBase {
  using BodyPolicy = Policy_;
  using Headers = Headers_;

  ResponsePlan plan;
  ZmRef<ResponseCompletion> completion;

  unsigned status() const { return plan.status; }
  template <typename L>
  void reason(L &&l) const { l(plan.reason); }
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
      if (plan.file) l("bytes");
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
      if constexpr (ZuIsSame<Policy_, Zhttp::Body::Fixed>{})
	l(Zhttp::HeaderPad{10, '0'});
      else if (plan.contentLength || plan.sendBody)
	l(ZuBoxed(plan.contentLength));
    }
  }
  template <typename L> void header(L &&) const { }
  bool close() const { return plan.close; }
};

struct FixedResponse :
  public ResponseBuilderBase<Zhttp::Body::Fixed, FixedRespHeaders> {
  using Base = ResponseBuilderBase<Zhttp::Body::Fixed, FixedRespHeaders>;
  using ContentLength = ZuStringT<"content-length">;

  ZhttpPut::Record record;
  unsigned contentLength = 0;
  bool json = false;

  template <typename Emit>
  void body(Emit &&emit) {
    emit([this](auto &body) {
      if (json)
	ZfJSON::save(body, record);
      else
	sendSpanChunks(body, plan.body.data(), plan.body.length());
      body.flush();
      contentLength = body.produced();
      completion->contentLength = contentLength;
    });
  }
  template <typename L>
  void bodyHdrs(L &&l) const {
    l.template operator()<ContentLength>(
      [contentLength = this->contentLength](ZuSpan<uint8_t> span) {
	ZuStream s{span};
	s << ZuBoxed(contentLength).fmt<ZuFmt::Right<10>>();
      });
  }
};

struct StreamResponse :
  public ResponseBuilderBase<Zhttp::Body::Stream, RespHeaders> {
  template <typename Emit>
  void body(Emit &&emit) {
    emit([this](auto &body) -> bool {
      ZiFile file;
      if (file.dup(plan.fileHandle, ZiFile::GC) != Zi::OK) return false;
      auto buf = ZmScratch(char, FileChunk);
      buf.length(FileChunk);
      if (!buf) return false;
      uint64_t offset = plan.fileOffset;
      uint64_t left = plan.fileLength;
      while (left) {
	unsigned n = left > FileChunk ? FileChunk : unsigned(left);
	int r = file.pread(offset, buf.data(), n);
	if (r <= 0) return false;
	sendSpanChunks(body, buf.data(), unsigned(r));
	offset += r;
	left -= r;
      }
      return true;
    });
  }
};

struct EmptyResponse :
  public ResponseBuilderBase<Zhttp::Body::None, FixedRespHeaders> { };

struct Workload {
  struct RequestParser {
    using Headers = ReqHeaders;
    static constexpr uint64_t BodyMax = ReqBodyMax;

    ZhttpPut::String	bodyData;

    void operation(Zhttp::Method::T, const Zhttp::RequestTarget &) { }
    void version(ZuBSpan) { }
    void contentLength(uint64_t) { }
    void chunked() { }
    template <typename Key> void header(ZuBSpan) { }
    template <typename Rx>
    void bodyInput(Rx &rx) {
      Zhttp::bodyEach(rx,
	[this](ZuBSpan span) { bodyData << span; });
    }
    template <typename Rx> void body(Rx &rx) { bodyInput(rx); }
    void complete(bool) { }
  };

  RequestParser requestParser() { return {}; }

  template <typename Response, typename Emit>
  static void emit_(Response &&response, Emit &emit) {
    ZmRef<ResponseCompletion> completion = new ResponseCompletion;
    completion->status = response.plan.status;
    completion->contentLength = response.plan.contentLength;
    response.completion = completion;
    emit(ZuFwd<Response>(response), ZuMv(completion));
  }

  template <typename Emit>
  void response(
    const Zhttp::RequestInfo &info, RequestParser &request,
    Emit &&emit) {
    ++state->requests;
    if (info.method == Zhttp::Method::PUT) {
      ZhttpPut::Record record;
      ResponsePlan plan;
      if (info.bodyReceived != request.bodyData.length() ||
	  info.bodyConsumed != request.bodyData.length() ||
	  info.bodyReset || info.bodyDiscarded ||
	  !ZhttpPut::load(record, request.bodyData)) {
	plan.status = 400;
	plan.reason = "Bad Request";
	EmptyResponse response;
	response.plan = ZuMv(plan);
	emit_(ZuMv(response), emit);
	return;
      }
      plan.status = 200;
      plan.reason = "OK";
      plan.contentType = "application/json";
      plan.sendBody = true;
      plan.generated = true;
      FixedResponse response;
      response.plan = ZuMv(plan);
      response.record = ZuMv(record);
      response.json = true;
      emit_(ZuMv(response), emit);
      return;
    }
    StaticPlanner planner{state};
    ResponsePlan plan = planner.plan(info);
    if (!plan.sendBody) {
      EmptyResponse response;
      response.plan = ZuMv(plan);
      emit_(ZuMv(response), emit);
    } else if (plan.file) {
      StreamResponse response;
      response.plan = ZuMv(plan);
      emit_(ZuMv(response), emit);
    } else {
      FixedResponse response;
      response.plan = ZuMv(plan);
      emit_(ZuMv(response), emit);
    }
  }

  void complete(
    const Zhttp::RequestInfo &info, ZmRef<ResponseCompletion> &completion,
    bool) {
    ResponsePlan response;
    response.status = completion->status;
    response.contentLength = completion->contentLength;
    state->log.write(info, response, info.remote);
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
  }
  void listenFailed(int, bool) {
    state->errors = 1;
  }
  void connected(int) { }
  void disconnected(int) { }

  State *state = nullptr;
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

using Service = Zhttp::Service<Workload>;

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
  Workload workload{&state};
  Service service;
  service.txErrorFn(ZiTxErrorFn{[](bool transient, ZeException &e) {
    ZiLOG(Error, "zhttpd", ([transient, e](auto &s) {
      s << "transmit error" << (transient ? " (transient): " : ": ") << e;
    }));
    return false;
  }});
  auto serviceConfig = Zhttp::ServiceConfig()
    .localIP(ZiIP(state.options.addr))
    .port(state.options.port)
    .idleTimeout(state.options.timeout)
    .maxConnections(state.options.maxconn);
  if (state.options.http) {
    serviceConfig.tcp();
  }
  if (state.options.https) {
    int8_t policy;
    switch (state.options.http2) {
      case Http2Mode::force: policy = Zhttp::H2Policy::Force; break;
      case Http2Mode::disable: policy = Zhttp::H2Policy::Disable; break;
      default: policy = Zhttp::H2Policy::Prefer; break;
    }
    serviceConfig.tls(Zhttp::H2Config{}
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
    serviceConfig.quic(Zhttp::QUICConfig{}
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
  bool serviceInited = service.init(
    Zhttp::EngineConfig{&mx, "3", "4"},
    ZuMv(serviceConfig), &workload);
  if (!serviceInited) {
    ZiLOG(Error, "zhttpd", "HTTP service initialization failed");
    service.final();
    mx.stop();
    state.log.final();
    ZiLog::stop();
    return 1;
  }
  if (!service.start()) {
    ZiLOG(Error, "zhttpd", "HTTP engine start failed");
    (void)service.stop();
    service.final();
    mx.stop();
    state.log.final();
    ZiLog::stop();
    return 1;
  }
  service.diagnostic(state.options.memDiag,
    Zhttp::DiagnosticFn{[]() { printMemDiag(); }});
#ifdef Zquic_DEBUG
  if (h3Enabled)
    service.diagnostic(state.options.quicDiag,
      Zhttp::DiagnosticFn{[&service]() { service.printQUICDiag(); }});
#endif
  service.wait();
  if (!service.stop()) state.errors = 1;
  service.final();
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
