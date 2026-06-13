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

#include "ZhttpStaticServer.hh"

using namespace ZhttpStatic;

void usage(int code = 1)
{
  std::cerr <<
    "Usage: zhttpserver /path/to/wwwroot [OPTION]...\n\n"
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
    "  --forward host url         301 redirect by Host, repeatable\n"
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
    "  -h, --help                 show help\n" << std::flush;
  ::exit(code);
}

bool parseUInt(ZuCSpan v, unsigned &out)
{
  if (!v) return false;
  unsigned n = 0;
  for (unsigned i = 0; i < v.length(); ++i) {
    if (v[i] < '0' || v[i] > '9') return false;
    n = (n * 10) + (v[i] - '0');
  }
  out = n;
  return true;
}

bool loadOptions(Options &options, int argc, char **argv)
{
  bool root = false;
  bool httpSet = false;
  for (int i = 1; i < argc; ++i) {
    ZuCSpan arg{argv[i]};
    auto value = [&]() -> ZuCSpan {
      if (auto eq = arg.find([](auto c) { return c == '='; }); eq >= 0)
	return ZuCSpan{arg.data() + eq + 1, arg.length() - unsigned(eq) - 1};
      if (i + 1 >= argc) return {};
      return ZuCSpan{argv[++i]};
    };
    if (arg == "-h" || arg == "--help") usage(0);
    else if (arg == "--http") { options.http = true; httpSet = true; }
    else if (arg == "--https") options.https = true;
    else if (arg == "--http3") options.http3 = true;
    else if (arg == "--ipv6") options.ipv6 = true;
    else if (arg == "--daemon") options.daemon = true;
    else if (arg == "--syslog") options.syslog = true;
    else if (arg == "--no-listing") options.noListing = true;
    else if (arg == "--chroot") options.chroot = true;
    else if (arg == "--no-keepalive") options.noKeepalive = true;
    else if (arg == "--single-file") options.singleFile = true;
    else if (arg == "--hide-dotfiles") options.hideDotfiles = true;
    else if (arg == "--forward-https") options.forwardHttps = true;
    else if (arg == "--no-server-id") options.noServerID = true;
    else if (arg == "--addr") options.addr = value();
    else if (arg.starts("--addr=")) options.addr = value();
    else if (arg == "--port" || arg.starts("--port=")) {
      unsigned port;
      if (!parseUInt(value(), port) || port > 65535) return false;
      options.port = port;
    } else if (arg == "--cert" || arg.starts("--cert=")) options.cert = value();
    else if (arg == "--key" || arg.starts("--key=")) options.key = value();
    else if (arg == "--index" || arg.starts("--index=")) options.index = value();
    else if (arg == "--mimetypes" || arg.starts("--mimetypes="))
      options.mimetypes = value();
    else if (arg == "--default-mimetype" ||
	arg.starts("--default-mimetype="))
      options.defaultMimetype = value();
    else if (arg == "--uid" || arg.starts("--uid=")) options.uid = value();
    else if (arg == "--gid" || arg.starts("--gid=")) options.gid = value();
    else if (arg == "--pidfile" || arg.starts("--pidfile="))
      options.pidfile = value();
    else if (arg == "--log" || arg.starts("--log=")) options.logPath = value();
    else if (arg == "--timeout" || arg.starts("--timeout=")) {
      unsigned timeout;
      if (!parseUInt(value(), timeout)) return false;
      options.timeout = timeout;
    } else if (arg == "--maxconn" || arg.starts("--maxconn=")) {
      unsigned maxconn;
      if (!parseUInt(value(), maxconn)) return false;
      options.maxconn = maxconn;
    } else if (arg == "--auth" || arg.starts("--auth=")) {
      if (!parseAuth(options, value())) return false;
    } else if (arg == "--forward" || arg.starts("--forward=")) {
      ZuCSpan host = value();
      ZuCSpan url = value();
      if (!parseForward(options, host, url)) return false;
    } else if (arg == "--forward-all" || arg.starts("--forward-all="))
      options.forwardAll = value();
    else if (arg[0] == '-')
      return false;
    else if (!root) {
      options.root = arg;
      root = true;
    } else
      return false;
  }
#ifndef _WIN32
  if (options.port == 8080 && !geteuid()) options.port = 80;
#endif
  if (!httpSet && (options.https || options.http3)) options.http = false;
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

bool resolveUID(ZuCSpan user, uid_t &uid, gid_t &gid, ZtString<> &name)
{
  unsigned id;
  if (parseID(user, id)) {
    uid = id;
    return true;
  }
  auto s = str(user);
  struct passwd *pw = getpwnam(s.c_str());
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
  auto s = str(group);
  struct group *gr = getgrnam(s.c_str());
  if (!gr) return false;
  gid = gr->gr_gid;
  return true;
}

bool chrootRoot(Options &options)
{
  auto root = std::filesystem::absolute(std::filesystem::path{str(options.root)});
  if (options.singleFile) {
    auto dir = root.parent_path();
    auto leaf = root.filename();
    if (dir.empty() || leaf.empty()) return false;
    if (::chdir(dir.c_str()) < 0 || ::chroot(dir.c_str()) < 0 ||
	::chdir("/") < 0)
      return false;
    options.root = zstr(leaf.string());
    return true;
  }
  if (::chdir(root.c_str()) < 0 || ::chroot(root.c_str()) < 0 ||
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
  ZtString<> userName;
  if (options.uid && !resolveUID(options.uid, uid, gid, userName))
    return false;
  if (options.gid && !resolveGID(options.gid, gid))
    return false;
  if (userName) {
    auto s = str(userName);
    if (initgroups(s.c_str(), gid) < 0) return false;
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
    ZiLOG(Error, "zhttpserver", "PID file names a running process");
    return false;
  }
  if (rc != ZiDaemon::OK) {
    ZiLOG(Error, "zhttpserver", "daemon initialization failed");
    return false;
  }
#ifndef _WIN32
  if (options.chroot && !chrootRoot(options)) {
    ZiLOG(Error, "zhttpserver", "chroot failed");
    return false;
  }
  if (!dropPrivileges(options)) {
    ZiLOG(Error, "zhttpserver", "privilege drop failed");
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
  public Zhttp::H1ReqParser<Impl, ReqHeaders, (1<<20)> { };
template <typename Impl>
struct ReqParserBase_<Impl, true> :
  public Zhttp::H3ReqParser<Impl, ReqHeaders, (1<<20)> { };

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
    body << ZuCSpan{resp.body};
    return;
  }
  if (!resp.file || !resp.fileLength) return;
  ZiMMapFile map;
  if (map.mmap(resp.filePath, ZiFile::ReadOnly | ZiFile::GC,
	resp.fileOffset + resp.fileLength, false) == Zi::OK) {
    const char *p = static_cast<const char *>(map.addr()) + resp.fileOffset;
    body << ZuCSpan{p, unsigned(resp.fileLength)};
    return;
  }
  ZiFile file;
  if (file.open(resp.filePath, ZiFile::ReadOnly | ZiFile::GC) != Zi::OK)
    return;
  char buf[16384];
  uint64_t offset = resp.fileOffset;
  uint64_t left = resp.fileLength;
  while (left) {
    unsigned n = left > sizeof(buf) ? sizeof(buf) : unsigned(left);
    int r = file.pread(offset, buf, n);
    if (r <= 0) break;
    body << ZuCSpan{buf, unsigned(r)};
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
    stream.link()->app()->state->errors = 1;
    return -1;
  }

  template <typename Stream>
  int request(Stream &stream, ReqParser<true> &parser) {
    ++stream.link()->app()->state->requests;
    parser.req.h3 = true;
    parser.req.tls = true;
    StaticPlanner planner{stream.link()->app()->state};
    auto resp = planner.plan(parser.req);
    stream.sendResponse(resp);
    stream.link()->app()->state->log.write(parser.req, resp, stream.link()->remote);
    return 1;
  }
};

ZiMxParams mxParams()
{
  return ZiMxParams()
    .scheduler([](auto &s) {
      s.nThreads(4)
	.thread(1, [](auto &t) { t.isolated(1); })
	.thread(2, [](auto &t) { t.isolated(1); })
	.thread(3, [](auto &t) { t.isolated(1); })
	.thread(4, [](auto &t) { t.isolated(1); });
    })
    .rxThread(1).txThread(2);
}

struct HTTPServer : public Ztcp::Server<HTTPServer> {
  struct Link;

  HTTPServer(State *state_) : state{state_} { }

  ZiConnection *accepted(const ZiCxnInfo &ci);
  ZiIP localIP() const { return ZiIP(state->options.addr); }
  unsigned localPort() const { return state->options.port; }
  void listening(const ZiListenInfo &info) {
    std::cerr << "http listening: " << info.port << '\n' << std::flush;
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

  StaticH1Server h1;
  ZmScheduler::Timer idleTimer;
  ZtString<>	remote;
  bool		counted = true;
};

ZiConnection *HTTPServer::accepted(const ZiCxnInfo &ci)
{
  unsigned active = ++state->active;
  if (state->options.maxconn && active > state->options.maxconn) {
    --state->active;
    return nullptr;
  }
  ZtString<> remote;
  remote << ci.remoteIP;
  return new Link::Cxn(new Link(this, remote), ci);
}

struct TLSServer : public Ztls::Server<TLSServer> {
  using RxBufAlloc = Ztls::RxBufAlloc<8<<10, 100<<20, "Zhttp.Buf">;
  using TxBufAlloc = Ztls::TxBufAlloc<8<<10, 100<<20, "Zhttp.Buf">;
  struct Link;

  TLSServer(State *state_) : state{state_} { }

  ZiConnection *accepted(const ZiCxnInfo &ci);
  ZiIP localIP() const { return ZiIP(state->options.addr); }
  unsigned localPort() const { return state->options.port; }
  void listening(const ZiListenInfo &info) {
    ZiLOG(Info, "zhttpserver", ([port = info.port](auto &s) {
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
  ZtString<>	remote;
  bool		counted = true;
};

ZiConnection *TLSServer::accepted(const ZiCxnInfo &ci)
{
  unsigned active = ++state->active;
  if (state->options.maxconn && active > state->options.maxconn) {
    --state->active;
    return nullptr;
  }
  ZtString<> remote;
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
    ZiLOG(Info, "zhttpserver", ([port = this->local().port()](auto &s) {
      s << "h3 listening: " << port;
    }));
  }
  void listenFailed(bool) {
    state->errors = 1;
    state->done.post();
  }
  uint64_t maxData() const { return 100<<20; }
  uint64_t maxStreamData() const { return 16<<20; }
  uint64_t maxStreamsBidi() const { return 64; }
  uint64_t maxStreamsUni() const { return 16; }

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

  H3ServerLink(H3Server *app, ZuCSpan remote_) :
    Base{app}, remote{remote_} { }
  void connected(Zi::Connected info) {
    if (info.transport != Zi::Transport::QUIC ||
	info.version != int(Zquic::Version1) || info.alpn != "h3")
      app()->state->errors = 1;
    if (!h3.openLocal(*this))
      app()->state->errors = 1;
  }
  void disconnected() {
    if (counted) {
      --app()->state->active;
      counted = false;
    }
  }
  void streamed(ZmRef<Stream>) { }

  H3Cxn	h3;
  ZtString<>	remote;
  bool		counted = true;
};

ZmRef<H3Server::Link> H3Server::accepted(const Zquic::InitialInfo &info)
{
  unsigned active = ++state->active;
  if (state->options.maxconn && active > state->options.maxconn) {
    --state->active;
    return {};
  }
  ZtString<> remote;
  remote << info.peer.ip();
  return new Link{this, remote};
}

H3ServerStream::H3Cxn &H3ServerStream::h3Cxn() const
{
  return this->link()->h3;
}

int H3ServerStream::process(Zquic::RxStream &rx)
{
  if (Zquic::StreamID::uni(uint64_t(this->id()))) {
    auto s = this->CxnStream::process(*this);
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
  return h3.processReq(*this, *this);
}

void H3ServerStream::sendResponse(ResponsePlan resp)
{
  auto ref = this->link()->findStream(this->id());
  if (!ref) return;
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
    link->send_(ref, "", true);
  });
}

int main(int argc, char **argv)
{
  Options options;
  if (!loadOptions(options, argc, argv)) usage();
  std::string error;
  if (!validate(options, error)) {
    std::cerr << "zhttpserver: " << error << '\n' << std::flush;
    return 1;
  }
  ZiLog::init("zhttpserver", options.syslog ? "daemon" : "user");
  ZiLog::level(0);
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
  state.mime.init(state.options);
  if (!state.log.init(state.options)) {
    std::cerr << "zhttpserver: failed to open access log\n" << std::flush;
    ZiLog::stop();
    return 1;
  }

  ZiMultiplex mx(mxParams());
  if (!mx.start()) {
    std::cerr << "ZiMultiplex start failed\n" << std::flush;
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
      std::cerr << "HTTP server initialization failed\n" << std::flush;
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
      std::cerr << "HTTPS server initialization failed\n" << std::flush;
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
	    .maxData(100<<20).maxStreamData(16<<20)
	    .maxStreamsBidi(64).maxStreamsUni(16))) {
      std::cerr << "H3 server initialization failed\n" << std::flush;
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
    std::cerr << "zhttpserver: no transport enabled\n" << std::flush;
    mx.stop();
    state.log.final();
    ZiLog::stop();
    return 1;
  }
  if (httpInit) http.listen();
  if (tlsInit) tls.listen();
  if (h3Init && !h3.listen()) {
    std::cerr << "H3 server listen failed\n" << std::flush;
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
