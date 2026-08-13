//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// REST/HTTP Ping client

#include <iostream>

#include <zlib/ZmHeap.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmTime.hh>
#include <zlib/ZmTrap.hh>

#include <zlib/ZtcHash.hh>
#include <zlib/ZtcHeap.hh>

#include <zlib/ZfCLI.hh>

#include <zlib/ZiHashCSV.hh>
#include <zlib/ZiHeapCSV.hh>
#include <zlib/ZiLog.hh>

#include <zlib/ZhttpClient.hh>
#include <zlib/ZrestClient.hh>

ZtEnumNS(, Http3Mode, int8_t, force, prefer, disable);
ZtEnumNS(, Http2Mode, int8_t, force, prefer, disable);

ZtEnumImplNS(Http3Mode);
ZtEnumImplNS(Http2Mode);

constexpr unsigned ClientTimeout = 15;
constexpr unsigned H3StallTimeout = 15;
constexpr unsigned H3QuietTimeout = 2;
constexpr unsigned MaxRedirects = 8;
constexpr uint64_t RespBodyMax = 1<<20;

struct Options {
  ZuCSpan	ca;
  uint32_t	requests = 1;
  uint32_t	concurrency = 1;
  uint32_t	links = 1;
  uint32_t	linkConcurrency = 1;
  uint32_t	retries = 0;
  uint32_t	timeout = ClientTimeout;
  uint32_t	stallTimeout = H3StallTimeout;
  uint32_t	quietTimeout = H3QuietTimeout;
  ZuCSpan	keyLog;
  ZuCSpan	url{"http://localhost:8080/"};
  Http3Mode::T	http3 = Http3Mode::prefer;
  Http2Mode::T	http2 = Http2Mode::prefer;
  uint32_t	quicHeartbeat = 0;
  bool		verbose = false;
#ifdef ZiMultiplex_DEBUG
  bool		debug = false;
  bool		frag = false;
  bool		yield = false;
#endif
#ifdef ZiMultiplex_FILTER
  ZuCSpan	quicRxDrop;
  ZuCSpan	quicTxDrop;
#endif
#ifdef Zquic_DEBUG
  uint32_t	quicDiag = 0;
#endif
  uint32_t	memDiag = 0;
  bool		help = false;
};

ZfStruct((Options, CLI),
  (((ca),        (CLI::Opt<'c'>,  CLI::Long<"ca">)),         (String)),
  (((requests),  (CLI::Opt<'n'>,  CLI::Long<"requests">)),   (UInt32, 1)),
  (((concurrency), (CLI::Opt<'j'>, CLI::Long<"jobs">)),       (UInt32, 1)),
  (((links),      (CLI::Long<"links">)),                     (UInt32, 1)),
  (((linkConcurrency), (CLI::Long<"link-concurrency">)),     (UInt32, 1)),
  (((retries),   (CLI::Long<"retries">)),                    (UInt32, 0)),
  (((timeout),   (CLI::Long<"timeout">)),                    (UInt32, ClientTimeout)),
  (((stallTimeout),
    (CLI::Long<"stall-timeout">)),                            (UInt32, H3StallTimeout)),
  (((quietTimeout),
    (CLI::Long<"quiet-timeout">)),                            (UInt32, H3QuietTimeout)),
  (((keyLog),    (CLI::Long<"key-log">)),                    (String)),
  (((http3),     (Enum<Http3Mode::Map>,
		  CLI::Opt<'3'>, CLI::Long<"http3">)),       (Int8,
								 Http3Mode::prefer)),
  (((http2),     (Enum<Http2Mode::Map>,
		  CLI::Opt<'2'>, CLI::Long<"http2">)),       (Int8,
								 Http2Mode::prefer)),
  (((quicHeartbeat),
    (CLI::Long<"quic-heartbeat">)),                           (UInt32, 0)),
  (((verbose),   (CLI::Flag<'v'>, CLI::Long<"verbose">)),    (Bool, false)),
#ifdef ZiMultiplex_DEBUG
  (((debug),     (CLI::Long<"debug">)),                      (Bool, false)),
  (((frag),      (CLI::Long<"frag">)),                       (Bool, false)),
  (((yield),     (CLI::Long<"yield">)),                      (Bool, false)),
#endif
#ifdef ZiMultiplex_FILTER
  (((quicRxDrop), (CLI::Long<"quic-rx-drop">)),              (String)),
  (((quicTxDrop), (CLI::Long<"quic-tx-drop">)),              (String)),
#endif
#ifdef Zquic_DEBUG
  (((quicDiag),   (CLI::Long<"quic-diag">)),                 (UInt32, 0)),
#endif
  (((memDiag),    (CLI::Long<"mem-diag">)),                  (UInt32, 0)),
  (((url),        (CLI::Arg<1>)),                             (String,
							 "http://localhost:8080/")),
  (((help),       (CLI::Flag<'h'>, CLI::Long<"help">)),       (Bool, false)));

static ZmSemaphore done;

static void usage(int code = 1)
{
  std::cerr <<
    "Usage: zrest [OPTION]... [URL]\n\n"
    "Options:\n"
    "  -c, --ca=PATH       CA path for https:\n"
    "  -n, --requests=N    submit N Ping requests, default 1\n"
    "  -j, --jobs=M        run up to M requests concurrently, default 1\n"
    "  --links=N           persistent links in pool 0, default 1\n"
    "  --link-concurrency=N\n"
    "                      maximum operations per link, default 1\n"
    "  --retries=N         retry transient connection failures N times\n"
    "  --timeout=N         completion timeout in seconds, default 15, 0 disables\n"
    "  --stall-timeout=N   no-progress stall timeout in seconds, default 15\n"
    "  --quiet-timeout=N   quiet transport timeout in seconds, default 2\n"
    "  --key-log=PATH      append HTTP/3 TLS secrets\n"
    "  -3, --http3=MODE    HTTP/3 mode: force, prefer, disable\n"
    "  -2, --http2=MODE    HTTP/2 mode: force, prefer, disable\n"
    "  --quic-heartbeat=N  send QUIC PING after N idle seconds, 0 disables\n"
    "  -v, --verbose       show connection information\n"
#ifdef ZiMultiplex_DEBUG
    "  --debug             enable ZiMultiplex and HTTP/3 debug logging\n"
    "  --frag              fragment ZiMultiplex I/O in debug builds\n"
    "  --yield             yield in ZiMultiplex in debug builds\n"
#endif
#ifdef ZiMultiplex_FILTER
    "  --quic-rx-drop=N%   randomly drop N% of received QUIC UDP packets\n"
    "  --quic-tx-drop=N%   randomly drop N% of transmitted QUIC UDP packets\n"
#endif
#ifdef Zquic_DEBUG
    "  --quic-diag=N       print HTTP/3 QUIC counters every N seconds\n"
#endif
    "  --mem-diag=N        print memory counters every N seconds\n"
    "  -h, --help          show help\n\n"
    "URL defaults to http://localhost:8080/.\n" << std::flush;
  ::exit(code);
}

#ifdef ZiMultiplex_FILTER
static bool parseDrop(ZuCSpan s, double &drop)
{
  drop = 0.0;
  if (!s) return true;
  if (s.length() < 2 || s[s.length() - 1] != '%') return false;
  ZuBox<double> pct;
  if (pct.scan(s.data(), s.length() - 1) != s.length() - 1) return false;
  drop = pct;
  if (drop < 0.0 || drop > 100.0) return false;
  drop *= .01;
  return true;
}
#endif

static bool validateOptions(const Options &options, int argc)
{
  if (argc < 1 || argc > 2 || !options.requests || !options.concurrency ||
      !options.links || !options.linkConcurrency)
    return false;
  if (options.http3 < 0 || options.http3 >= Http3Mode::N ||
      options.http2 < 0 || options.http2 >= Http2Mode::N)
    return false;
#ifdef ZiMultiplex_FILTER
  double drop;
  if (!parseDrop(options.quicRxDrop, drop) ||
      !parseDrop(options.quicTxDrop, drop)) return false;
#endif
  return true;
}

static void printMemDiag()
{
  ZiLOG(Info, "zrest", ([](auto &s) {
    s << "Hash Tables:\n" << Ztc::hashCSV();
    s << "Heaps:\n" << Ztc::heapCSV();
  }));
}

struct Pong;
struct Ping : public ZmObject {
  bool ping = true;

  template <typename Link>
  void process(Link *, const Pong *) const { }

  template <typename Link>
  void failed(Link *) const { }
};
struct Pong : public ZmObject {
  bool pong = true;
};

ZfStruct((Ping, URI),
  (((ping)),	(Bool)));
ZfStruct((Pong, JSON),
  (((pong)),	(Bool)));

struct PongParser;

struct PingBuilder : public Zrest::ReqBuilder<PingBuilder, Ping> {
  enum { Query = Zrest::QueryPolicy::URI };
  using Headers = ZuTypeList<>;
  using Responses = ZuTypeList<PongParser>;
};

struct PongParser : public Zrest::ResParser<PongParser, Pong> {
  enum { Body = Zrest::BodyPolicy::JSON };
  using Headers = ZhttpHeaders(
    ("content-type", "application/json"), "content-length");
};

using Requests = ZuTypeList<PingBuilder>;

struct ReqBuilder_ : public ZmObject, public Zrest::MReqBuilder<Requests> {
  using Base = Zrest::MReqBuilder<Requests>;
  using Reqs = Requests;
  using Base::init;

  uint64_t key() const { return id; }
  uint64_t length() const { return 1; }

  unsigned id = 0;
};

struct ResParser : public Zrest::MResParser<ReqBuilder_> { };

struct Client;
struct Pool;
ZuDerive(ReqBuilderQ, (ZmPQueue<ReqBuilder_,
  ZmPQueueOverlap<false,
    ZmPQueueNode<ReqBuilder_,
      ZmPQueueHeapID<"zrest.ReqBuilder">>>>));
using ReqBuilder = ReqBuilderQ::Node;
using TxQ = ZmPQTx<Pool, ReqBuilderQ, ZmPQTxOrdered<false>>;

struct Pool : public Zhttp::Pool<Client, TxQ, ResParser> {
  using Base = Zhttp::Pool<Client, TxQ, ResParser>;

  Pool(Client *client) : Base{client} { }

  ReqBuilderQ *txQueue() { return &m_requests; }
  void archive_(ReqBuilder *);
  ZmRef<ReqBuilder> retrieve_(ReqBuilderQ::Key, ReqBuilderQ::Key) { return {}; }

private:
  ReqBuilderQ m_requests;
};

struct Client : public Zhttp::Client<Client, Pool> {
  void idle() { done.post(); }

  void archive(ReqBuilder *) {
    produce_();
    if (m_generated == m_options->requests) seal(0);
  }

  void workload(const Options &options) {
    txRun(0, [this, options = &options]() {
      m_options = options;
      unsigned n = options->requests;
      if (n > options->concurrency) n = options->concurrency;
      while (n--) produce_();
      if (m_generated == options->requests) seal(0);
    });
  }

private:
  void produce_() {
    if (!m_options || m_generated >= m_options->requests) return;
    ZmRef<ReqBuilder> request = new ReqBuilder{};
    request->id = m_generated++;
    request->template init<PingBuilder>(new Ping{});
    send(0, ZuMv(request));
  }

  const Options *m_options = nullptr;
  unsigned m_generated = 0;
};

void Pool::archive_(ReqBuilder *request)
{
  client()->archive(request);
}

static ZiMxParams mxParams(const Options &options)
{
  auto params = ZiMxParams()
    .scheduler([](auto &s) {
      s.nThreads(4)
	.thread(1, [](auto &t) { t.isolated(1); })
	.thread(2, [](auto &t) { t.isolated(1); })
	.thread(3, [](auto &t) { t.isolated(1); })
	.thread(4, [](auto &t) { t.isolated(1); }); })
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

static void interrupted() { done.post(); }

int main(int argc, char **argv)
{
  ZiHeapCSV::init(::getenv("Z_HEAPTUNE"));
  ZiHashCSV::init(::getenv("Z_HASHTUNE"));

  Options options;
  try {
    argc = ZfCLI::load(options, argc, argv);
  } catch (const ZeException &e) {
    std::cerr << e << '\n';
    usage();
  }
  if (options.help) usage(0);
  if (!validateOptions(options, argc)) usage();

  Zhttp::URL urlStorage;
  auto urlError = urlStorage.assign(options.url);
  if (!urlError.ok()) {
    std::cerr << "zrest: invalid URL (code=" << int(urlError.code) <<
      ", offset=" << urlError.offset << ")\n";
    return 1;
  }
  Zhttp::URLView url = urlStorage.url();

  ZiLog::init("zrest");
  ZiLog::level(
#ifdef ZiMultiplex_DEBUG
    options.debug ? Ze::Debug :
#endif
#ifdef Zquic_DEBUG
    options.quicDiag ? Ze::Info :
#endif
    options.memDiag ? Ze::Debug :
    options.verbose ? Ze::Info : Ze::Warning);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();

  ZmTrap::sigintFn(interrupted);
  ZmTrap::trap();

  ZiMultiplex mx(mxParams(options));
  if (!mx.start()) {
    ZiLOG(Error, "zrest", "ZiMultiplex start failed");
    ZiLog::stop();
    return 1;
  }

  Zhttp::ProtocolPolicy::T policy;
  switch (options.http3) {
    case Http3Mode::force: policy = Zhttp::ProtocolPolicy::ForceH3; break;
    case Http3Mode::disable: policy = Zhttp::ProtocolPolicy::DisableH3; break;
    default: policy = Zhttp::ProtocolPolicy::PreferH3; break;
  }
  Zhttp::H2Policy::T h2Policy;
  switch (options.http2) {
    case Http2Mode::force: h2Policy = Zhttp::H2Policy::Force; break;
    case Http2Mode::disable: h2Policy = Zhttp::H2Policy::Disable; break;
    default: h2Policy = Zhttp::H2Policy::Prefer; break;
  }
  double rxDrop = 0, txDrop = 0;
#ifdef ZiMultiplex_FILTER
  (void)parseDrop(options.quicRxDrop, rxDrop);
  (void)parseDrop(options.quicTxDrop, txDrop);
#endif
  bool secure = url.scheme == Zhttp::Scheme::https;
  auto config = Zhttp::Config()
    .links(options.links)
    .concurrency(options.concurrency)
    .linkConcurrency(options.linkConcurrency)
    .requestTimeout(options.timeout)
    .maxRedirects(MaxRedirects)
    .maxRetries(options.retries)
    .retainedBodyMax(RespBodyMax)
    .protocol(policy)
    .h2Policy(h2Policy)
    .secure(secure)
    .tcp(true)
    .tls(secure && policy != Zhttp::ProtocolPolicy::ForceH3)
    .quic(secure && policy != Zhttp::ProtocolPolicy::DisableH3);
  auto quic = Zhttp::QUICConfig()
    .caPath(options.ca).keyLogPath(options.keyLog)
    .maxStreamsDuplex(options.linkConcurrency)
    .heartbeat(options.quicHeartbeat ?
      ZuTime{options.quicHeartbeat} : ZuTime{})
    .rxDrop(rxDrop).txDrop(txDrop);

  Client app;
  bool appInited = app.init(
    Zhttp::HubConfig{&mx, "3", "4"}, 1, config, Zhttp::TCPConfig{},
    Zhttp::H2Config{}.caPath(options.ca).policy(h2Policy), quic);
  bool poolInited = appInited && app.pool(
    0, Zhttp::Destination{url.host, url.port, url.ipv6Literal});
  app.txErrorFn(ZiTxErrorFn{[](ZeException &e) {
    ZiLOG(Error, "zrest", ([e](auto &s) { s << "transmit error: " << e; }));
    return false;
  }});
  bool appUp = poolInited && app.start();
  if (!appUp) {
    ZiLOG(Error, "zrest", "client initialization/start failed");
    if (appInited) app.stop();
    app.final();
    mx.stop();
    ZiLog::stop();
    return 1;
  }
  app.workload(options);

  unsigned elapsed = 0;
  unsigned memElapsed = 0;
#ifdef Zquic_DEBUG
  unsigned quicElapsed = 0;
#endif
  bool timedOut = false;
  for (;;) {
    unsigned step = options.timeout ? options.timeout - elapsed : 0;
    if (options.memDiag) {
      unsigned left = options.memDiag - memElapsed;
      if (!step || left < step) step = left;
    }
#ifdef Zquic_DEBUG
    if (options.quicDiag) {
      unsigned left = options.quicDiag - quicElapsed;
      if (!step || left < step) step = left;
    }
#endif
    if (!step) { done.wait(); break; }
    if (!done.timedwait(Zm::now(step))) break;
    elapsed += step;
    if (options.memDiag && (memElapsed += step) >= options.memDiag) {
      memElapsed = 0;
      printMemDiag();
    }
#ifdef Zquic_DEBUG
    if (options.quicDiag && (quicElapsed += step) >= options.quicDiag) {
      quicElapsed = 0;
      app.printQUICDiag();
    }
#endif
    if (options.timeout && elapsed >= options.timeout) {
      timedOut = done.trywait() != 0;
      break;
    }
  }
  if (timedOut) ZiLOG(Error, "zrest", "timed out");
  app.stop();
  bool incomplete = app.completed() != options.requests;
  if (incomplete)
    ZiLOG(Error, "zrest", ([
      completed = app.completed(), expected = options.requests,
      active = app.active()
    ](auto &s) {
      s << "incomplete run (completed=" << completed <<
	", expected=" << expected << ", active=" << active << ')';
    }));
  int rc = timedOut || incomplete || app.failed() ? 1 : 0;
  app.final();
  mx.stop();
  ZiLog::stop();
  return rc;
}
