//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// test HTTP client

#include <iostream>
#include <string.h>

#include <zlib/ZuLib.hh>
#include <zlib/ZmBitmap.hh>
#include <zlib/ZmTime.hh>
#include <zlib/ZmTrap.hh>

#include <zlib/ZtcHash.hh>
#include <zlib/ZtcHeap.hh>

#include <zlib/ZfCLI.hh>

#include <zlib/ZiFile.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiHashCSV.hh>
#include <zlib/ZiHeapCSV.hh>

#include <zlib/ZhttpClient.hh>

#include "runtime.hh"
#include "zhttpput.hh"

ZtEnumNS(, Http3Mode, int8_t, force, prefer, disable);
ZtEnumNS(, Http2Mode, int8_t, force, prefer, disable);

ZtEnumImplNS(Http3Mode);
ZtEnumImplNS(Http2Mode);

constexpr unsigned ClientTimeout = 15;
constexpr unsigned H3StallTimeout = 15;
constexpr unsigned H3QuietTimeout = 2;

struct Options {
  ZuCSpan	ca;
  ZuCSpan	output{"index.html"};
  bool		discardResponse = false;
  bool		put = false;
  uint32_t	requests = 1;
  uint32_t	concurrency = 1;
  uint32_t	links = 1;
  uint32_t	linkConcurrency = 1;
  uint32_t	retries = 0;
  uint32_t	timeout = ClientTimeout;
  uint32_t	stallTimeout = H3StallTimeout;
  uint32_t	quietTimeout = H3QuietTimeout;
  ZuCSpan	keyLog;
  ZuCSpan	url;
  Http3Mode::T	http3 = Http3Mode::prefer;
  Http2Mode::T	http2 = Http2Mode::prefer;
  ZuCSpan	quicMigration{"passive"};
  uint32_t	quicHeartbeat = 0;
  uint32_t	quicMigrationCIDReserve = 1;
  bool		quicMigrationCloseOnFailure = false;
  ZuCSpan	quicMigrationLocal;
  bool		quicMigrateLocal = false;
  bool		quicMigrateAfterHeaders = false;
  uint64_t	quicMigrateAfterBytes = 0;
  ZiSockAddr	quicMigrationLocalAddr;
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
  (((output),    (CLI::Opt<'o'>,  CLI::Long<"output">)),     (String, "index.html")),
  (((discardResponse),
    (CLI::Long<"discard-response">)),                         (Bool, false)),
  (((put),       (CLI::Long<"put">)),                         (Bool, false)),
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
  (((quicMigration),
    (CLI::Long<"quic-migration">)),                         (String, "passive")),
  (((quicHeartbeat),
    (CLI::Long<"quic-heartbeat">)),                         (UInt32, 0)),
  (((quicMigrationCIDReserve),
    (CLI::Long<"quic-migration-cid-reserve">)),             (UInt32, 1)),
  (((quicMigrationCloseOnFailure),
    (CLI::Long<"quic-migration-close-on-failure">)),        (Bool, false)),
  (((quicMigrationLocal),
    (CLI::Long<"quic-migration-local">)),                   (String)),
  (((quicMigrateLocal),
    (CLI::Long<"quic-migrate-local">)),                     (Bool, false)),
  (((quicMigrateAfterHeaders),
    (CLI::Long<"quic-migrate-after-headers">)),             (Bool, false)),
  (((quicMigrateAfterBytes),
    (CLI::Long<"quic-migrate-after-bytes">)),               (UInt64, 0)),
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
  (((url),       (CLI::Arg<1>)),                             (String)),
  (((help),      (CLI::Flag<'h'>, CLI::Long<"help">)),       (Bool, false)));

void usage(int code = 1)
{
  std::cerr <<
    "Usage: zhttp [OPTION]... URL\n\n"
    "Options:\n"
    "  -c, --ca=PATH       CA path for https:\n"
    "  -o, --output=PATH   response body output path\n"
    "  --discard-response  discard response bodies instead of writing files\n"
    "  --put               PUT and validate a typed JSON record\n"
    "  -n, --requests=N    submit N GET requests, default 1\n"
    "  -j, --jobs=M        run up to M requests concurrently, default 1\n"
    "  --links=N           persistent links in pool 0, default 1\n"
    "  --link-concurrency=N\n"
    "                      maximum operations per link, default 1\n"
    "  --retries=N         retry transient connection failures N times\n"
    "  --timeout=N         completion timeout in seconds, default 15, 0 disables\n"
    "  --stall-timeout=N   no-progress stall timeout in seconds, default 15,\n"
    "                      0 disables; currently applies to HTTP/3\n"
    "  --quiet-timeout=N   quiet transport timeout in seconds, default 2,\n"
    "                      0 disables; currently applies to HTTP/3\n"
    "  --key-log=PATH      append HTTP/3 TLS secrets for tshark/Wireshark;\n"
    "                      defaults to SSLKEYLOGFILE when set\n"
    "  -3, --http3=MODE   HTTP/3 mode for https: force, prefer, disable;\n"
    "                      default prefer\n"
    "  -2, --http2=MODE   HTTP/2 mode within TLS: force, prefer, disable;\n"
    "                      default prefer\n"
    "  --quic-migration=MODE\n"
    "                      QUIC migration policy: disabled, passive, active;\n"
    "                      default passive\n"
    "  --quic-heartbeat=N  send QUIC PING after N idle seconds, 0 disables\n"
    "  --quic-migration-cid-reserve=N\n"
    "                      peer CID reserve for QUIC migration, default 1\n"
    "  --quic-migration-close-on-failure\n"
    "                      close active migration attempts on failure\n"
    "  --quic-migration-local=ADDR[:PORT]\n"
    "                      local address for HTTP/3 client migration\n"
    "  --quic-migrate-local\n"
    "                      request one HTTP/3 client local UDP port migration\n"
    "                      after H3 control streams open\n"
    "  --quic-migrate-after-headers\n"
    "                      request HTTP/3 client migration after response headers\n"
    "  --quic-migrate-after-bytes=N\n"
    "                      request HTTP/3 client migration after N response bytes\n"
    "  -v, --verbose       show DNS and Alt-Svc probing\n"
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
    "For N > 1, response bodies are written to PATH.0, PATH.1, ...\n" <<
    std::flush;
  ::exit(code);
}

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

Zhttp::Migration::T migrationMode(const Options &options)
{
  return Zhttp::migrationMode(options.quicMigration);
}

ZuTime quicHeartbeat(const Options &options)
{
  return options.quicHeartbeat ? ZuTime{options.quicHeartbeat} : ZuTime{};
}

bool parseMigrationLocal(ZuCSpan s, ZiSockAddr &addr);

bool migrationConfigured(const Options &options)
{
  return options.quicMigrateLocal || options.quicMigrationLocal ||
    options.quicMigrateAfterHeaders || options.quicMigrateAfterBytes;
}

bool migrationOnOpen(const Options &options)
{
  return options.quicMigrateLocal ||
    (options.quicMigrationLocal && !options.quicMigrateAfterHeaders &&
      !options.quicMigrateAfterBytes);
}

bool validateOptions(Options &options, int argc)
{
  if (argc < 0 || argc != 2) return false;
  if (!options.requests || !options.concurrency || !options.links ||
      !options.linkConcurrency)
    return false;
  if (options.http3 < 0 || options.http3 >= Http3Mode::N) return false;
  if (options.http2 < 0 || options.http2 >= Http2Mode::N) return false;
  int8_t mode = Zhttp::migrationMode(options.quicMigration, -1);
  if (mode < 0) return false;
  if (migrationConfigured(options) && mode != Zhttp::Migration::Active)
    return false;
  if (options.quicMigrationLocal &&
      !parseMigrationLocal(options.quicMigrationLocal,
	options.quicMigrationLocalAddr))
    return false;
#ifdef ZiMultiplex_FILTER
  double drop;
  if (!parseDrop(options.quicRxDrop, drop)) return false;
  if (!parseDrop(options.quicTxDrop, drop)) return false;
#endif
  return true;
}

void printMemDiag()
{
  ZiLOG(Info, "zhttp", ([](auto &s) {
    s << "Hash Tables:\n" << Ztc::hashCSV();
    s << "Heaps:\n" << Ztc::heapCSV();
  }));
}

ZuDerive(HdrString, ZtString<ZtStringHeapID<"zhttp.HdrString">>);

HdrString outputPath(ZuCSpan base, unsigned reqID, unsigned requests)
{
  HdrString path;
  path << base;
  if (requests > 1) path << '.' << ZuBoxed(reqID);
  return path;
}

using RequestHeaders = ZhttpHeaders(
  "user-agent",
  "accept",
  "content-length");
using ResponseHeaders = ZhttpHeaders(
  "alt-svc",
  "connection",
  "location");

using URL = Zhttp::URL;

constexpr unsigned MaxRedirects = 8;
constexpr uint64_t RespBodyMax = 100<<20;

struct ResParser;

struct Request_ : public ZmObject {
  using Headers = RequestHeaders;
  using ContentLength = ZuStringT<"content-length">;

  Zhttp::BodyPolicy::T bodyPolicy() const {
    return put ? Zhttp::BodyPolicy::OptionalFixed : Zhttp::BodyPolicy::None;
  }

  void reset() { requestContentLength = 0; }
  template <typename L>
  void operation(L &&l) const {
    auto pathQuery = Zhttp::splitPathQuery(target);
    l(put ? Zhttp::Method::PUT : Zhttp::Method::GET,
      pathQuery.path, pathQuery.hasQuery,
      [query = pathQuery.query](auto &stream) { stream << query; });
  }
  template <typename L> void protocol(L &&) const { }
  template <typename Key, typename L>
  void header(L &&l) const {
    if constexpr (Key{}() == "user-agent")
      l("zhttp/1.0");
    else if constexpr (Key{}() == "accept")
      l("*/*");
    else if constexpr (ZuIsSame<Key, ContentLength>{}) {
      if (put) l(Zhttp::Placeholder{10, '0'});
    }
  }
  template <typename L> void header(L &&) const { }
  template <typename Emit>
  void body(Emit &&emit) {
    if (!put) return;
    emit([this](auto &body) {
      ZfJSON::save(body, putRecord);
      body.flush();
      requestContentLength = body.produced();
      return true;
    });
  }
  template <typename L>
  void bodyHdrs(L &&l) const {
    l.template operator()<ContentLength>(
      [contentLength = requestContentLength](ZuSpan<uint8_t> span) {
	ZuStream s{span};
	s << ZuBoxed(contentLength).fmt<ZuFmt::Right<10>>();
      });
  }

  bool replayable() const { return true; }
  bool reproducible() const { return true; }
  void connected(const Zhttp::ConnectedInfo &);
  void disconnected(bool) { }
  void connectFailed(bool) { }
  void selected(const Zhttp::Endpoint &) { }
  void redirected(const URL &);
  void observed(const Zhttp::ClientEvent &) { }
  void completed(const Zhttp::Result &);

  uint64_t key() const { return id; }
  uint64_t length() const { return 1; }

  unsigned	id = 0;
  unsigned	requests = 1;
  unsigned	requestContentLength = 0;
  HdrString	target;
  HdrString	output;
  bool		discardResponse = false;
  bool		put = false;
  ZhttpPut::Record putRecord;
  ZhttpPut::String responseJSON;
  bool		logResponse = false;
  unsigned	status = 0;
  ZiFile	bodyFile;
  int64_t	contentLength = -1;
  uint64_t	bodyBytes = 0;
  unsigned	bodyChunks = 0;
  bool		bodyFileOpen = false;
  bool		chunked = false;
  bool		redirecting = false;
  bool		framingLogged = false;
  bool		responseHeadersDone = false;
  bool		truncateOutput = false;
  bool		done = false;
  bool		failed = false;
};

bool hotLog(const Options &options)
{
  return options.verbose
#ifdef ZiMultiplex_DEBUG
    || options.debug
#endif
    ;
}

struct ReqLogCtx {
  unsigned	id = 0;
  unsigned	requests = 1;
};

inline ReqLogCtx reqLogCtx(const Request_ &req)
{
  return {req.id, req.requests};
}

bool parseMigrationLocal(ZuCSpan s, ZiSockAddr &addr)
{
  Zhttp::AuthorityView authority;
  if (!Zhttp::parseAuthority(
      authority, ZuBSpan{s}, 0, 0, false, true).ok()) return false;
  try {
    ZiIP ip;
    if (!ZiIP::parse(ip, authority.host)) return false;
    if (!ip) return false;
    addr.init(ip, authority.port);
    return true;
  } catch (...) {
    return false;
  }
}

void closeBody(Request_ &req)
{
  if (req.bodyFileOpen) {
    req.bodyFile.close();
    req.bodyFileOpen = false;
  }
}

void resetResponse(Request_ &req, bool truncateOutput)
{
  closeBody(req);
  req.status = 0;
  req.contentLength = -1;
  req.bodyBytes = 0;
  req.bodyChunks = 0;
  req.responseJSON.length(0);
  req.chunked = false;
  req.redirecting = false;
  req.framingLogged = false;
  req.responseHeadersDone = false;
  req.truncateOutput = truncateOutput;
  req.done = false;
  req.failed = false;
}

void initReq(
  Request_ &req, const Options &options, const URL &url, unsigned id)
{
  req.id = id;
  req.requests = options.requests;
  req.target = url.pathQuery();
  if (!options.discardResponse)
    req.output = outputPath(options.output, id, options.requests);
  req.discardResponse = options.discardResponse;
  req.put = options.put;
  if (req.put) {
    req.putRecord.id = id;
    req.putRecord.text << "zhttp-put-" << ZuBoxed(id);
  }
  req.logResponse = hotLog(options);
}

template <typename S>
void reqLogPrefix(const Request_ &req, S &s)
{
  if (req.requests > 1) s << "req=" << req.id << ' ';
}
template <typename S>
void reqLogPrefix(const ReqLogCtx &ctx, S &s)
{
  if (ctx.requests > 1) s << "req=" << ctx.id << ' ';
}

bool truncateOutputPath(Request_ &req)
{
  if (req.discardResponse) return true;
  if (!req.truncateOutput) return true;
  ZiFile f{req.output, ZiFile::Write | ZiFile::GC};
  if (!f) {
    auto ctx = reqLogCtx(req);
    ZiLOG(Error, "zhttp", ([ctx, output = ZeString(req.output)](auto &s) {
      reqLogPrefix(ctx, s);
      s << "failed to open " << output;
    }));
    req.failed = true;
    req.done = true;
    return false;
  }
  f.close();
  req.truncateOutput = false;
  return true;
}

bool redirectStatus(unsigned status)
{
  return status == 301 || status == 302 || status == 303 ||
    status == 307 || status == 308;
}

void logFraming(Request_ &req)
{
  if (req.framingLogged) return;
  req.framingLogged = true;
  if (!req.logResponse) return;
  auto ctx = reqLogCtx(req);
  auto chunked = req.chunked;
  auto contentLength = req.contentLength;
  ZiLOG(Info, "zhttp.response", ([ctx, chunked, contentLength](auto &s) {
    reqLogPrefix(ctx, s);
    s << "framing: ";
    if (chunked)
      s << "chunked";
    else if (contentLength >= 0)
      s << "content-length=" << contentLength;
    else
      s << "no content-length";
  }));
}

void logConnected(const Request_ &, const Zhttp::ConnectedInfo &);

struct ResParser {
  using Headers = ResponseHeaders;

  bool enable1xx() const { return false; }

  Request_ *req = nullptr;

  void init(const Request_ &req_) {
    req = const_cast<Request_ *>(&req_);
    reset();
  }
  void reset() { resetResponse(*req, true); }

  void headersDone() {
    if (req->responseHeadersDone) return;
    req->responseHeadersDone = true;
  }

  void status(unsigned value) {
    req->status = value;
    req->redirecting = redirectStatus(value);
    if (!req->redirecting && !req->discardResponse &&
	!truncateOutputPath(*req)) return;
    if (!req->logResponse) return;
    auto ctx = reqLogCtx(*req);
    ZiLOG(Info, "zhttp.response", ([ctx, value](auto &s) {
      reqLogPrefix(ctx, s);
      s << "status: " << value;
    }));
  }
  void bodyInfo(Zhttp::BodyType::T type, uint64_t length) {
    req->chunked = type == Zhttp::BodyType::Streamed;
    req->contentLength = type == Zhttp::BodyType::Fixed ? int64_t(length) : -1;
  }
  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuBSpan value) {
    if (!req->logResponse) return;
    auto ctx = reqLogCtx(*req);
    ZeString value_;
    value_ << ZuCSpan(value);
    ZiLOG(Info, "zhttp.response", ([ctx, value = ZuMv(value_)](auto &s) {
      reqLogPrefix(ctx, s);
      s << "header " << Key{}() << ": " << value;
    }));
  }

  template <typename Rx>
  bool body(Rx &rx) {
    logFraming(*req);
    headersDone();
    return Zhttp::bodyEach(rx, [req = this->req](ZuBSpan span) {
      req->bodyBytes += span.length();
      ++req->bodyChunks;
      if (req->put) req->responseJSON << span;
      if (req->redirecting || req->discardResponse) return;
      if (!truncateOutputPath(*req)) return;
      if (!req->bodyFileOpen) {
	req->bodyFile = ZiFile(req->output, ZiFile::Write | ZiFile::GC);
	if (!req->bodyFile) {
	  auto ctx = reqLogCtx(*req);
	  ZiLOG(Error, "zhttp", ([
	    ctx, output = ZeString(req->output)](auto &s) {
	    reqLogPrefix(ctx, s);
	    s << "failed to open " << output;
	  }));
	  req->failed = true;
	  req->done = true;
	  return;
	}
	req->bodyFileOpen = true;
      }
      if (req->bodyFile.write(span.data(), span.length()) == Zi::OK)
	return;
      auto ctx = reqLogCtx(*req);
      ZiLOG(Error, "zhttp", ([ctx](auto &s) {
	reqLogPrefix(ctx, s);
	s << "failed to write body chunk";
      }));
      req->failed = true;
      req->done = true;
    });
  }

  void complete(bool ok) {
    auto ctx = reqLogCtx(*req);
    if (ok && req->put && !req->redirecting) {
      ZhttpPut::Record record;
      if (!ZhttpPut::load(record, req->responseJSON) ||
	  !ZhttpPut::equals(record, req->putRecord)) {
	ok = false;
	req->failed = true;
      }
    }
    if (ok) {
      if (!req->redirecting) headersDone();
      if (req->logResponse) {
	auto bodyBytes = req->bodyBytes;
	auto bodyChunks = req->bodyChunks;
	ZiLOG(Info, "zhttp.response", ([ctx, bodyBytes, bodyChunks](auto &s) {
	  reqLogPrefix(ctx, s);
	  s << "body complete: " << bodyBytes << " bytes in " << bodyChunks <<
	    " chunks";
	}));
      }
    } else
      ZiLOG(Error, "zhttp.response", ([ctx](auto &s) {
	reqLogPrefix(ctx, s);
	s << "response failed";
      }));
    closeBody(*req);
  }
};

void Request_::redirected(const URL &url_)
{
  target = url_.pathQuery();
}

void Request_::completed(const Zhttp::Result &result)
{
  closeBody(*this);
  done = true;
  failed = !result.ok() ||
      (put &&
	(!result.requestBodyProduced ||
	 result.requestBodyProduced != result.requestBodyCommitted ||
	 result.requestBodyReset || result.requestBodyDiscarded ||
	 result.responseBodyReceived != bodyBytes ||
	 result.responseBodyConsumed != bodyBytes ||
	 result.responseBodyReset || result.responseBodyDiscarded));
}

void Request_::connected(const Zhttp::ConnectedInfo &info)
{
  logConnected(*this, info);
}

struct Client;
struct Pool;
ZuDerive(RequestQ, (ZmPQueue<Request_,
  ZmPQueueOverlap<false,
    ZmPQueueNode<Request_,
      ZmPQueueHeapID<"zhttp.Request">>>>));
using Request = RequestQ::Node;
using TxQ = ZmPQTx<Pool, RequestQ, ZmPQTxOrdered<false>>;

struct Pool : public Zhttp::Pool<Client, TxQ, ResParser> {
  using Base = Zhttp::Pool<Client, TxQ, ResParser>;

  Pool(Client *client) : Base{client} { }

  RequestQ *txQueue() { return &m_requests; }

  void archive_(Request *request);
  ZmRef<Request> retrieve_(RequestQ::Key, RequestQ::Key) { return {}; }

private:
  RequestQ	m_requests;
};

struct Client : public Zhttp::Client<Client, Pool> {
  using Base = Zhttp::Client<Client, Pool>;

  void idle() { ZhttpUtil::Runtime::post(); }

  void archive(Request *) {
    produce_();
    if (m_generated == m_options->requests) seal(0);
  }

  void workload(const Options &options, const URL &url) {
    txRun(0, [this, options = &options, url = &url]() {
      m_options = options;
      m_url = url;
      unsigned n = options->requests;
      if (n > options->concurrency) n = options->concurrency;
      while (n--) produce_();
      if (m_generated == options->requests) seal(0);
    });
  }

private:
  void produce_() {
    if (!m_options || m_generated >= m_options->requests) return;
    ZmRef<Request> request = new Request;
    initReq(*request, *m_options, *m_url, m_generated++);
    send(0, ZuMv(request));
  }

  const Options	*m_options = nullptr;
  const URL	*m_url = nullptr;
  unsigned	m_generated = 0;
};

void Pool::archive_(Request *request)
{
  client()->archive(request);
}

void logConnected_(
  const Request_ &req, ZuCSpan transport, ZuCSpan httpVersion,
  uint32_t version, ZuCSpan alpn)
{
  ZiLOG(Info, "zhttp", ([
    transport,
    httpVersion,
    version,
    alpn = ZeString(alpn)
  ](auto &s) {
    s << transport << " connected";
    if (httpVersion) s << " HTTP: " << httpVersion;
    if (version) s << " transport version: " << version;
    if (alpn) s << " ALPN: " << alpn;
  }));
}

void logConnected(const Request_ &req, const Zhttp::ConnectedInfo &info)
{
  logConnected_(req,
    Zhttp::Transport{}.name(info.transport),
    Zhttp::Version{}.name(info.httpVersion), info.version, info.alpn);
}

ZiMxParams mxParams(const Options &options)
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

  Zhttp::URLStorage urlStorage;
  auto urlError = urlStorage.assign(ZuBSpan{options.url});
  if (!urlError.ok()) {
    auto code = urlError.code;
    auto offset = urlError.offset;
    ZiLOG(Error, "zhttp", ([code, offset](auto &s) {
      s << "invalid URL (code=" << int(code) << ", offset=" << offset << ')';
    }));
    usage();
  }
  URL url = urlStorage.url();

  ZiLog::init("zhttp");
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

  ZmTrap::sigintFn(ZhttpUtil::Runtime::post);
  ZmTrap::trap();

  ZiMultiplex mx(mxParams(options));
  if (!mx.start()) {
    ZiLOG(Error, "zhttp", "ZiMultiplex start failed");
    ZiLog::stop();
    return 1;
  }

  Zhttp::ProtocolPolicy::T policy;
  switch (options.http3) {
    case Http3Mode::force:
      policy = Zhttp::ProtocolPolicy::ForceH3;
      break;
    case Http3Mode::disable:
      policy = Zhttp::ProtocolPolicy::DisableH3;
      break;
    default:
      policy = Zhttp::ProtocolPolicy::PreferH3;
      break;
  }

  double rxDrop = 0, txDrop = 0;
#ifdef ZiMultiplex_FILTER
  (void)parseDrop(options.quicRxDrop, rxDrop);
  (void)parseDrop(options.quicTxDrop, txDrop);
#endif
  Zhttp::H2Policy::T h2Policy;
  switch (options.http2) {
    case Http2Mode::force: h2Policy = Zhttp::H2Policy::Force; break;
    case Http2Mode::disable: h2Policy = Zhttp::H2Policy::Disable; break;
    default: h2Policy = Zhttp::H2Policy::Prefer; break;
  }
  bool secure = url.scheme == Zhttp::Scheme::https;
  auto clientConfig = Zhttp::Config()
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
    .heartbeat(quicHeartbeat(options))
    .migration(migrationMode(options))
    .migrationCIDReserve(options.quicMigrationCIDReserve)
    .migrationCloseOnFailure(options.quicMigrationCloseOnFailure)
    .migrationLocal(options.quicMigrationLocalAddr)
    .migrateOnOpen(migrationOnOpen(options))
    .migrateAfterHeaders(options.quicMigrateAfterHeaders)
    .migrateAfterBytes(options.quicMigrateAfterBytes)
    .rxDrop(rxDrop).txDrop(txDrop);

  Client app;
  bool appInited = app.init(
    Zhttp::HubConfig{&mx, "3", "4"}, 1, clientConfig,
    Zhttp::TCPConfig{},
    Zhttp::H2Config{}.caPath(options.ca).policy(h2Policy), quic);
  bool poolInited = appInited && app.pool(
    0, Zhttp::Destination{url.host, url.port, url.ipv6Literal});
  app.txErrorFn(ZiTxErrorFn{[](ZeException &e) {
    ZiLOG(Error, "zhttp", ([e](auto &s) {
      s << "transmit error: " << e;
    }));
    return false;
  }});
  bool appUp = poolInited && app.start();
  if (!appUp) {
    ZiLOG(Error, "zhttp", "client initialization/start failed");
    if (appInited) app.stop();
    app.final();
    mx.stop();
    ZiLog::stop();
    return 1;
  }
  app.workload(options, url);

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
    if (!step) {
      ZhttpUtil::Runtime::wait();
      break;
    }
    if (ZhttpUtil::Runtime::wait(step)) break;
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
      timedOut = !ZhttpUtil::Runtime::trywait();
      break;
    }
  }
  if (timedOut) ZiLOG(Error, "zhttp", "timed out");
  app.stop();
  bool incomplete = app.completed() != options.requests;
  if (incomplete)
    ZiLOG(Error, "zhttp", ([
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
