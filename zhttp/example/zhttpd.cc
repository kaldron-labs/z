//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// static HTTP server

#include <iostream>
#include <string.h>

#ifndef _WIN32
#include <grp.h>
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include <zlib/ZiDaemon.hh>
#include <zlib/Ztcp.hh>
#include <zlib/Ztls.hh>
#include <zlib/Zquic.hh>
#include <zlib/Zhttp.hh>

#include "Zhttpd.hh"

using namespace Zhttpd;

constexpr uint64_t ReqBodyMax = 1<<20;
constexpr unsigned BufBuiltin = 8<<10;
constexpr unsigned BufMax = 100<<20;
constexpr uint64_t H3DataMax = 100<<20;
constexpr uint64_t H3StreamDataMax = 16<<20;
constexpr uint64_t H3BidiMax = 4096;
constexpr uint64_t H3UniMax = 16;

void usage(int code = 1)
{
  std::cerr <<
    "Usage: zhttpd /path/to/wwwroot [OPTION]...\n\n"
    "Options:\n"
    "  --port number              listen port, default 8080 or 80 if root\n"
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
    "  --uid uid/uname            drop user privileges after initialization\n"
    "  --gid gid/gname            drop group privileges after initialization\n"
    "  --chroot                   chroot to wwwroot after initialization\n"
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
    "  --debug                    enable ZiMultiplex and HTTP/3 debug logging\n"
    "  -h, --help                 show help\n" << std::flush;
  ::exit(code);
}

bool loadOptions(Options &options, int argc, char **argv)
{
  bool help = false;
  if (!Zhttpd::loadOptions(options, argc, argv, help)) return false;
  if (help) usage(0);
  return true;
}

#ifndef _WIN32
bool parseID(ZuCSpan s, unsigned &id)
{
  if (!s) return false;
  unsigned n = 0;
  for (unsigned i = 0; i < s.length(); ++i) {
    if (s[i] < '0' || s[i] > '9') return false;
    n = (n * 10) + (s[i] - '0');
  }
  id = n;
  return true;
}

bool resolveUID(ZuCSpan user, uid_t &uid, gid_t &gid, HdrString &name)
{
  unsigned id;
  if (parseID(user, id)) {
    uid = id;
    return true;
  }
  HdrString s{user};
  struct passwd *pw = getpwnam(s.ndata());
  if (!pw) return false;
  uid = pw->pw_uid;
  gid = pw->pw_gid;
  name = user;
  return true;
}

bool resolveGID(ZuCSpan group, gid_t &gid)
{
  unsigned id;
  if (parseID(group, id)) {
    gid = id;
    return true;
  }
  HdrString s{group};
  struct group *gr = getgrnam(s.ndata());
  if (!gr) return false;
  gid = gr->gr_gid;
  return true;
}

bool chrootRoot(Options &options)
{
  Zi::Path root = options.root;
  if (!ZiFile::absolute(root))
    root = ZiFile::append(ZiFile::cwd(), root);
  if (options.singleFile) {
    Zi::Path dir = ZiFile::dirname(root);
    Zi::Path leaf = ZiFile::leafname(root);
    if (!dir || !leaf) return false;
    if (::chdir(dir.ndata()) < 0 || ::chroot(dir.ndata()) < 0 ||
	::chdir("/") < 0)
      return false;
    options.root = leaf;
    return true;
  }
  if (::chdir(root.ndata()) < 0 || ::chroot(root.ndata()) < 0 ||
      ::chdir("/") < 0)
    return false;
  options.root = "/";
  return true;
}

bool dropPrivileges(const Options &options)
{
  if (!options.uid && !options.gid) return true;
  uid_t uid = getuid();
  gid_t gid = getgid();
  HdrString userName;
  if (options.uid && !resolveUID(options.uid, uid, gid, userName))
    return false;
  if (options.gid && !resolveGID(options.gid, gid))
    return false;
  if (userName) {
    if (initgroups(userName.ndata(), gid) < 0) return false;
  } else {
    if (setgroups(0, nullptr) < 0 && errno != EPERM) return false;
  }
  if (setgid(gid) < 0) return false;
  if (setuid(uid) < 0) return false;
  return true;
}
#endif

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
#ifndef _WIN32
  if (options.chroot && !chrootRoot(options)) {
    ZiLOG(Error, "zhttpd", "chroot failed");
    return false;
  }
  if (!dropPrivileges(options)) {
    ZiLOG(Error, "zhttpd", "privilege drop failed");
    return false;
  }
#else
  if (options.chroot || options.uid || options.gid) return false;
#endif
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
    if constexpr (ZuIsSame<Key, ZuStringT<"host">>{})
      req.host = ZuCSpan{value};
    else if constexpr (ZuIsSame<Key, ZuStringT<"authorization">>{})
      req.authorization = ZuCSpan{value};
    else if constexpr (ZuIsSame<Key, ZuStringT<"range">>{})
      req.range = ZuCSpan{value};
    else if constexpr (ZuIsSame<Key, ZuStringT<"if-modified-since">>{})
      req.ifModifiedSince = ZuCSpan{value};
    else if constexpr (ZuIsSame<Key, ZuStringT<"connection">>{})
      req.connection = ZuCSpan{value};
    else if constexpr (ZuIsSame<Key, ZuStringT<"referer">>{})
      req.referer = ZuCSpan{value};
    else if constexpr (ZuIsSame<Key, ZuStringT<"user-agent">>{})
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
  Zhttp::H3::QPackRxTable *qpackRx() const { return qpackRx_; }
  bool qpackDecoderWrite(ZuBSpan span) const {
    return qpackDecoderWrite_ && qpackDecoderWrite_(qpackDecoder_, span);
  }
  uint64_t streamID() const { return streamID_; }
  using ReqSink::body;
  using ReqSink::contentLength;
  using ReqSink::header;
  using ReqSink::operation;
  using ReqSink::version;

  Zhttp::H3::QPackRxTable	*qpackRx_ = nullptr;
  void				*qpackDecoder_ = nullptr;
  bool				(*qpackDecoderWrite_)(void *, ZuBSpan) = nullptr;
  uint64_t			streamID_ = 0;
};

struct RespOps {
  RespOps(const ResponsePlan *plan_) : plan{plan_} { }

  unsigned status() const { return plan->status; }
  template <typename L> void reason(L &&l) const { l(plan->reason); }
  uint64_t contentLength() const { return plan->contentLength; }
  template <typename Key, typename L>
  void header(L &&l) const {
    if constexpr (ZuIsSame<Key, ZuStringT<"content-type">>{})
      l(plan->contentType);
    else if constexpr (ZuIsSame<Key, ZuStringT<"date">>{})
      l(plan->date);
    else if constexpr (ZuIsSame<Key, ZuStringT<"server">>{})
      l(plan->server);
    else if constexpr (ZuIsSame<Key, ZuStringT<"last-modified">>{})
      l(plan->lastModified);
    else if constexpr (ZuIsSame<Key, ZuStringT<"accept-ranges">>{})
      l(plan->file ? ZuCSpan{"bytes"} : ZuCSpan{});
    else if constexpr (ZuIsSame<Key, ZuStringT<"content-range">>{})
      l(plan->contentRange);
    else if constexpr (ZuIsSame<Key, ZuStringT<"location">>{})
      l(plan->location);
    else if constexpr (ZuIsSame<Key, ZuStringT<"www-authenticate">>{})
      l(plan->wwwAuthenticate);
    else if constexpr (ZuIsSame<Key, ZuStringT<"allow">>{})
      l(plan->allow);
    else if constexpr (ZuIsSame<Key, ZuStringT<"connection">>{})
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
  Zhttp::H3::QPackTxTable *qpackTx() const { return qpackTx_; }
  bool qpackEncoderWrite(ZuBSpan span) const {
    return qpackEncoderWrite_ && qpackEncoderWrite_(qpackEncoder_, span);
  }
  uint64_t streamID() const { return streamID_; }
  using RespOps::contentLength;
  using RespOps::header;
  using RespOps::reason;
  using RespOps::status;

  Zhttp::H3::QPackTxTable	*qpackTx_ = nullptr;
  void				*qpackEncoder_ = nullptr;
  bool				(*qpackEncoderWrite_)(void *, ZuBSpan) = nullptr;
  uint64_t			streamID_ = 0;
};

template <typename Tx, typename Builder>
void sendBody(Tx &tx, Builder &builder, const ResponsePlan &resp) {
  if (!resp.sendBody) return;
  auto body = static_cast<typename Builder::Base &>(builder).body(tx);
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

struct StaticH1Server :
  public Zhttp::H1::Server<StaticH1Server, ReqParser<false> > {
  using Base = Zhttp::H1::Server<StaticH1Server, ReqParser<false> >;
  using Base::parser;

  template <typename Link>
  int error(Link &link, ReqParser<false> &) {
    link.app()->state->errors = 1;
    return -1;
  }

  template <typename Link>
  int request(Link &link, ReqParser<false> &parser) {
    ++link.app()->state->requests;
    parser.req.tls = Link::TLS;
    StaticPlanner planner{link.app()->state};
    auto resp = planner.plan(parser.req);
    link.sendResponse(resp);
    link.app()->state->log.write(parser.req, resp, link.remote);
    return resp.close ? -1 : 1;
  }
};

struct StaticH3Server :
  public Zhttp::H3::ServerStream<StaticH3Server, ReqParser<true> > {
  using Base = Zhttp::H3::ServerStream<StaticH3Server, ReqParser<true> >;
  using Base::parser;

  template <typename Stream>
  int error(Stream &stream, ReqParser<true> &) {
    ZiLOG(Debug, "zhttpd.h3", ([id = stream.id()](auto &s) {
      s << "request parse error stream=" << id;
    }));
    stream.link()->app()->state->errors = 1;
    return -1;
  }

  template <typename Stream>
  int request(Stream &stream, ReqParser<true> &parser) {
    auto n = ++stream.link()->app()->state->requests;
    parser.req.h3 = true;
    parser.req.tls = true;
    StaticPlanner planner{stream.link()->app()->state};
    auto resp = planner.plan(parser.req);
    ZiLOG(Debug, "zhttpd.h3", ([
      id = stream.id(), target = parser.req.target,
      status = resp.status, length = resp.contentLength, n
    ](auto &s) mutable {
      s << "request stream=" << id << " total=" << n <<
	" target=" << target << " status=" << status <<
	" content-length=" << length;
    }));
    stream.link()->app()->state->log.write(parser.req, resp, stream.link()->remote);
    stream.sendResponse(ZuMv(resp));
    return 1;
  }
};

ZiMxParams mxParams(bool debug = false)
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
  if (debug) params.debug(true);
#else
  (void)debug;
#endif
  return params;
}

struct HTTPServer : public Ztcp::Server<HTTPServer> {
  struct Link;

  HTTPServer(State *state_) : state{state_} { }

  ZiConnection *accepted(const ZiCxnInfo &ci);
  ZiIP localIP() const { return ZiIP(state->options.addr); }
  unsigned localPort() const { return state->options.port; }
  void listening(const ZiListenInfo &info) {
    ZiLOG(Info, "zhttpd", ([port = info.port](auto &s) {
      s << "http listening: " << port;
    }));
  }
  void listenFailed(bool) {
    state->errors = 1;
    state->done.post();
  }

  State	*state = nullptr;
};

struct HTTPServer::Link :
  public Ztcp::SrvLink<HTTPServer, HTTPServer::Link> {
  using Base = Ztcp::SrvLink<HTTPServer, HTTPServer::Link>;
  using Base::Base;
  enum { TLS = false };

  Link(HTTPServer *app, ZuCSpan remote_) : Base{app}, remote{remote_} { }
  void connected(Zi::Connected) { h1.connected(*this); touch(); }
  void disconnected() {
    app()->mx()->del(&idleTimer);
    h1.disconnected(*this);
    if (counted) {
      --app()->state->active;
      counted = false;
    }
  }
  void touch() {
    auto timeout = app()->state->options.timeout;
    if (!timeout) return;
    app()->mx()->add([link = ZmMkRef(this)]() { link->disconnect(); },
      Zm::now(timeout), ZmScheduler::Update, &idleTimer);
  }

  void sendResponse(const ResponsePlan &resp) {
    auto tx = this->txStream();
    H1RespBuilder builder{&resp};
    builder.response(tx);
    sendBody(tx, builder, resp);
    builder.finish(tx);
  }

  int process(Ztcp::RxStream &rx) {
    int rc = h1.process(*this, rx);
    if (rc >= 0) touch();
    return rc;
  }

  StaticH1Server	h1;
  ZmScheduler::Timer	idleTimer;
  HdrString			remote;
  bool			counted = true;
};

ZiConnection *HTTPServer::accepted(const ZiCxnInfo &ci)
{
  unsigned active = ++state->active;
  if (state->options.maxconn && active > state->options.maxconn) {
    --state->active;
    return nullptr;
  }
  HdrString remote;
  remote << ci.remoteIP;
  return new Link::Cxn(new Link(this, remote), ci);
}

struct TLSServer : public Ztls::Server<TLSServer> {
  using RxBufAlloc = Ztls::RxBufAlloc<BufBuiltin, BufMax, "Zhttp.Buf">;
  using TxBufAlloc = Ztls::TxBufAlloc<BufBuiltin, BufMax, "Zhttp.Buf">;
  struct Link;

  TLSServer(State *state_) : state{state_} { }

  ZiConnection *accepted(const ZiCxnInfo &ci);
  ZiIP localIP() const { return ZiIP(state->options.addr); }
  unsigned localPort() const { return state->options.port; }
  void listening(const ZiListenInfo &info) {
    ZiLOG(Info, "zhttpd", ([port = info.port](auto &s) {
      s << "https listening: " << port;
    }));
  }
  void listenFailed(bool) {
    state->errors = 1;
    state->done.post();
  }

  State	*state = nullptr;
};

struct TLSServer::Link :
  public Ztls::SrvLink<TLSServer, TLSServer::Link,
    TLSServer::RxBufAlloc, TLSServer::TxBufAlloc> {
  using Base = Ztls::SrvLink<TLSServer, TLSServer::Link,
    TLSServer::RxBufAlloc, TLSServer::TxBufAlloc>;
  using Base::Base;
  enum { TLS = true };

  Link(TLSServer *app, ZuCSpan remote_) : Base{app}, remote{remote_} { }
  void connected(Zi::Connected) { h1.connected(*this); touch(); }
  void disconnected() {
    app()->mx()->del(&idleTimer);
    h1.disconnected(*this);
    if (counted) {
      --app()->state->active;
      counted = false;
    }
  }
  void touch() {
    auto timeout = app()->state->options.timeout;
    if (!timeout) return;
    app()->mx()->add([link = ZmMkRef(this)]() { link->disconnect(); },
      Zm::now(timeout), ZmScheduler::Update, &idleTimer);
  }

  void sendResponse(const ResponsePlan &resp) {
    auto tx = this->txStream();
    H1RespBuilder builder{&resp};
    builder.response(tx);
    sendBody(tx, builder, resp);
    builder.finish(tx);
  }

  int process(Ztls::RxStream &rx) {
    int rc = h1.process(*this, rx);
    if (rc >= 0) touch();
    return rc;
  }

  StaticH1Server h1;
  ZmScheduler::Timer idleTimer;
  HdrString	remote;
  bool		counted = true;
};

ZiConnection *TLSServer::accepted(const ZiCxnInfo &ci)
{
  unsigned active = ++state->active;
  if (state->options.maxconn && active > state->options.maxconn) {
    --state->active;
    return nullptr;
  }
  HdrString remote;
  remote << ci.remoteIP;
  return new Link::Cxn(new Link(this, remote), ci);
}

struct H3ServerLink;
struct H3ServerStream;
struct H3Server : public Zquic::Server<H3Server, H3ServerLink> {
  using Link = H3ServerLink;
  using Stream = H3ServerStream;

  H3Server(State *state_) : state{state_} { }

  ZmRef<Link> accepted(const Zquic::InitialInfo &);
  ZiIP localIP() const { return ZiIP(state->options.addr); }
  uint16_t localPort() const { return state->options.port; }
  void listening() {
    ZiLOG(Info, "zhttpd", ([port = this->local().port()](auto &s) {
      s << "h3 listening: " << port;
    }));
  }
  void listenFailed(bool) {
    state->errors = 1;
    state->done.post();
  }
  uint64_t maxData() const { return H3DataMax; }
  uint64_t maxStreamData() const { return H3StreamDataMax; }
  uint64_t maxStreamsBidi() const { return H3BidiMax; }
  uint64_t maxStreamsUni() const { return H3UniMax; }

  State	*state = nullptr;
};

struct H3ServerStream :
  public Zquic::SrvStream<H3ServerLink, H3ServerStream>,
  public Zhttp::H3::CxnStream<H3ServerStream,
    Zhttp::H3::Cxn<H3ServerLink, ZmRef<H3ServerStream> > > {
  using Base = Zquic::SrvStream<H3ServerLink, H3ServerStream>;
  using H3Cxn = Zhttp::H3::Cxn<H3ServerLink, ZmRef<H3ServerStream> >;
  using CxnStream = Zhttp::H3::CxnStream<H3ServerStream, H3Cxn>;
  using Base::Base;

  int process(Zquic::RxStream &rx);
  H3Cxn &h3Cxn() const;

  void sendResponse(ResponsePlan resp);

  StaticH3Server h3;
};

struct H3ServerLink :
  public Zquic::SrvLink<H3Server, H3ServerLink, H3ServerStream> {
  using Base = Zquic::SrvLink<H3Server, H3ServerLink, H3ServerStream>;
  using H3Cxn = Zhttp::H3::Cxn<H3ServerLink, ZmRef<H3ServerStream> >;

  H3ServerLink(H3Server *app, State *state_, ZuCSpan remote_) :
    Base{app}, state{state_}, remote{remote_} { }
  void connected(Zi::Connected info) {
    if (info.transport != Zi::Transport::QUIC ||
	info.version != int(Zquic::Version1) || info.alpn != "h3")
      app()->state->errors = 1;
    if (!h3.openLocal(*this))
      app()->state->errors = 1;
    ZiLOG(Debug, "zhttpd.h3", ([
      remote = ZeString(remote), info
    ](auto &s) {
      s << "connected remote=" << remote <<
	" version=" << info.version << " alpn=" << info.alpn;
    }));
  }
  void disconnected() {
    unsigned active = state ? state->active.load_() : 0;
    uint64_t requests = state ? state->requests.load_() : 0;
    uint64_t errors = state ? state->errors.load_() : 0;
    ZiLOG(Debug, "zhttpd.h3", ([
      remote = ZeString(remote), counted = counted, active, requests, errors,
      haveState = bool(state)
    ](auto &s) {
      s << "disconnected remote=" << remote <<
	" counted=" << counted;
      if (haveState)
	s << " active=" << active <<
	  " requests=" << requests <<
	  " errors=" << errors;
    }));
    if (counted && state) {
      --state->active;
      counted = false;
    }
  }
  void streamed(ZmRef<Stream> stream) {
    ZiLOG(Debug, "zhttpd.h3", ([id = stream ? stream->id() : -1](auto &s) {
      s << "streamed stream=" << id;
    }));
  }

  H3Cxn		h3;
  State		*state = nullptr;
  HdrString		remote;
  bool		counted = true;
};

ZmRef<H3Server::Link> H3Server::accepted(const Zquic::InitialInfo &info)
{
  unsigned active = ++state->active;
  if (state->options.maxconn && active > state->options.maxconn) {
    ZiLOG(Debug, "zhttpd.h3", ([ip = info.peer.ip(), active](auto &s) {
      s << "reject initial peer=" << ip
	<< " active=" << active;
    }));
    --state->active;
    return {};
  }
  ZeString remote;
  remote << info.peer.ip();
  ZiLOG(Debug, "zhttpd.h3", ([remote = ZeString(remote), active](auto &s) {
    s << "accepted remote=" << remote << " active=" << active;
  }));
  return new Link{this, state, remote};
}

H3ServerStream::H3Cxn &H3ServerStream::h3Cxn() const
{
  return this->link()->h3;
}

int H3ServerStream::process(Zquic::RxStream &rx)
{
  if (Zquic::StreamID::uni(uint64_t(this->id()))) {
    auto s = this->CxnStream::process(*this);
    ZiLOG(Debug, "zhttpd.h3", ([id = this->id(), s](auto &out) {
      out << "control stream=" << id << " state=" << s;
    }));
    if (s == Zhttp::H3::CxnState::Error) {
      this->link()->app()->state->errors = 1;
      return -1;
    }
    (void)rx;
    return 0;
  }

  h3.parser.qpackRx_ = &this->link()->h3.qpackRxTable;
  using Cxn = ZuDecay<decltype(this->link()->h3)>;
  h3.parser.qpackDecoder_ = &this->link()->h3;
  h3.parser.qpackDecoderWrite_ = [](void *ptr, ZuBSpan span) {
    return static_cast<Cxn *>(ptr)->qpackDecoderWrite(span);
  };
  h3.parser.streamID_ = uint64_t(this->id());
  int rc = h3.processReq(*this, *this);
  ZiLOG(Debug, "zhttpd.h3", ([id = this->id(), rc](auto &s) {
    s << "process request stream=" << id << " rc=" << rc;
  }));
  return rc;
}

void H3ServerStream::sendResponse(ResponsePlan resp)
{
  ZiLOG(Debug, "zhttpd.h3", ([
    id = this->id(), status = resp.status, length = resp.contentLength
  ](auto &s) {
    s << "queue response stream=" << id << " status=" << status <<
      " content-length=" << length;
  }));
  auto ref = this->link()->findStream(this->id());
  if (!ref) {
    ZiLOG(Error, "zhttpd", ([id = this->id()](auto &s) {
      s << "H3 response stream not found: " << id;
    }));
    return;
  }
  this->link()->app()->txInvoke([
    link = ZmMkRef(this->link()), ref, resp = ZuMv(resp)
  ]() mutable {
    auto tx = ref->txStream_();
    H3RespBuilder builder{&resp};
    builder.qpackTx_ = &link->h3.qpackTxTable;
    builder.qpackEncoder_ = &link->h3;
    builder.qpackEncoderWrite_ = [](void *ptr, ZuBSpan span) {
      return static_cast<ZuDecay<decltype(link->h3)> *>(ptr)->
	qpackEncoderWrite(span);
    };
    builder.streamID_ = uint64_t(ref->id());
    builder.response(tx);
    sendBody(tx, builder, resp);
    builder.finish(tx);
    bool ok = link->send_(ref, "", true);
    ZiLOG(Debug, "zhttpd.h3", ([id = ref->id(), ok](auto &s) {
      s << "send response stream=" << id << " ok=" << ok;
    }));
    if (!ok)
      ZiLOG(Error, "zhttpd", ([id = ref->id()](auto &s) {
	s << "H3 response send failed: " << id;
      }));
  });
}

int main(int argc, char **argv)
{
  Options options;
  if (!loadOptions(options, argc, argv)) usage();
  ZeString error;
  if (!validate(options, error)) {
    std::cerr << "zhttpd: " << error << '\n' << std::flush;
    return 1;
  }
  ZiLog::init("zhttpd", options.syslog ? "daemon" : "user");
  ZiLog::level(options.debug ? Ze::Debug : Ze::Info);
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

  ZiMultiplex mx(mxParams(options.debug));
  if (!mx.start()) {
    ZiLOG(Error, "zhttpd", "ZiMultiplex start failed");
    state.log.final();
    ZiLog::stop();
    return 1;
  }
  bool httpInit = false;
  bool tlsInit = false;
  bool h3Init = false;
  HTTPServer http{&state};
  TLSServer tls{&state};
  H3Server h3{&state};
  if (state.options.http) {
    if (!http.init(Ztcp::ServerParams(&mx, "3", "4"))) {
      ZiLOG(Error, "zhttpd", "HTTP server initialization failed");
      mx.stop();
      state.log.final();
      ZiLog::stop();
      return 1;
    }
    httpInit = true;
  }
  if (state.options.https) {
    ZuCSpan alpn[] = { "http/1.1" };
    if (!tls.init(
	  Ztls::ServerParams(&mx, "3", "4")
	    .certPath(state.options.cert).keyPath(state.options.key)
	    .alpn(alpn))) {
      ZiLOG(Error, "zhttpd", "HTTPS server initialization failed");
      if (httpInit) http.final();
      mx.stop();
      state.log.final();
      ZiLog::stop();
      return 1;
    }
    tlsInit = true;
  }
  if (state.options.http3) {
    ZuCSpan alpn[] = { "h3" };
    if (!h3.init(
	  Zquic::ServerParams(&mx, "3", "4")
	    .certPath(state.options.cert).keyPath(state.options.key).alpn(alpn)
	    .maxData(H3DataMax).maxStreamData(H3StreamDataMax)
	    .maxStreamsBidi(H3BidiMax).maxStreamsUni(H3UniMax))) {
      ZiLOG(Error, "zhttpd", "H3 server initialization failed");
      if (tlsInit) tls.final();
      if (httpInit) http.final();
      mx.stop();
      state.log.final();
      ZiLog::stop();
      return 1;
    }
    h3Init = true;
  }
  if (!httpInit && !tlsInit && !h3Init) {
    ZiLOG(Error, "zhttpd", "no transport enabled");
    mx.stop();
    state.log.final();
    ZiLog::stop();
    return 1;
  }
  if (httpInit) http.listen();
  if (tlsInit) tls.listen();
  if (h3Init && !h3.listen()) {
    ZiLOG(Error, "zhttpd", "H3 server listen failed");
    state.errors = 1;
    state.done.post();
  }
  state.done.wait();
  if (h3Init) h3.final();
  if (tlsInit) tls.final();
  if (httpInit) http.final();
  mx.stop();
  state.log.final();
  ZiLog::stop();
  return state.errors ? 1 : 0;
}
