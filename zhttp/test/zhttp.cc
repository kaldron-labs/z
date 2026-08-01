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

#include <zlib/ZtcHash.hh>
#include <zlib/ZtcHeap.hh>

#include <zlib/ZfCLI.hh>

#include <zlib/ZiFile.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiHashCSV.hh>
#include <zlib/ZiHeapCSV.hh>

#include <zlib/Zhttp.hh>

#include "ZhttpPut.hh"

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
  uint32_t	bodyTxBatch = Zhttp::BodyDeflt::TxBatch;
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
  (((bodyTxBatch), (CLI::Long<"body-tx-batch">)),             (UInt32,
							 Zhttp::BodyDeflt::TxBatch)),
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
    "  --body-tx-batch=N   maximum request-body bytes produced per Tx turn\n"
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

int8_t migrationMode(const Options &options)
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
  if (!options.requests || !options.concurrency || !options.bodyTxBatch)
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
  "accept");
using ResponseHeaders = ZhttpHeaders(
  "alt-svc",
  "connection",
  "location");

using URL = Zhttp::URL;

constexpr unsigned MaxRedirects = 8;
constexpr uint64_t RespBodyMax = 100<<20;

struct Req {
  unsigned	id = 0;
  unsigned	requests = 1;
  URL		url;
  HdrString	output;
  bool		discardResponse = false;
  bool		put = false;
  ZhttpPut::Record putRecord;
  ZhttpPut::String requestJSON;
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

using State = Req;

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

inline ReqLogCtx reqLogCtx(const Req &req)
{
  return {req.id, req.requests};
}

bool parseAuthority(
  ZuCSpan authority, ZuCSpan &host, ZuCSpan &port, ZuCSpan &error)
{
  port = {};
  if (!authority) {
    error = "missing URL host";
    return false;
  }
  if (authority[0] == '[') {
    ZuCSpan rest = authority;
    rest.offset(1);
    auto close = rest.find([](auto c) { return c == ']'; });
    if (close < 0) {
      error = "invalid URL authority";
      return false;
    }
    host = rest;
    host.trunc(close);
    rest.offset(close + 1);
    if (!host) {
      error = "invalid URL authority";
      return false;
    }
    if (!rest) return true;
    if (rest[0] != ':') {
      error = "invalid URL authority";
      return false;
    }
    port = rest;
    port.offset(1);
    if (!port) {
      error = "invalid URL authority";
      return false;
    }
    return true;
  }

  for (unsigned i = 0; i < authority.length(); ++i) {
    if (authority[i] != ':') continue;
    host = authority;
    host.trunc(i);
    port = authority;
    port.offset(i + 1);
    if (!host || !port) {
      error = "invalid URL authority";
      return false;
    }
    return true;
  }

  host = authority;
  return true;
}

bool parseMigrationLocal(ZuCSpan s, ZiSockAddr &addr)
{
  ZuCSpan host, port_, msg;
  if (!parseAuthority(s, host, port_, msg)) return false;
  uint16_t port = 0;
  if (port_) {
    unsigned p = ZuBox<unsigned>(port_);
    if (p > 65535) return false;
    port = p;
  }
  try {
    ZiIP ip{host};
    if (!ip) return false;
    addr.init(ip, port);
    return true;
  } catch (...) {
    return false;
  }
}

void closeBody(Req &req)
{
  if (req.bodyFileOpen) {
    req.bodyFile.close();
    req.bodyFileOpen = false;
  }
}

void resetResponse(Req &req, bool truncateOutput)
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
  Req &req, const Options &options, const URL &url, unsigned id)
{
  req = {};
  req.id = id;
  req.requests = options.requests;
  req.url = url;
  if (!options.discardResponse)
    req.output = outputPath(options.output, id, options.requests);
  req.discardResponse = options.discardResponse;
  req.put = options.put;
  if (req.put) {
    req.putRecord.id = id;
    req.putRecord.text << "zhttp-put-" << ZuBoxed(id);
    ZfJSON::save(req.requestJSON, req.putRecord);
  }
  req.logResponse = hotLog(options);
}

template <typename S>
void reqLogPrefix(const Req &req, S &s)
{
  if (req.requests > 1) s << "req=" << req.id << ' ';
}
template <typename S>
void reqLogPrefix(const ReqLogCtx &ctx, S &s)
{
  if (ctx.requests > 1) s << "req=" << ctx.id << ' ';
}

bool truncateOutputPath(Req &req)
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

void logFraming(State &state)
{
  if (state.framingLogged) return;
  state.framingLogged = true;
  if (!state.logResponse) return;
  auto ctx = reqLogCtx(state);
  auto chunked = state.chunked;
  auto contentLength = state.contentLength;
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

void logConnected(const State &, const Zhttp::ConnectedInfo &);

struct ClientCallbacks :
  public Zhttp::Agent<
    ClientCallbacks, Req, RequestHeaders, ResponseHeaders, RespBodyMax,
    Zhttp::Body::OptionalFixed<unsigned>> {
  using Base = Zhttp::Agent<
    ClientCallbacks, Req, RequestHeaders, ResponseHeaders, RespBodyMax,
    Zhttp::Body::OptionalFixed<unsigned>>;

  URL requestURL(const State &state) const { return state.url; }

  template <typename L>
  void requestOperation(const State &state, const URL &url, L &&l) {
    ZuCSpan path;
    ZuCSpan query;
    url.pathQuery(path, query);
    l(state.put ? Zhttp::Method::PUT : Zhttp::Method::GET, path, query);
  }
  bool requestReplayable(const State &) const { return true; }
  bool requestReproducible(const State &) const { return true; }
  uint64_t requestContentLength(const State &state) const {
    return state.put ? state.requestJSON.length() : 0;
  }
  bool requestHasBody(const State &state) const { return state.put; }
  unsigned requestBodyCursor(State &) { return 0; }
  template <typename Tx>
  int requestBody(
    State &state, unsigned &cursor, Tx &tx, unsigned batch) {
    if (!state.put || cursor >= state.requestJSON.length())
      return Zhttp::BodyProduce::Done;
    unsigned length = state.requestJSON.length() - cursor;
    if (length > batch) length = batch;
    tx << ZuCSpan{state.requestJSON.data() + cursor, length};
    cursor += length;
    return cursor < state.requestJSON.length() ?
      Zhttp::BodyProduce::More : Zhttp::BodyProduce::Done;
  }
  template <typename L>
  void requestHost(const State &, const URL &url, L &&l) {
    l(ZuCSpan{url.host});
  }
  template <typename Key, typename L>
  void requestHeader(const State &, L &&l) {
    if constexpr (Key{}() == "user-agent")
      l("zhttp/1.0");
    else if constexpr (Key{}() == "accept")
      l("*/*");
    else
      l("");
  }

  void responseHeadersDone(State &state) {
    if (state.responseHeadersDone) return;
    state.responseHeadersDone = true;
  }

  void responseStatus(State &state, unsigned status) {
    state.status = status;
    state.redirecting = redirectStatus(status);
    if (!state.redirecting && !state.discardResponse &&
	!truncateOutputPath(state)) return;
    if (!state.logResponse) return;
    auto ctx = reqLogCtx(state);
    ZiLOG(Info, "zhttp.response", ([ctx, status](auto &s) {
      reqLogPrefix(ctx, s);
      s << "status: " << status;
    }));
  }
  void responseContentLength(State &state, uint64_t contentLength) {
    state.contentLength = contentLength;
  }
  void responseChunked(State &state) {
    state.chunked = true;
  }
  template <typename Key>
  void responseHeader(State &state, ZuBSpan value) {
    if (!state.logResponse) return;
    auto ctx = reqLogCtx(state);
    ZeString value_;
    value_ << ZuCSpan(value);
    ZiLOG(Info, "zhttp.response", ([ctx, value = ZuMv(value_)](auto &s) {
      reqLogPrefix(ctx, s);
      s << "header " << Key{}() << ": " << value;
    }));
  }

  template <typename Rx>
  void responseBody(State &state, Rx &rx) {
    logFraming(state);
    responseHeadersDone(state);
    Zhttp::bodyEach(rx, [&state](ZuBSpan span) {
      state.bodyBytes += span.length();
      ++state.bodyChunks;
      if (state.put) state.responseJSON << span;
      if (state.redirecting || state.discardResponse) return;
      if (!truncateOutputPath(state)) return;
      if (!state.bodyFileOpen) {
	state.bodyFile = ZiFile(state.output, ZiFile::Write | ZiFile::GC);
	if (!state.bodyFile) {
	  auto ctx = reqLogCtx(state);
	  ZiLOG(Error, "zhttp", ([
	    ctx, output = ZeString(state.output)](auto &s) {
	    reqLogPrefix(ctx, s);
	    s << "failed to open " << output;
	  }));
	  state.failed = true;
	  state.done = true;
	  return;
	}
	state.bodyFileOpen = true;
      }
      if (state.bodyFile.write(span.data(), span.length()) == Zi::OK)
	return;
      auto ctx = reqLogCtx(state);
      ZiLOG(Error, "zhttp", ([ctx](auto &s) {
	reqLogPrefix(ctx, s);
	s << "failed to write body chunk";
      }));
      state.failed = true;
      state.done = true;
    });
  }

  void responseEnd(State &state, bool ok) {
    auto ctx = reqLogCtx(state);
    if (ok && state.put && !state.redirecting) {
      ZhttpPut::Record record;
      if (!ZhttpPut::load(record, state.responseJSON) ||
	  !ZhttpPut::equals(record, state.putRecord)) {
	ok = false;
	state.failed = true;
      }
    }
    if (ok) {
      if (!state.redirecting) responseHeadersDone(state);
      if (state.logResponse) {
	auto bodyBytes = state.bodyBytes;
	auto bodyChunks = state.bodyChunks;
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
    closeBody(state);
  }

  void redirected(State &state, const URL &) {
    resetResponse(state, true);
  }

  void completed(State &state, const Zhttp::Result &result) {
    closeBody(state);
    state.done = true;
    state.failed = !result.ok() ||
      (state.put &&
	(result.requestBodyProduced != state.requestJSON.length() ||
	 result.requestBodyCommitted != state.requestJSON.length() ||
	 result.requestBodyReset || result.requestBodyDiscarded ||
	 result.responseBodyReceived != state.bodyBytes ||
	 result.responseBodyConsumed != state.bodyBytes ||
	 result.responseBodyReset || result.responseBodyDiscarded));
  }

  void connected(State &state, const Zhttp::ConnectedInfo &info) {
    logConnected(state, info);
  }

  bool responseFailed(const State &state) const { return state.failed; }
};

void logConnected_(
  const State &state, ZuCSpan transport, int version, ZuCSpan alpn)
{
  ZiLOG(Info, "zhttp", ([
    transport = ZeString(transport),
    host = ZeString(state.url.host),
    version,
    alpn = ZeString(alpn)
  ](auto &s) {
    s << transport << " connected (hostname: " << host;
    if (version) s << " version: " << version;
    if (alpn) s << " ALPN: " << alpn;
    s << ')';
  }));
}

void logConnected(const State &state, const Zhttp::ConnectedInfo &info)
{
  ZuCSpan transport = info.transport == Zhttp::Transport::QUIC ? "QUIC" :
    info.transport == Zhttp::Transport::TLS ? "TLS" : "TCP";
  logConnected_(state, transport, int(info.version), info.alpn);
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

  URL url;
  auto urlError = Zhttp::URL::parse(url, options.url);
  if (!urlError.ok()) {
    ZiLOG(Error, "zhttp", ([code = urlError.code,
	offset = urlError.offset](auto &s) {
      s << "invalid URL (code=" << int(code) << ", offset=" << offset << ')';
    }));
    usage();
  }

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

  ZiMultiplex mx(mxParams(options));
  if (!mx.start()) {
    ZiLOG(Error, "zhttp", "ZiMultiplex start failed");
    return 1;
  }

  int8_t policy;
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
  Zhttp::AgentConfig agentConfig;
  int8_t h2Policy;
  switch (options.http2) {
    case Http2Mode::force: h2Policy = Zhttp::H2Policy::Force; break;
    case Http2Mode::disable: h2Policy = Zhttp::H2Policy::Disable; break;
    default: h2Policy = Zhttp::H2Policy::Prefer; break;
  }
  agentConfig
    .concurrency(options.concurrency)
    .maxPending(options.requests)
    .bodyTxBatch(options.bodyTxBatch)
    .requestTimeout(options.timeout)
    .maxRedirects(MaxRedirects)
    .maxRetries(options.retries)
    .protocol(policy)
    .h2Policy(h2Policy)
    .tcp(true)
    .tls(policy != Zhttp::ProtocolPolicy::ForceH3)
    .quic(policy != Zhttp::ProtocolPolicy::DisableH3);
  Zhttp::QUICConfig quic;
  quic
    .caPath(options.ca).keyLogPath(options.keyLog)
    .maxStreamsDuplex(options.concurrency)
    .heartbeat(quicHeartbeat(options))
    .migration(migrationMode(options))
    .migrationCIDReserve(options.quicMigrationCIDReserve)
    .migrationCloseOnFailure(options.quicMigrationCloseOnFailure)
    .migrationLocal(options.quicMigrationLocalAddr)
    .migrateOnOpen(migrationOnOpen(options))
    .migrateAfterHeaders(options.quicMigrateAfterHeaders)
    .migrateAfterBytes(options.quicMigrateAfterBytes)
    .rxDrop(rxDrop).txDrop(txDrop);

  ClientCallbacks app;
  bool appInited = app.init(
    Zhttp::EngineConfig{&mx, "3", "4"}, agentConfig,
    Zhttp::TCPConfig{},
    Zhttp::H2Config{}.caPath(options.ca).policy(h2Policy), quic);
  bool appUp = appInited && app.start();
  if (!appUp) {
    ZiLOG(Error, "zhttp", "client initialization/start failed");
    if (appInited) app.stop();
    app.final();
    mx.stop();
    ZiLog::stop();
    return 1;
  }
  app.diagnostic(options.memDiag,
    Zhttp::DiagnosticFn{[]() { printMemDiag(); }});
#ifdef Zquic_DEBUG
  app.diagnostic(options.quicDiag,
    Zhttp::DiagnosticFn{[&app]() { app.printQUICDiag(); }});
#endif

  ZtArray<Req, ZtArrayHeapID<"zhttp.Request">> requests;
  requests.length(options.requests);
  for (unsigned i = 0; i < requests.length(); ++i) {
    initReq(requests[i], options, url, i);
    resetResponse(requests[i], true);
  }
  app.submit(requests.data(), requests.length());

  bool timedOut = !app.wait(options.timeout);
  if (timedOut) ZiLOG(Error, "zhttp", "timed out");
  app.stop();
  bool incomplete = app.Base::completed() != requests.length();
  if (incomplete)
    ZiLOG(Error, "zhttp", ([
      completed = app.Base::completed(), expected = requests.length(),
      active = app.Base::active(), pending = app.Base::pending()
    ](auto &s) {
      s << "incomplete run (completed=" << completed <<
	", expected=" << expected << ", active=" << active <<
	", pending=" << pending << ')';
    }));
  int rc = timedOut || incomplete || app.failed() ? 1 : 0;
  app.final();

  mx.stop();
  ZiLog::stop();

  return rc;
}
