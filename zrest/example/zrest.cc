//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// REST/HTTP Ping client

#include <iostream>
#include <math.h>

#include <zlib/ZmBlock.hh>
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

#include "zrestproto.hh"

ZtEnumNS(, Http3Mode, int8_t, force, prefer, disable);
ZtEnumNS(, Http2Mode, int8_t, force, prefer, disable);

ZtEnumImplNS(Http3Mode);
ZtEnumImplNS(Http2Mode);

enum { ClientTimeout = 15, H3StallTimeout = 15, H3QuietTimeout = 2,
  MaxRedirects = 8, WorkBatch = 64 };

enum ClientState { Idle, Authenticating, Ready, Refreshing, Complete, Failed };

struct Options {
  ZuCSpan	ca;
  ZuCSpan	user{DefaultUser{}()};
  ZuCSpan	pass{DefaultPass{}()};
  unsigned	requests = 1;
  unsigned	concurrency = 1;
  unsigned	links = 1;
  unsigned	linkMax = 1;
  unsigned	retries = 0;
  unsigned	timeout = ClientTimeout;
  unsigned	stallTimeout = H3StallTimeout;
  unsigned	quietTimeout = H3QuietTimeout;
  double	interval = 0;
  ZuTime	intervalTime;
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
  (((user),      (CLI::Long<"user">)),                       (String, "test")),
  (((pass),      (CLI::Long<"pass">)),                       (String, "test123")),
  (((requests),  (CLI::Opt<'n'>,  CLI::Long<"requests">)),   (UInt32, 1)),
  (((concurrency), (CLI::Opt<'j'>, CLI::Long<"jobs">)),       (UInt32, 1)),
  (((links),      (CLI::Long<"links">)),                     (UInt32, 1)),
  (((linkMax),   (CLI::Long<"link-max">)),                  (UInt32, 1)),
  (((retries),   (CLI::Long<"retries">)),                    (UInt32, 0)),
  (((timeout),   (CLI::Long<"timeout">)),                    (UInt32, ClientTimeout)),
  (((stallTimeout),
    (CLI::Long<"stall-timeout">)),                            (UInt32, H3StallTimeout)),
  (((quietTimeout),
    (CLI::Long<"quiet-timeout">)),                            (UInt32, H3QuietTimeout)),
  (((interval),  (CLI::Opt<'i'>, CLI::Long<"interval">)),    (Float, 0)),
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
    "  --user=USER         authentication username, default test\n"
    "  --pass=PASS         authentication password, default test123\n"
    "  -n, --requests=N    submit N Ping requests, default 1\n"
    "  -j, --jobs=M        run up to M requests concurrently, default 1\n"
    "  --links=N           persistent links in pool 0, default 1\n"
    "  --link-max=N\n"
    "                      H1 pipeline/H2-H3 stream limit per link, default 1\n"
    "  --retries=N         retry transient connection failures N times\n"
    "  --timeout=N         completion timeout in seconds, default 15, 0 disables\n"
    "  --stall-timeout=N   no-progress stall timeout in seconds, default 15\n"
    "  --quiet-timeout=N   quiet transport timeout in seconds, default 2\n"
    "  -i, --interval=N    non-negative seconds between ping waves; fractional\n"
    "                      values such as 0.01 use the Tx scheduler\n"
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

static bool validateOptions(Options &options, int argc)
{
  if (argc < 1 || argc > 2 || !options.requests || !options.concurrency ||
      !options.links || !options.linkMax || !options.user ||
      !options.pass || !::isfinite(options.interval) || options.interval < 0)
    return false;
  options.intervalTime = ZuTime{options.interval};
  if (options.interval > 0 &&
      (!*options.intervalTime || !options.intervalTime)) return false;
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

class Client;

struct TokenState : public RefreshRequest {
  TokenString bearer;
  int64_t deadline = 0;
};

struct AuthReq : public Credentials {
  Client *client = nullptr;
  template <typename Link, typename Response>
  void process(Link *, const Response *) const;
  template <typename Link> void failed(Link *) const;
};

struct RefreshReq : public ExampleObject, public ZmObject {
  Client *client = nullptr;
  ZmRef<const TokenState> state;
  template <typename Link, typename Response>
  void process(Link *, const Response *) const;
  template <typename Link> void failed(Link *) const;
};

struct PingReq : public Ping {
  Client *client = nullptr;
  ZmRef<const TokenState> state;
  uint64_t logicalID = 0;
  bool replayed = false;
  template <typename Link, typename Response>
  void process(Link *, const Response *) const;
  template <typename Link> void failed(Link *) const;
};

struct AuthTokensParser;
struct AuthUnauthorizedParser;
struct AuthInternalParser;
struct RefreshTokensParser;
struct RefreshUnauthorizedParser;
struct RefreshInternalParser;
struct PongParser;
struct PingUnauthorizedParser;

struct AuthBuilder : public Zrest::ReqBuilder<AuthBuilder, AuthReq> {
  enum { Method = Zhttp::Method::POST, Body = Zrest::BodyPolicy::JSON };
  using Path = AuthPath;
  using Responses = ZuTypeList<
    AuthTokensParser, AuthUnauthorizedParser, AuthInternalParser>;
  const Credentials &bodyObject(const AuthReq *request) const {
    return *request;
  }
};
struct RefreshBuilder : public Zrest::ReqBuilder<RefreshBuilder, RefreshReq> {
  enum { Method = Zhttp::Method::POST, Body = Zrest::BodyPolicy::JSON };
  using Path = RefreshPath;
  using Responses = ZuTypeList<
    RefreshTokensParser, RefreshUnauthorizedParser, RefreshInternalParser>;
  const RefreshRequest &bodyObject(const RefreshReq *request) const {
    return *request->state;
  }
};
struct PingBuilder : public Zrest::ReqBuilder<PingBuilder, PingReq> {
  using Base = Zrest::ReqBuilder<PingBuilder, PingReq>;
  using Base::header;
  enum { Query = Zrest::QueryPolicy::URI };
  using Path = PingPath;
  using Headers = ZhttpHeaders("authorization");
  using Responses = ZuTypeList<PongParser, PingUnauthorizedParser>;
  const Ping &queryObject(const PingReq *request) const { return *request; }
  template <typename Key, typename L> void header(L &&l) const {
    if constexpr (Key{}() == "authorization") l(object->state->bearer);
    else Base::template header<Key>(ZuFwd<L>(l));
  }
};

struct AuthTokensParser : public Zrest::ResParser<
    AuthTokensParser, TokenResponse> {
  enum { Body = Zrest::BodyPolicy::JSON };
};
struct AuthUnauthorizedParser : public Zrest::ResParser<
    AuthUnauthorizedParser, Unauthorized> {
  enum { Status = 401, Body = Zrest::BodyPolicy::Zero };
};
struct AuthInternalParser : public Zrest::ResParser<
    AuthInternalParser, InternalError> {
  enum { Status = 500, Body = Zrest::BodyPolicy::Zero };
};
struct RefreshTokensParser : public Zrest::ResParser<
    RefreshTokensParser, TokenResponse> {
  enum { Body = Zrest::BodyPolicy::JSON };
};
struct RefreshUnauthorizedParser : public Zrest::ResParser<
    RefreshUnauthorizedParser, Unauthorized> {
  enum { Status = 401, Body = Zrest::BodyPolicy::Zero };
};
struct RefreshInternalParser : public Zrest::ResParser<
    RefreshInternalParser, InternalError> {
  enum { Status = 500, Body = Zrest::BodyPolicy::Zero };
};
struct PongParser : public Zrest::ResParser<PongParser, Pong> {
  enum { Body = Zrest::BodyPolicy::JSON };
};
struct PingUnauthorizedParser : public Zrest::ResParser<
    PingUnauthorizedParser, Unauthorized> {
  enum { Status = 401, Body = Zrest::BodyPolicy::Zero };
};

using Requests = ZuTypeList<AuthBuilder, RefreshBuilder, PingBuilder>;

struct ReqBuilder_ : public ZmObject, public Zrest::MReqBuilder<Requests> {
  using Base = Zrest::MReqBuilder<Requests>;
  using Reqs = Requests;
  using Base::init;

  uint64_t id = 0;

  uint64_t key() const { return id; }
  uint64_t length() const { return 1; }
};

struct ResParser : public Zrest::MResParser<ReqBuilder_> { };

class Pool;
ZuDerive(ReqBuilderQ, (ZmPQueue<ReqBuilder_,
  ZmPQueueOverlap<false,
    ZmPQueueNode<ReqBuilder_,
      ZmPQueueHeapID<"zrest.ReqBuilder">>>>));
using ReqBuilder = ReqBuilderQ::Node;
ZuDerive(TxQ, (ZmPQTx<Pool, ReqBuilderQ, ZmPQTxOrdered<false>>));

class Pool : public Zhttp::Pool<Client, TxQ, ResParser> {
  using Base = Zhttp::Pool<Client, TxQ, ResParser>;

public:
  Pool(Client *client) : Base{client} { }
  ReqBuilderQ *txQueue() { return &m_requests; }
  void archive_(ReqBuilder *);
  ZmRef<ReqBuilder> retrieve_(ReqBuilderQ::Key, ReqBuilderQ::Key) { return {}; }

private:
  ReqBuilderQ m_requests;
};

struct Pending {
  uint64_t logicalID = 0;
  bool replayed = false;
};
ZuDerive(PendingQ, (ZtArray<Pending, ZtArrayHeapID<"zrest.Pending">>));

class Client : public Zhttp::Client<Client, Pool> {
public:
  void idle() {
    terminalReady_();
  }

  void workload(const Options &options, ZiMultiplex *mx) {
    txRun(0, [this, options = &options, mx]() {
      workload_(options, mx);
    });
  }

  void quiesce() {
    ZmBlock<>{}([this](auto wake) mutable {
      txRun(0, [this, wake = ZuMv(wake)]() mutable {
	m_stopping = true;
	cancelTimer_();
	quiesceClean_(ZuMv(wake));
      });
    });
  }

  unsigned logicalCompleted() const { return m_completed; }
  unsigned logicalFailed() const { return m_logicalFailed; }

  void tokens(const TokenResponse *tokens, bool refresh) {
    ZmRef<TokenState> state;
    auto receipt = Zm::now();
    if (tokens && tokens->accessToken && tokens->refreshToken &&
	tokens->accessToken.length() <= JWTMax &&
	tokens->refreshToken.length() <= JWTMax && tokens->expiresIn &&
	receipt.sec() > 0 &&
	tokens->expiresIn <= uint64_t(INT64_MAX - receipt.sec())) {
      state = new TokenState{};
      state->refreshToken = tokens->refreshToken;
      state->bearer << "Bearer " << tokens->accessToken;
      state->deadline = receipt.sec() + int64_t(tokens->expiresIn);
    }
    txRun(0, [this, state = ZuMv(state), refresh]() mutable {
      retire_();
      if (m_state == Failed || m_state == Complete) return;
      if (!state) { fail_(); return; }
      m_tokens = ZuMv(state).constRef();
      m_state = Ready;
      if (m_options->verbose) {
        auto now = Zm::now();
        uint64_t ns = uint64_t(now.sec()) * 1000000000ULL + now.nsec();
        ZiLOG(Info, "zrest", ([refresh, ns](auto &s) {
          s << "event=" << (refresh ? "refresh" : "auth") <<
	    " time_ns=" << ns;
        }));
      }
      if (!refresh && m_interval) {
	m_next = Zm::now() + m_interval;
	armTimer_();
      } else drive_();
    });
  }

  void unauthorized(bool ping, uint64_t logicalID, bool replayed,
      ZmRef<const TokenState> state) {
    txRun(0, [this, ping, logicalID, replayed, state = ZuMv(state)]() mutable {
      retire_();
      if (m_state == Failed || m_state == Complete) return;
      if (!ping || replayed) { fail_(); return; }
      m_pending.unshift(Pending{logicalID, true});
      if (state.ptr() == m_tokens.ptr()) refresh_();
      else drive_();
    });
  }

  void pong(uint64_t logicalID, bool value) {
    txRun(0, [this, logicalID, value]() {
      retire_();
      if (m_state == Failed || m_state == Complete) return;
      if (!value) { fail_(); return; }
      ++m_completed;
      if (m_options->verbose)
	ZiLOG(Info, "zrest", ([logicalID](auto &s) {
	  s << "event=pong id=" << logicalID;
	}));
      drive_();
    });
  }

  void requestFailed() {
    txRun(0, [this]() {
      retire_();
      if (m_state == Failed || m_state == Complete) terminalReady_();
      else fail_();
    });
  }

private:
  void workload_(const Options *options, ZiMultiplex *mx) {
    m_options = options;
    m_mx = mx;
    m_interval = options->intervalTime;
    m_txSID = ZmSelf()->sid();
    ZiAssert(ZmSelf()->sid() == m_txSID, "zrest", (),
	"workload outside Tx shard", return);
    authenticate_();
  }

  void authenticate_() {
    m_state = Authenticating;
    auto request = new AuthReq{};
    request->client = this;
    request->username = m_options->user;
    request->password = m_options->pass;
    send_<AuthBuilder>(request);
  }

  void refresh_() {
    if (m_state == Refreshing) return;
    if (!m_tokens) { fail_(); return; }
    m_state = Refreshing;
    auto request = new RefreshReq{};
    request->client = this;
    request->state = m_tokens;
    send_<RefreshBuilder>(request);
  }

  template <typename Builder, typename Request>
  void send_(Request *object) {
    ZmRef<ReqBuilder> request = new ReqBuilder{};
    request->id = m_attempt++;
    request->template init<Builder>(object);
    ++m_active;
    send(0, ZuMv(request));
  }

  void ping_(Pending pending, ZmRef<const TokenState> state) {
    auto request = new PingReq{};
    request->client = this;
    request->state = ZuMv(state);
    request->logicalID = pending.logicalID;
    request->replayed = pending.replayed;
    request->ping = true;
    send_<PingBuilder>(request);
  }

  void retire_() {
    ZmAssert(m_active);
    --m_active;
  }

  void drive_() {
    ZiAssert(ZmSelf()->sid() == m_txSID, "zrest", (),
	"state transition outside Tx shard", return);
    if (m_state == Failed || m_state == Complete || m_state == Authenticating ||
	m_state == Refreshing || !m_options) return;
    if (m_waveRemaining) return;
    if (m_pending) {
      dispatchPending_();
      return;
    }
    if (m_completed == m_options->requests && !m_active) {
      m_state = Complete;
      cancelTimer_();
      if (m_options->verbose)
	ZiLOG(Info, "zrest", ([completed = m_completed](auto &s) {
	  s << "event=summary completed=" << completed << " failed=0";
	}));
      seal(0);
      return;
    }
    if (m_generated >= m_options->requests) return;
    if (!m_interval) {
      unsigned n = m_options->concurrency - m_active;
      unsigned left = m_options->requests - m_generated;
      if (n > left) n = left;
      if (!n) return;
      auto now = Zm::now();
      if (m_tokens->deadline <= now.sec()) { refresh_(); return; }
      startWave_(n, false);
      return;
    }
    auto now = Zm::now();
    if (m_next > now) { armTimer_(); return; }
    unsigned wave = m_options->requests - m_generated;
    if (wave > m_options->concurrency) wave = m_options->concurrency;
    if (m_options->concurrency - m_active < wave) return;
    if (m_tokens->deadline <= now.sec()) { refresh_(); return; }
    startWave_(wave, true);
  }

  void startWave_(unsigned count, bool paced) {
    m_waveState = m_tokens;
    m_waveRemaining = count;
    m_waveCount = count;
    m_wavePaced = paced;
    issueWave_();
  }

  void issueWave_() {
    if (m_state != Ready || !m_waveRemaining) return;
    unsigned n = m_waveRemaining;
    if (n > WorkBatch) n = WorkBatch;
    m_waveRemaining -= n;
    while (n--)
      ping_(Pending{m_generated++, false}, m_waveState);
    if (m_waveRemaining) {
      txRun(0, [this]() { issueWave_(); });
      return;
    }
    unsigned count = m_waveCount;
    bool paced = m_wavePaced;
    m_waveState = nullptr;
    m_waveCount = 0;
    m_wavePaced = false;
    if (!paced) { drive_(); return; }
    auto now = Zm::now();
    m_next = now + m_interval;
    ++m_tick;
    if (m_options->verbose) {
      uint64_t ns = uint64_t(now.sec()) * 1000000000ULL + now.nsec();
      ZiLOG(Info, "zrest", ([tick = m_tick, count, ns](auto &s) {
	s << "event=ping-wave tick=" << tick << " count=" << count <<
	  " time_ns=" << ns;
      }));
    }
    if (m_generated < m_options->requests) armTimer_();
  }

  void dispatchPending_() {
    if (!m_pending || m_active >= m_options->concurrency) return;
    auto now = Zm::now();
    if (m_tokens->deadline <= now.sec()) { refresh_(); return; }
    ZmRef<const TokenState> state = m_tokens;
    unsigned n = m_options->concurrency - m_active;
    if (n > WorkBatch) n = WorkBatch;
    while (n-- && m_pending) ping_(m_pending.shift(), state);
    if (m_pending && m_active < m_options->concurrency)
      txRun(0, [this]() { dispatchPending_(); });
  }

  void armTimer_() {
    if (m_timerArmed || m_stopping || !m_mx || !m_interval ||
	m_generated >= m_options->requests) return;
    m_timerArmed = true;
    m_mx->add(&m_timer, m_next, ZmScheduler::Update,
      [this](auto &&arm) {
	return arm([this]() {
	  m_timerArmed = false;
	  drive_();
	});
      }, m_txSID);
  }

  void cancelTimer_() {
    if (m_mx) m_mx->del(&m_timer);
    m_timerArmed = false;
  }

  void fail_() {
    if (m_state == Failed || m_state == Complete) return;
    m_state = Failed;
    m_logicalFailed = 1;
    cancelTimer_();
    m_waveState = nullptr;
    m_waveRemaining = 0;
    m_waveCount = 0;
    m_cleaning = true;
    seal(0);
    cleanPending_();
  }

  void cleanPending_() {
    unsigned n = WorkBatch;
    while (n-- && m_pending) (void)m_pending.shift();
    if (m_pending) {
      txRun(0, [this]() { cleanPending_(); });
      return;
    }
    m_cleaning = false;
    terminalReady_();
  }

  void terminalReady_() {
    if (!m_notified && !m_active && !m_cleaning &&
	(m_state == Complete || m_state == Failed)) {
      m_notified = true;
      done.post();
    }
  }

  template <typename Wake>
  void quiesceClean_(Wake wake) {
    unsigned n = WorkBatch;
    while (n-- && m_pending) (void)m_pending.shift();
    if (m_pending) {
      txRun(0, [this, wake = ZuMv(wake)]() mutable {
	quiesceClean_(ZuMv(wake));
      });
      return;
    }
    m_waveState = nullptr;
    m_waveRemaining = 0;
    m_waveCount = 0;
    m_tokens = nullptr;
    m_mx = nullptr;
    txRun(0, [wake = ZuMv(wake)]() mutable { wake(); });
  }

  const Options		*m_options = nullptr;
  ZiMultiplex		*m_mx = nullptr;
  ZmRef<const TokenState> m_tokens;
  ZmRef<const TokenState> m_waveState;
  PendingQ		m_pending;
  ZmScheduler::Timer	m_timer;
  ZuTime		m_interval;
  ZuTime		m_next;
  uint64_t		m_attempt = 0;
  unsigned		m_generated = 0;
  unsigned		m_completed = 0;
  unsigned		m_active = 0;
  unsigned		m_tick = 0;
  unsigned		m_waveRemaining = 0;
  unsigned		m_waveCount = 0;
  int			m_txSID = 0;
  ClientState		m_state = Idle;
  unsigned		m_logicalFailed = 0;
  bool			m_timerArmed = false;
  bool			m_stopping = false;
  bool			m_wavePaced = false;
  bool			m_cleaning = false;
  bool			m_notified = false;
};

template <typename Link, typename Response>
void AuthReq::process(Link *, const Response *response) const
{
  if constexpr (ZuIsSame<Response, TokenResponse>{}) client->tokens(response, false);
  else client->unauthorized(false, 0, false, {});
}
template <typename Link> void AuthReq::failed(Link *) const {
  client->requestFailed();
}
template <typename Link, typename Response>
void RefreshReq::process(Link *, const Response *response) const
{
  if constexpr (ZuIsSame<Response, TokenResponse>{}) client->tokens(response, true);
  else client->unauthorized(false, 0, false, state);
}
template <typename Link> void RefreshReq::failed(Link *) const {
  client->requestFailed();
}
template <typename Link, typename Response>
void PingReq::process(Link *, const Response *response) const
{
  if constexpr (ZuIsSame<Response, Pong>{}) client->pong(logicalID, response->pong);
  else client->unauthorized(true, logicalID, replayed, state);
}
template <typename Link> void PingReq::failed(Link *) const {
  client->requestFailed();
}

void Pool::archive_(ReqBuilder *) { }

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
  if (!url.host || (url.path && url.path != "/") ||
      url.hasQuery || url.hasFragment) {
    std::cerr << "zrest: URL must be an HTTP(S) origin\n";
    return 1;
  }

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

  Zhttp::ProtoPolicy::T policy;
  switch (options.http3) {
    case Http3Mode::force: policy = Zhttp::ProtoPolicy::ForceH3; break;
    case Http3Mode::disable: policy = Zhttp::ProtoPolicy::DisableH3; break;
    default: policy = Zhttp::ProtoPolicy::PreferH3; break;
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
    .linkMax(options.linkMax)
    .requestTimeout(options.timeout)
    .maxRedirects(MaxRedirects)
    .maxRetries(options.retries)
    .retainedBodyMax(RespBodyMax)
    .protocol(policy)
    .h2Policy(h2Policy)
    .secure(secure)
    .tcp(true)
    .tls(secure && policy != Zhttp::ProtoPolicy::ForceH3)
    .quic(secure && policy != Zhttp::ProtoPolicy::DisableH3);
  auto quic = Zhttp::QUICConfig()
    .caPath(options.ca).keyLogPath(options.keyLog)
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
  app.workload(options, &mx);

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
  app.quiesce();
  app.stop();
  bool incomplete = app.logicalCompleted() != options.requests;
  if (incomplete)
    ZiLOG(Error, "zrest", ([
      completed = app.logicalCompleted(), expected = options.requests,
      active = app.active()
    ](auto &s) {
      s << "incomplete run (completed=" << completed <<
	", expected=" << expected << ", active=" << active << ')';
    }));
  int rc = timedOut || incomplete || app.logicalFailed() ? 1 : 0;
  app.final();
  mx.stop();
  ZiLog::stop();
  return rc;
}
