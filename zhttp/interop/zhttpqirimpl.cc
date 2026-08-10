//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include "zhttpqir.hh"

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>

#include <zlib/ZuBox.hh>
#include <zlib/ZuMatcher.hh>
#include <zlib/ZuTokenizer.hh>
#include <zlib/ZiFile.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/Zquic.hh>
#include <zlib/Zhttp.hh>

#include "zhttpd.hh"

namespace Zhttp::QIR {

static constexpr ZuCSpan DefaultWWW{"/www"};
static constexpr ZuCSpan DefaultDownloads{"/downloads"};
static constexpr ZuCSpan DefaultCA{"/certs/ca.pem"};
static constexpr ZuCSpan DefaultCert{"/certs/cert.pem"};
static constexpr ZuCSpan DefaultKey{"/certs/priv.key"};
static constexpr ZuCSpan Scheme{"https://"};
static constexpr ZuCSpan HQGet{"GET "};
static constexpr ZuCSpan HQALPN{"hq-interop"};
static constexpr ZuCSpan H3ALPN{"h3"};
static constexpr ZuCSpan QIRMigrationModeName{"active"};
static constexpr ZuCSpan QIRMigrationCIDReserveName{"4"};
// Rebind tests emulate transparent NAT remapping; a 1s PING restores path
// progress before the simulator's 5s rebind cadence can compound stalls.
static constexpr ZuTime QIRRebindHeartBeat{1};

using H3ReqHeaders = ZhttpHeaders("user-agent", "accept");
using H3RespHeaders = ZhttpHeaders("location");

enum {
  H3DataMax = 100<<20,
  H3StreamDataMax = 16<<20,
  H3BidiMax = 4096,
  H3UniMax = 16,
  H3RespBodyMax = 100<<20
};

Zquic::MigrationMode::T qirMigrationMode()
{
  return Zquic::MigrationMode::Active;
}

unsigned qirMigrationCIDReserve()
{
  return QIRMigrationCIDReserve;
}

ZuTime qirHeartBeat(Case testCase)
{
  switch (testCase) {
    case RebindPort:
    case RebindAddr:
      return QIRRebindHeartBeat;
    case Handshake:
    case Transfer:
    case HTTP3:
    case ConnectionMigration:
    case Unsupported:
      return {};
  }
  return {};
}

const char *qirMigrationModeArg()
{
  return QIRMigrationModeName.data();
}

const char *qirMigrationCIDReserveArg()
{
  return QIRMigrationCIDReserveName.data();
}

template <typename Params>
static Params qirParams_(Params params, const Env &env)
{
  return ZuMv(params)
    .migrationMode(qirMigrationMode())
    .migCIDRes(qirMigrationCIDReserve())
    .heartBeat(env.heartBeat);
}

static void setString_(ZtString<> &out, ZuCSpan in)
{
  out.length(0);
  out << in;
}

static void setQLogPath_(ZtString<> &out, ZuCSpan dir, Role role)
{
  out.length(0);
  if (!dir.length()) return;
  out << dir;
  if (dir[dir.length() - 1] != '/') out << '/';
  out << roleName(role) << ".sqlog";
}

static ZuCSpan env_(const char *(*getenvFn)(const char *), const char *name)
{
  if (const char *v = getenvFn(name)) return ZuCSpan{v};
  return {};
}

static const char *getenv_(const char *name)
{
  return ::getenv(name);
}

static bool scanPort_(ZuCSpan s, unsigned &port)
{
  if (!s.length()) return false;
  ZuBox<unsigned> box;
  if (box.scan(s) != s.length()) return false;
  unsigned v = box;
  if (!v) return false;
  if (v > 65535) return false;
  port = v;
  return true;
}

static ZuCSpan trimCRLF_(ZuCSpan s)
{
  while (s.length()) {
    char c = s[s.length() - 1];
    if (c != '\r' && c != '\n') break;
    s = ZuCSpan{s.data(), s.length() - 1};
  }
  return s;
}

static bool pathEscape_(ZuCSpan path)
{
  if (!path.length() || path[0] != '/') return true;
  ZuCSpan rest{path.data() + 1, path.length() - 1};
  while (rest) {
    ZuCSpan part = ZuTokenizer::Delimited<'/'>::next(rest);
    if (part == "." || part == "..") return true;
  }
  return false;
}

static PathStatus appendOutput_(ZuCSpan path, ZuCSpan downloads, ZtString<> &out)
{
  if (!path.length() || path[0] != '/') return PathEmpty;
  if (path[path.length() - 1] == '/') return PathDir;
  if (pathEscape_(path)) return PathEscape;
  out.length(0);
  out << downloads;
  if (downloads.length() && downloads[downloads.length() - 1] == '/')
    out << ZuCSpan{path.data() + 1, path.length() - 1};
  else
    out << path;
  return PathOK;
}

static ZiMxParams mxParams_()
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

static bool readFile_(ZuCSpan path, ZtString<> &data)
{
  ZiFile file;
  if (file.open(Zi::Path{path}, ZiFile::ReadOnly | ZiFile::NoFollow |
      ZiFile::GC) != Zi::OK)
    return false;
  ZiFile::Stat stat;
  if (file.fstat(stat) != Zi::OK || !stat.regular ||
      stat.size > 128 * 1024 * 1024)
    return false;
  data.length(unsigned(stat.size));
  int n = file.read(data.data(), data.length());
  return n == int(data.length());
}

static ZmSemaphore *sigDone_;

static void sigHandler_(int)
{
  if (sigDone_) sigDone_->post();
}

static void installSigHandlers_(
  ZmSemaphore *done, struct sigaction &oldInt, struct sigaction &oldTerm)
{
  sigDone_ = done;
  struct sigaction action{};
  action.sa_handler = sigHandler_;
  sigemptyset(&action.sa_mask);
  sigaction(SIGINT, &action, &oldInt);
  sigaction(SIGTERM, &action, &oldTerm);
}

static void restoreSigHandlers_(
  const struct sigaction &oldInt, const struct sigaction &oldTerm)
{
  sigaction(SIGINT, &oldInt, nullptr);
  sigaction(SIGTERM, &oldTerm, nullptr);
  sigDone_ = nullptr;
}

template <typename L>
static int consumeRx_(Zquic::RxStream &rx, L l)
{
  int n = 0;
  while (!rx.empty()) {
    int64_t r = rx.consume(
      [](ZuBSpan span) -> int64_t { return span.length(); },
      [&l](ZuBSpan span) { l(span); });
    if (r < 0) return -1;
    if (!r) break;
    n += int(r);
  }
  return n;
}

struct H3ReqOps {
  H3ReqOps(const Request &request_) : req{&request_} { }

  template <typename L>
  void operation(L &&l) const {
    l(Zhttp::Method::GET, [this](auto &&emit) {
      emit([this](auto &tx) { tx << req->path; });
    });
  }
  template <typename L> void host(L &&l) const { l(ZuCSpan{req->host}); }
  template <typename Key, typename L>
  void header(L &&l) const {
    if constexpr (Key{}() == "user-agent")
      l("zhttpqir/1.0");
    else if constexpr (Key{}() == "accept")
      l("*/*");
    else
      l("");
  }

  const Request	*req = nullptr;
};

template <typename H3Cxn_>
struct H3Request :
  public Zhttp::H3Request<H3Request<H3Cxn_>, H3ReqHeaders>,
  public H3ReqOps {
  using Base = Zhttp::H3Request<H3Request<H3Cxn_>, H3ReqHeaders>;
  using H3Cxn = H3Cxn_;

  H3Request(const Request &request_, H3Cxn &h3_, uint64_t streamID_) :
    H3ReqOps{request_}, h3{&h3_}, streamID_{streamID_} { }

  H3Cxn &h3Cxn() const { return *h3; }
  uint64_t streamID() const { return streamID_; }
  using H3ReqOps::header;
  using H3ReqOps::host;
  using H3ReqOps::operation;

  H3Cxn		*h3 = nullptr;
  uint64_t	streamID_ = 0;
};

ZuDerive(H3ReqPayload,
  (ZtArray<char, ZtArrayHeapID<"ZhttpQIR.H3ReqPayload">>));

struct H3ReqTx {
  H3ReqTx(H3ReqPayload &payload_) : payload{&payload_} { }

  H3ReqTx &operator <<(ZuBSpan span) {
    for (unsigned i = 0; i < span.length(); ++i)
      payload->push(char(span[i]));
    return *this;
  }
  H3ReqTx &operator <<(char c) {
    payload->push(c);
    return *this;
  }
  void flush() { }

  H3ReqPayload	*payload = nullptr;
};

template <typename StreamRef>
static bool sendH3Request_(const Request &request, StreamRef stream)
{
  using H3Cxn = ZuDecay<decltype(stream->link()->h3)>;
  H3Request<H3Cxn> builder{
    request, stream->link()->h3, uint64_t(stream->id())};
  H3ReqPayload payload;
  H3ReqTx tx{payload};
  builder.begin(tx);
  builder.finish(tx);
  return payload.length() &&
    stream->link()->send(stream,
      ZuCSpan{payload.data(), payload.length()}, true);
}

struct HQServerLink;
struct HQServerStream;
struct HQServer : public Zquic::Server<HQServer, HQServerLink> {
  using Link = HQServerLink;
  using Stream = HQServerStream;

  HQServer(const Env &env_) : env{env_} { }

  ZmRef<Link> accepted(const Zquic::InitialInfo &);
  ZiIP localIP() const { return ZiIP("0.0.0.0"); }
  unsigned localPort() const { return env.port; }
  void listenFailed(bool) {
    errors = 1;
    done.post();
  }

  Env			env;
  ZmSemaphore		done;
  ZmAtomic<unsigned>	errors = 0;
};

struct HQServerStream :
  public Zquic::SrvStream<HQServerLink, HQServerStream> {
  using Base = Zquic::SrvStream<HQServerLink, HQServerStream>;
  using Base::Base;

  int process(Zquic::RxStream &rx);

  ZtString<>	request;
  bool		done = false;
};

struct HQServerLink :
  public Zquic::SrvLink<HQServer, HQServerLink, HQServerStream> {
  using Base = Zquic::SrvLink<HQServer, HQServerLink, HQServerStream>;
  using Base::Base;

  HQServerLink(HQServer *app) : Base{app} { }

  void connected(Zquic::Connected info) {
    if (info.alpn != HQALPN) {
      app()->errors = 1;
      disconnect();
    }
  }
  void disconnected(bool) { }
  void streamed(ZmRef<Stream>) { }
};

ZmRef<HQServer::Link> HQServer::accepted(const Zquic::InitialInfo &)
{
  return new Link{this};
}

int HQServerStream::process(Zquic::RxStream &rx)
{
  if (done) return -1;
  int n = consumeRx_(rx, [this](ZuBSpan span) {
    request << span;
  });
  if (n < 0 || (n && !retireRx(unsigned(n)))) return -1;
  if (!this->rxComplete()) return n ? 1 : 0;

  done = true;
  HqRequest req;
  if (parseHQRequest(request, req) != PathOK) {
    this->link()->app()->errors = 1;
    this->link()->disconnect();
    return -1;
  }
  ZtString<> path;
  if (mapHQPath(req.path, this->link()->app()->env.www, path) != PathOK) {
    this->link()->app()->errors = 1;
    this->link()->disconnect();
    return -1;
  }
  ZtString<> data;
  if (!readFile_(path, data) ||
      !this->link()->send(
	ZmRef<HQServerStream>{this},
	ZuCSpan{data.data(), data.length()}, true)) {
    this->link()->app()->errors = 1;
    this->link()->disconnect();
    return -1;
  }
  return 1;
}

struct HQClient : public Zquic::Client<HQClient> {
  struct Link;
  struct Stream;

  HQClient(const Env &env_, Requests requests_) :
    env{env_}, requests{ZuMv(requests_)} { }

  void finish(bool ok) {
    if (!ok) errors = 1;
    if (++doneCount >= requests.length()) done.post();
  }
  void fail() {
    errors = 1;
    done.post();
  }

  Env			env;
  Requests		requests;
  ZmSemaphore		done;
  ZmAtomic<unsigned>	doneCount = 0;
  ZmAtomic<unsigned>	errors = 0;
};

struct HQClient::Stream :
  public Zquic::CliStream<HQClient::Link, HQClient::Stream> {
  using Base = Zquic::CliStream<HQClient::Link, HQClient::Stream>;
  using Base::Base;

  int process(Zquic::RxStream &rx);

  unsigned requestIndex = unsigned(-1);
  ZiFile file;
  bool done = false;
};

struct HQClient::Link :
  public Zquic::CliLink<HQClient, HQClient::Link, HQClient::Stream> {
  using Base = Zquic::CliLink<HQClient, HQClient::Link, HQClient::Stream>;
  using Base::Base;

  HQClient::Stream *streamFor(uint64_t streamID) {
    return findStream(int64_t(streamID));
  }

  void connected(Zquic::Connected info) {
    if (info.alpn != HQALPN) {
      app()->fail();
      return;
    }
    for (unsigned i = 0, n = app()->requests.length(); i < n; ++i) {
      auto stream = this->stream(Zquic::StreamType::Duplex);
      if (!stream) {
	app()->fail();
	return;
      }
      stream->requestIndex = i;
      if (stream->file.open(app()->requests[i].output,
	  ZiFile::Write | ZiFile::Create | ZiFile::Truncate | ZiFile::GC,
	  0666) != Zi::OK) {
	app()->fail();
	return;
      }
      ZtString<> line;
      if (!buildHQRequestLine(app()->requests[i].path, line) ||
	  !send(stream, ZuCSpan{line.data(), line.length()}, true)) {
	app()->fail();
	return;
      }
    }
  }
  void disconnected(bool) {
    if (app()->doneCount < app()->requests.length()) app()->fail();
  }
  void connectFailed(bool) { app()->fail(); }
  void streamed(ZmRef<Stream>) { }
};

int HQClient::Stream::process(Zquic::RxStream &rx)
{
  if (done) return -1;
  if (requestIndex >= this->link()->app()->requests.length()) {
    this->link()->app()->fail();
    return -1;
  }
  bool ok = true;
  int n = consumeRx_(rx, [this, &ok](ZuBSpan span) {
    if (span.length() &&
	file.write(span.data(), span.length()) != Zi::OK)
      ok = false;
  });
  if (n < 0 || (n && !retireRx(unsigned(n))) || !ok) {
    this->link()->app()->fail();
    return -1;
  }
  if (this->rxComplete()) {
    done = true;
    file.close();
    this->link()->app()->finish(true);
    return 1;
  }
  return n ? 1 : 0;
}

struct H3Client : public Zquic::Client<H3Client> {
  struct Link;
  struct Stream;

  H3Client(const Env &env_, Requests requests_) :
    env{env_}, requests{ZuMv(requests_)} { }

  void finish(bool ok) {
    if (!ok) errors = 1;
    if (++doneCount >= requests.length()) done.post();
  }
  void fail() {
    errors = 1;
    done.post();
  }
  uint64_t maxStreamsDuplex() const {
    return requests.length() > H3BidiMax ? requests.length() : H3BidiMax;
  }
  uint64_t maxStreamsSimplex() const { return H3UniMax; }

  Env			env;
  Requests		requests;
  ZmSemaphore		done;
  ZmAtomic<unsigned>	doneCount = 0;
  ZmAtomic<unsigned>	errors = 0;
};

struct H3ResponseParser :
  public Zhttp::H3ResponseParser<H3ResponseParser, H3RespHeaders> {
  using Base = Zhttp::H3ResponseParser<H3ResponseParser, H3RespHeaders>;
  using State = typename Base::State;

  H3ResponseParser() : Base{H3RespBodyMax} { }

  void bind(H3Client::Stream *stream_) { stream = stream_; }
  Zhttp::H3::QPackRxTable *qpackRx() const;
  bool qpackDecoderWrite(ZuBSpan span) const;
  uint64_t streamID() const;
  void status(unsigned status) { status_ = status; }
  template <typename Rx>
  bool body(Rx &rx);
  void complete(State::T state);
  void bodyInfo(Zhttp::BodyType::T, uint64_t) { }
  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuBSpan) { }

  H3Client::Stream	*stream = nullptr;
  unsigned		status_ = 0;
};

struct H3Client::Stream :
  public Zquic::CliStream<H3Client::Link, H3Client::Stream>,
  public Zhttp::H3::CxnParser<H3Client::Stream> {
  using Base = Zquic::CliStream<H3Client::Link, H3Client::Stream>;
  using CxnParser = Zhttp::H3::CxnParser<H3Client::Stream>;
  using Base::Base;

  int process(Zquic::RxStream &rx);
  Zhttp::H3::CxnState::T h3State() const;
  void h3State(Zhttp::H3::CxnState::T);
  bool peerControlStream();
  bool peerEncoderStream();
  bool peerDecoderStream();
  Zhttp::H3::QPackRxTable *qpackRx();
  bool qpackTxInsn(Zhttp::H3::QPackInsn::T, uint64_t);
  bool qpackTxMaxCapacity(uint64_t);
  void complete(bool ok);

  H3ResponseParser	parser;
  unsigned	requestIndex = unsigned(-1);
  ZiFile	file;
  bool		done = false;
};

struct H3Client::Link :
  public Zquic::CliLink<H3Client, H3Client::Link, H3Client::Stream> {
  using Base = Zquic::CliLink<H3Client, H3Client::Link, H3Client::Stream>;
  using H3Cxn = Zhttp::H3::Cxn<Link, ZmRef<Stream>>;
  using Base::Base;

  H3Client::Stream *streamFor(uint64_t streamID) {
    return findStream(int64_t(streamID));
  }

  void connected(Zquic::Connected info) {
    if (info.alpn != H3ALPN) {
      app()->fail();
      return;
    }
    h3.link_ = this;
    ZiTxErrorFn txError = h3.txError;
    unsigned requests = app()->requests.length();
    auto link = ZmMkRef(this);
    app()->txInvoke([link, txError = ZuMv(txError), requests]() mutable {
      auto limits = Zhttp::H3::Params{}.qpackLimits();
      bool ok = link->h3Tx.init(limits.txCapacity, limits.txSections);
      typename H3Cxn::LocalStreams streams;
      if (ok) streams = H3Cxn::openLocalStreams(*link, txError);
      ok = ok && bool(streams);
      link->app()->rxInvoke([
	link, streams = ZuMv(streams), ok
      ]() mutable {
	if (!ok || !link->h3.openLocal(*link, ZuMv(streams))) {
	  link->app()->fail();
	}
      });
      if (!ok) return;
      for (unsigned i = 0; i < requests; ++i) {
	auto stream = link->stream(Zquic::StreamType::Duplex);
	link->app()->rxInvoke([
	  link, stream = ZuMv(stream), i
	]() mutable { link->opened(i, ZuMv(stream)); });
      }
    });
  }
  void opened(unsigned i, ZmRef<Stream> stream) {
    if (!h3.localOpen) return;
    if (!stream) {
      app()->fail();
      return;
    }
    stream->requestIndex = i;
    stream->parser.bind(stream);
    stream->parser.reset();
    if (stream->file.open(app()->requests[i].output,
	ZiFile::Write | ZiFile::Create | ZiFile::Truncate | ZiFile::GC,
	0666) != Zi::OK) {
      app()->fail();
      return;
    }
    if (!sendH3Request_(app()->requests[i], stream)) {
      app()->fail();
      return;
    }
  }
  void disconnected(bool) {
    if (app()->doneCount < app()->requests.length()) app()->fail();
  }
  void connectFailed(bool) { app()->fail(); }
  void streamed(ZmRef<Stream>) { }

  Zhttp::H3::QPackTxTable *qpackTx() { return &h3Tx; }

  // Rx thread exclusive
  alignas(Zm::CacheLineSize)
  H3Cxn				h3;

  // Tx thread exclusive
  alignas(Zm::CacheLineSize)
  Zhttp::H3::QPackTxTable	h3Tx;
};

Zhttp::H3::QPackRxTable *H3ResponseParser::qpackRx() const
{
  return stream ? &stream->link()->h3.qpackRxTable : nullptr;
}

bool H3ResponseParser::qpackDecoderWrite(ZuBSpan span) const
{
  return stream && stream->link()->h3.qpackDecoderWrite(span);
}

uint64_t H3ResponseParser::streamID() const
{
  return stream ? uint64_t(stream->id()) : 0;
}

template <typename Rx>
bool H3ResponseParser::body(Rx &rx)
{
  bool accepted = true;
  bool consumed = Zhttp::bodyEach(rx, [this, &accepted](ZuBSpan body) {
    if (!stream || !body.length()) return;
    if (stream->file.write(body.data(), body.length()) != Zi::OK) {
      accepted = false;
      stream->complete(false);
    }
  });
  return consumed && accepted;
}

void H3ResponseParser::complete(State::T state)
{
  if (!stream) return;
  bool ok = state == State::Complete && status_ >= 200 && status_ < 300;
  stream->complete(ok);
}

int H3Client::Stream::process(Zquic::RxStream &rx)
{
  if (Zquic::StreamID::uni(uint64_t(this->id()))) {
    auto s = this->CxnParser::process(*this);
    if (s == Zhttp::H3::CxnState::Error) {
      this->link()->app()->fail();
      return -1;
    }
    (void)rx;
    return 0;
  }
  auto s = parser.process(*this);
  if (s == H3ResponseParser::State::Error ||
      s == H3ResponseParser::State::Cancelled) {
    complete(false);
    return -1;
  }
  if (s == H3ResponseParser::State::Complete) return 1;
  return done ? -1 : 0;
}

Zhttp::H3::CxnState::T H3Client::Stream::h3State() const
{
  return this->link()->h3.state;
}

void H3Client::Stream::h3State(Zhttp::H3::CxnState::T state)
{
  this->link()->h3.state = state;
}

bool H3Client::Stream::peerControlStream()
{
  return this->link()->h3.peerControlStream();
}

bool H3Client::Stream::peerEncoderStream()
{
  return this->link()->h3.peerEncoderStream();
}

bool H3Client::Stream::peerDecoderStream()
{
  return this->link()->h3.peerDecoderStream();
}

Zhttp::H3::QPackRxTable *H3Client::Stream::qpackRx()
{
  return this->link()->h3.qpackRx();
}

bool H3Client::Stream::qpackTxInsn(
  Zhttp::H3::QPackInsn::T type, uint64_t value)
{
  return this->link()->h3.qpackTxInsn(type, value);
}

bool H3Client::Stream::qpackTxMaxCapacity(uint64_t capacity)
{
  return this->link()->h3.qpackTxMaxCapacity(capacity);
}

void H3Client::Stream::complete(bool ok)
{
  if (done) return;
  done = true;
  file.close();
  this->link()->app()->finish(ok);
}

ZuCSpan roleName(Role role)
{
  switch (role) {
    case Client: return "client";
    case Server: return "server";
  }
  return {};
}

bool parseRole(ZuCSpan s, Role &role)
{
  static constexpr auto matcher = ZuMatcher<"client", "server">();
  int i = matcher.exact(s);
  if (i < 0) return false;
  role = Role(i);
  return true;
}

ZuCSpan caseName(Case testCase)
{
  switch (testCase) {
    case Handshake: return "handshake";
    case Transfer: return "transfer";
    case HTTP3: return "http3";
    case RebindPort: return "rebind-port";
    case RebindAddr: return "rebind-addr";
    case ConnectionMigration: return "connectionmigration";
    case Unsupported: return "unsupported";
  }
  return {};
}

Case parseCase(ZuCSpan s)
{
  static constexpr auto matcher = ZuMatcher<
    "handshake", "transfer", "http3", "rebind-port", "rebind-addr",
    "connectionmigration">();
  int i = matcher.exact(s);
  return i < 0 ? Unsupported : Case(i);
}

bool caseSupported(Case testCase)
{
  switch (testCase) {
    case Handshake:
    case Transfer:
    case HTTP3:
    case RebindPort:
    case RebindAddr:
      return true;
    case ConnectionMigration:
    case Unsupported:
      return false;
  }
  return false;
}

bool caseH3(Case testCase)
{
  return testCase == HTTP3;
}

bool caseHQ(Case testCase)
{
  switch (testCase) {
    case Handshake:
    case Transfer:
    case RebindPort:
    case RebindAddr:
      return true;
    case HTTP3:
    case ConnectionMigration:
    case Unsupported:
      return false;
  }
  return false;
}

int loadEnv(Role role, Env &env, const char *(*getenvFn)(const char *))
{
  ZuCSpan testCase = env_(getenvFn, "TESTCASE");
  if (!testCase.length()) return Usage;
  env.testCase = parseCase(testCase);
  if (!caseSupported(env.testCase)) return UnsupportedStatus;
  env.heartBeat = qirHeartBeat(env.testCase);

  setString_(env.requests, env_(getenvFn, "REQUESTS"));
  setString_(env.keyLog, env_(getenvFn, "SSLKEYLOGFILE"));

  ZuCSpan www = env_(getenvFn, "ZHTTP_QIR_WWW");
  setString_(env.www, www.length() ? www : DefaultWWW);
  ZuCSpan downloads = env_(getenvFn, "ZHTTP_QIR_DOWNLOADS");
  setString_(env.downloads, downloads.length() ? downloads : DefaultDownloads);
  ZuCSpan ca = env_(getenvFn, "ZHTTP_QIR_CA");
  setString_(env.ca, ca.length() ? ca : DefaultCA);
  ZuCSpan cert = env_(getenvFn, "ZHTTP_QIR_CERT");
  setString_(env.cert, cert.length() ? cert : DefaultCert);
  ZuCSpan key = env_(getenvFn, "ZHTTP_QIR_KEY");
  setString_(env.key, key.length() ? key : DefaultKey);
  setQLogPath_(env.qlogPath, env_(getenvFn, "QLOGDIR"), role);

  env.port = DefaultPort;
  ZuCSpan port = env_(getenvFn, "ZHTTP_QIR_PORT");
  if (port.length() && !scanPort_(port, env.port)) return Usage;
  if (role == Client && !env.requests.length()) return Usage;

  return OK;
}

int parseRequests(ZuCSpan requests, ZuCSpan downloads, Requests &out)
{
  out.length(0);
  requests.trim();
  while (requests) {
    ZuCSpan url = ZuTokenizer::WhiteSpace::next(requests);
    if (!url.length()) break;
    Request request;
    if (mapURL(url, downloads, request) != PathOK)
      return Usage;
    out.push(request);
  }
  return out.length() ? OK : Usage;
}

PathStatus mapURL(ZuCSpan url, ZuCSpan downloads, Request &request)
{
  if (!url.match(Scheme)) return PathBadScheme;
  setString_(request.url, url);

  unsigned i = Scheme.length();
  unsigned hostStart = i;
  while (i < url.length() && url[i] != '/' && url[i] != ':' &&
      url[i] != '?' && url[i] != '#')
    ++i;
  if (i <= hostStart) return PathEmpty;
  setString_(request.host, ZuCSpan{url.data() + hostStart, i - hostStart});
  request.port = DefaultPort;
  if (i < url.length() && url[i] == ':') {
    unsigned portStart = ++i;
    while (i < url.length() && url[i] >= '0' && url[i] <= '9') ++i;
    if (i <= portStart ||
	!scanPort_(ZuCSpan{url.data() + portStart, i - portStart},
	  request.port))
      return PathEmpty;
  }
  if (i >= url.length()) return PathEmpty;
  if (url[i] != '/') return PathEmpty;
  unsigned pathStart = i;
  while (i < url.length() && url[i] != '?' && url[i] != '#') ++i;
  ZuCSpan path{url.data() + pathStart, i - pathStart};
  if (!path.length() || path == "/") return PathEmpty;
  setString_(request.path, path);
  return appendOutput_(path, downloads, request.output);
}

PathStatus parseHQRequest(ZuCSpan line, HqRequest &request)
{
  line = trimCRLF_(line);
  if (!line.match(HQGet)) return PathEscape;
  ZuCSpan path{line.data() + HQGet.length(), line.length() - HQGet.length()};
  if (!path.length()) return PathEmpty;
  if (path[path.length() - 1] == '/') return PathDir;
  if (pathEscape_(path)) return PathEscape;
  setString_(request.path, path);
  return PathOK;
}

PathStatus mapHQPath(ZuCSpan path, ZuCSpan www, ZtString<> &file)
{
  if (!path.length() || path[0] != '/') return PathEmpty;
  if (path[path.length() - 1] == '/') return PathDir;
  if (pathEscape_(path)) return PathEscape;
  file.length(0);
  file << www;
  if (www.length() && www[www.length() - 1] == '/')
    file << ZuCSpan{path.data() + 1, path.length() - 1};
  else
    file << path;
  return PathOK;
}

bool buildHQRequestLine(ZuCSpan path, ZtString<> &line)
{
  if (!path.length() || path[0] != '/' || path[path.length() - 1] == '/' ||
      pathEscape_(path))
    return false;
  line.length(0);
  line << "GET " << path << "\r\n";
  return true;
}

bool ensureParentDirs(ZuCSpan path)
{
  Zi::Path dir = ZiFile::dirname(Zi::Path{path});
  if (!dir || dir == "." || ZiStat{dir}.isdir()) return true;

  ZtArray<Zi::Path> stack;
  Zi::Path cur{dir};
  while (cur && cur != "." && !ZiStat{cur}.isdir()) {
    stack.push(cur);
    Zi::Path parent = ZiFile::dirname(cur);
    if (parent == cur) break;
    cur = ZuMv(parent);
  }
  for (int i = int(stack.length()); --i >= 0; )
    if (!ZiStat{stack[i]}.isdir() && ZiFile::mkdir(stack[i]) != Zi::OK)
      return false;
  return ZiStat{dir}.isdir();
}

static int runHQServer_(const Env &env)
{
  ZiLog::init("zhttpqir");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();

  ZiMultiplex mx{mxParams_()};
  if (!mx.start()) {
    ZiLog::stop();
    return Error;
  }

  HQServer server{env};
  struct sigaction oldInt{};
  struct sigaction oldTerm{};
  installSigHandlers_(&server.done, oldInt, oldTerm);
  int rc = Error;
  if (server.init(
	qirParams_(Zquic::ServerParams(&mx, "3", "4")
	  .certPath(env.cert).keyPath(env.key)
	  .alpn(ZuSpan<ZuCSpan>{HQALPN})
	  .keyLogPath(env.keyLog)
	  .qlog(bool(env.qlogPath))
	  .qlogPath(env.qlogPath), env)) &&
      server.start()) {
    server.done.wait();
    rc = server.errors ? Error : OK;
    server.stop();
  }
  server.final();
  restoreSigHandlers_(oldInt, oldTerm);
  mx.stop();
  ZiLog::stop();
  return rc;
}

static int runHQClient_(const Env &env, Requests requests)
{
  if (!requests.length()) return Usage;

  ZiLog::init("zhttpqir");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();

  ZiMultiplex mx{mxParams_()};
  if (!mx.start()) {
    ZiLog::stop();
    return Error;
  }

  HQClient client{env, ZuMv(requests)};
  int rc = Error;
  if (client.init(
	qirParams_(Zquic::ClientParams(&mx, "3", "4")
	  .caPath(env.ca)
	  .alpn(ZuSpan<ZuCSpan>{HQALPN})
	  .keyLogPath(env.keyLog)
	  .qlog(bool(env.qlogPath))
	  .qlogPath(env.qlogPath), env))) {
    ZmRef<HQClient::Link> link = new HQClient::Link{&client};
    link->connect(Zquic::Host{client.requests[0].host}, client.requests[0].port);
    if (client.done.timedwait(Zm::now(timeouts().total)) != 0)
      client.errors = 1;
    ZmBlock<>{}([link = ZuMv(link)](auto wake) mutable {
      link->disconnect([wake = ZuMv(wake)]() mutable { wake(); });
    });
    rc = client.errors ? Error : OK;
  }
  client.final();
  mx.stop();
  ZiLog::stop();
  return rc;
}

static bool sameAuthority_(const Requests &requests)
{
  if (!requests.length()) return false;
  ZuCSpan host{requests[0].host};
  unsigned port = requests[0].port;
  for (unsigned i = 1, n = requests.length(); i < n; ++i)
    if (ZuCSpan{requests[i].host} != host || requests[i].port != port)
      return false;
  return true;
}

static int runH3Client_(const Env &env, Requests requests)
{
  if (!sameAuthority_(requests)) return Usage;

  ZiLog::init("zhttpqir");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();

  ZiMultiplex mx{mxParams_()};
  if (!mx.start()) {
    ZiLog::stop();
    return Error;
  }

  H3Client client{env, ZuMv(requests)};
  int rc = Error;
  if (client.init(
	qirParams_(Zquic::ClientParams(&mx, "3", "4")
	  .caPath(env.ca)
	  .alpn(ZuSpan<ZuCSpan>{H3ALPN})
	  .keyLogPath(env.keyLog)
	  .qlog(bool(env.qlogPath))
	  .qlogPath(env.qlogPath)
	  .maxData(H3DataMax)
	  .maxStreamData(H3StreamDataMax)
	  .maxStreamsDuplex(client.maxStreamsDuplex())
	  .maxStreamsSimplex(H3UniMax), env))) {
    ZmRef<H3Client::Link> link = new H3Client::Link{&client};
    link->connect(Zquic::Host{client.requests[0].host}, client.requests[0].port);
    if (client.done.timedwait(Zm::now(timeouts().total)) != 0)
      client.errors = 1;
    ZmBlock<>{}([link = ZuMv(link)](auto wake) mutable {
      link->disconnect([wake = ZuMv(wake)]() mutable { wake(); });
    });
    rc = client.errors ? Error : OK;
  }
  client.final();
  mx.stop();
  ZiLog::stop();
  return rc;
}

static int runH3Server_(const Env &env)
{
  Zhttpd::Options options;
  options.root = env.www;
  options.addr = "0.0.0.0";
  options.port = env.port;
  options.cert = env.cert;
  options.key = env.key;
  options.keyLog = env.keyLog;
  options.logPath = "/dev/null";
  options.quicMigration = qirMigrationModeArg();
  options.quicMigrationCIDReserve = qirMigrationCIDReserve();
  options.http = false;
  options.http3 = true;
  options.noListing = true;
  options.noServerID = true;

  ZeString error;
  if (!Zhttpd::validate(options, error)) return Usage;

  ZiLog::init("zhttpqir");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();

  ZmSemaphore done;
  struct sigaction oldInt{};
  struct sigaction oldTerm{};
  installSigHandlers_(&done, oldInt, oldTerm);

  Zhttpd::Application app;
  int rc = Error;
  if (app.init(ZuMv(options), error)) {
    app.done(ZmFn<void(), ZmFnHeapID<"zhttpd.Done">>{
      [&done]() { done.post(); }});
    if (app.startMultiplex() && app.initServer() && app.startServer()) {
      done.wait();
      if (app.stopServer() && !app.errors()) rc = OK;
    }
    app.finalServer();
    app.stopMultiplex();
  }
  app.final();
  restoreSigHandlers_(oldInt, oldTerm);
  ZiLog::stop();
  return rc;
}

int run(Role role)
{
  Env env;
  int status = loadEnv(role, env, getenv_);
  if (status != OK) return status;
  if (role == Client) {
    Requests requests;
    status = parseRequests(env.requests, env.downloads, requests);
    if (status != OK) return status;
    for (unsigned i = 0, n = requests.length(); i < n; ++i)
      if (!ensureParentDirs(requests[i].output)) return Error;
    if (!ZiStat{env.ca}.exists()) return Usage;
    if (caseH3(env.testCase)) return runH3Client_(env, ZuMv(requests));
    if (caseHQ(env.testCase)) return runHQClient_(env, ZuMv(requests));
  } else {
    if (!ZiStat{env.www}.isdir() || !ZiStat{env.cert}.exists() ||
	!ZiStat{env.key}.exists())
      return Usage;
    if (caseH3(env.testCase)) return runH3Server_(env);
    if (caseHQ(env.testCase)) return runHQServer_(env);
  }
  return Error;
}

void usage()
{
  fputs("usage: zhttpqir server|client\n", stdout);
}

} // namespace Zhttp::QIR
