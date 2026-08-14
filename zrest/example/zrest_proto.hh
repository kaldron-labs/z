//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zrest_proto_HH
#define zrest_proto_HH

#include <zlib/ZmBlock.hh>
#include <zlib/ZmPQueue.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmTime.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZtEnum.hh>

#include <zlib/ZhttpClient.hh>

#include "zrest_auth.hh"
#include "zrestproto.hh"

ZtEnumNS(, Http3Mode, int8_t, force, prefer, disable);
ZtEnumNS(, Http2Mode, int8_t, force, prefer, disable);

enum {
  ClientTimeout = 15,
  H3StallTimeout = 15,
  H3QuietTimeout = 2,
  MaxRedirects = 8,
  WorkBatch = 64
};

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

template <typename Heap>
struct PingReq_ : public Heap, public ZmObject {
  bool				ping = false;
  Client			*client = nullptr;
  ZmRef<const TokenState>	state;
  uint64_t			logicalID = 0;
  bool				replayed = false;

  template <typename Link, typename Response>
  void process(Link *, const Response *) const;

  template <typename Link> void failed(Link *) const;
};
using PingReq_Heap = ZmHeap<"zrest.PingReq", PingReq_<ZuVoid>>;
ZuDerive(PingReq, (PingReq_<PingReq_Heap>));

ZfStruct((PingReq, URI), (((ping), (Required)), (Bool)));

#include "zrestproto_cli.hh"

using PingBuilder = PingBuilder_<PingReq>;

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

extern ZmSemaphore done;

class Client : public Zhttp::Client<Client, Pool> {
public:
  void idle();
  void workload(const Options &, ZiMultiplex *);
  void quiesce();

  unsigned logicalCompleted() const { return m_completed; }
  unsigned logicalFailed() const { return m_logicalFailed; }

  void tokens(const TokenResponse *, bool refresh);
  void unauthorized(bool ping, uint64_t logicalID, bool replayed,
    ZmRef<const TokenState>);
  void pong(uint64_t logicalID, bool value);
  void requestFailed();

private:
  void workload_(const Options *, ZiMultiplex *);
  void authenticate_();
  void refresh_();

  template <typename Builder, typename Request>
  void send_(Request *object) {
    ZmRef<ReqBuilder> request = new ReqBuilder{};
    request->id = m_attempt++;
    request->template init<Builder>(object);
    ++m_active;
    send(0, ZuMv(request));
  }

  void ping_(Pending, ZmRef<const TokenState>);
  void retire_();
  void drive_();
  void startWave_(unsigned count, bool paced);
  void issueWave_();
  void dispatchPending_();
  void armTimer_();
  void cancelTimer_();
  void fail_();
  void cleanPending_();
  void terminalReady_();
  template <typename Wake> void quiesceClean_(Wake);

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

template <typename Heap>
template <typename Link, typename Response>
void AuthReq_<Heap>::process(Link *, const Response *response) const
{
  if constexpr (ZuIsSame<Response, TokenResponse>{})
    client->tokens(response, false);
  else
    client->unauthorized(false, 0, false, {});
}
template <typename Heap>
template <typename Link> void AuthReq_<Heap>::failed(Link *) const {
  client->requestFailed();
}
template <typename Heap>
template <typename Link, typename Response>
void RefreshReq_<Heap>::process(Link *, const Response *response) const
{
  if constexpr (ZuIsSame<Response, TokenResponse>{})
    client->tokens(response, true);
  else
    client->unauthorized(false, 0, false, state);
}
template <typename Heap>
template <typename Link> void RefreshReq_<Heap>::failed(Link *) const {
  client->requestFailed();
}
template <typename Heap>
template <typename Link, typename Response>
void PingReq_<Heap>::process(Link *, const Response *response) const
{
  if constexpr (ZuIsSame<Response, Pong>{})
    client->pong(logicalID, response->pong);
  else
    client->unauthorized(true, logicalID, replayed, state);
}
template <typename Heap>
template <typename Link> void PingReq_<Heap>::failed(Link *) const {
  client->requestFailed();
}

#endif /* zrest_proto_HH */
