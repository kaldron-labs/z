//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zfix_demo_io_HH
#define zfix_demo_io_HH

#include <stdio.h>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmPlatform.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZtEnum.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZfixClient.hh>
#include <zlib/ZfixServer.hh>
#include "../test/ZfixDemo.hh"

namespace ZfixDemo {

namespace Scenario {

  ZtEnum(, Scenario, int8_t, RPC, Groups, Events, Reject, Policy);
  ZtEnumMap(, Scenario, CLI, "rpc", "groups", "events", "reject", "policy");
}

// Shared core TxBody cannot carry this application FIFO's links.
using BodyQueue = ZmList<Zfix::TxBody::Ref,
  ZmListHeapID<"Zfix.Demo.Queue">>;
struct Options {
  Scenario::T	scenario = Scenario::RPC;
  unsigned	mode = Zfix::Transport::FIX4;
  bool		tls = false, badAuth = false;
  ZuCSpan	token;
  const char	*cert = nullptr, *key = nullptr, *ca = nullptr;

  unsigned calls() const {
    return scenario == Scenario::Events ? 0 : scenario == Scenario::Policy ? 2 : 1;
  }

  unsigned messages() const {
    return scenario == Scenario::Policy ? 9 :
      scenario == Scenario::RPC || scenario == Scenario::Groups ? 2 : 1;
  }

  unsigned requests() const {
    return scenario == Scenario::Events ? 0 : scenario == Scenario::Policy ? 3 : 1;
  }
};

struct State {
  Options		options;
  Summary		summary;
  ZmAtomic<unsigned>	errors{0}, closed{0};
  ZmSemaphore		listening, done, stopped;
  unsigned		port = 0; // published by listening semaphore, immutable afterwards

  explicit State(Options value) : options{value} { }

  void fail(const char *file = __builtin_FILE(), unsigned line = __builtin_LINE()) {
    fprintf(stderr, "demo failure %s:%u\n", file, line); ++errors; done.post();
  }

  void closed_() { if (closed.xchAdd(1) == 1) stopped.post(); }
};

static Zfix::SessionConfig config(bool client, const Options &options) {
  Zfix::SessionConfig c; c.mode = options.mode; c.initiator = client; c.heartBtInt = 0;
  c.id = client ? "DEMO-CLIENT" : "DEMO-SERVER";
  c.header.version = options.mode == Zfix::Transport::FIXT11 ?
    Zfix::Version::VT1_1 : Zfix::Version::V4_4;
  c.header.local = client ? "DEMO-C" : "DEMO-S"; c.header.peer = client ? "DEMO-S" : "DEMO-C";
  c.appVerID = Zfix::AppVersion::V5_0_SP2; return c;
}
template <typename Net, bool Client> struct Hub;
template <typename Net, bool Client> class Link;
template <typename Net> struct Hub<Net, true> : Zfix::Client<Hub<Net, true>, Dictionary, Net> {
  using Link = ZfixDemo::Link<Net, true>;
  State	*state;

  explicit Hub(State *value) : state{value} { }
};

template <typename Net> struct Hub<Net, false> : Zfix::Server<Hub<Net, false>, Dictionary, Net> {
  using Link = ZfixDemo::Link<Net, false>;
  State		*state;
  ZmRef<Link>	connection; // native Rx-owned application route

  explicit Hub(State *value) : state{value} { }

  ZiConnection *accepted(const ZiCxnInfo &);

  ZiIP localIP() const { return ZiIP{"127.0.0.1"}; }
  unsigned localPort() const { return 0; }
  unsigned nAccepts() const { return 1; }

  void listening(const ZiListenInfo &info) { state->port = info.port; state->listening.post(); }

  void listenFailed(bool) { state->fail(); state->listening.post(); }
};

template <typename Net, bool Client>
class Link : public ZmNHeap<Link<Net, Client>>, public ZuIf<Client,
    Zfix::CliLink<Hub<Net, Client>, Link<Net, Client>, Dictionary, Net>,
    Zfix::SrvLink<Hub<Net, Client>, Link<Net, Client>, Dictionary, Net>> {
friend inline ZuStringT<"Zfix.Demo.Link"> ZmNHeapID(Link *);
  using Base = ZuIf<Client,
    Zfix::CliLink<Hub<Net, Client>, Link<Net, Client>, Dictionary, Net>,
    Zfix::SrvLink<Hub<Net, Client>, Link<Net, Client>, Dictionary, Net>>;
  using W = Zfix::Work<Zfix::RequestContext, Link>;
  struct Job {
    typename W::Ref	work;
    uint64_t		key;
  };

  using Jobs = ZmList<Job, ZmListHeapID<"Zfix.Demo.Jobs">>;
public:
  using ZmNHeap<Link>::operator new;
  using ZmNHeap<Link>::operator delete;
  using Base::app;

  explicit Link(Hub<Net, Client> *hub) : Base{hub, config(Client, hub->state->options)},
    m_calls{hub->state->options.calls()}, m_results{hub->state->options.calls()},
    // Finite batch plus one Session::Logon and one Session::Logout; byte cap is independent.
    m_store{hub->state->options.messages() + hub->state->options.requests() + 2} { }

  unsigned fixAssociateTx_(Zfix::TxBody::Ref body, Zfix::TxContext context, uint64_t) {
    ZmAssert(app()->txInvoked());
    if (body->administrative() && body->msgType() != "3") return Zfix::TxLoad::Accepted;
    bool saved = m_store.associate(body, context);
    return saved || body->administrative() ? Zfix::TxLoad::Accepted : Zfix::TxLoad::Refused;
  }

  bool fixReleaseTx_(Zfix::TxBody::Ref, Zfix::TxContext c, uint64_t) {
    ZmAssert(app()->txInvoked()); m_store.erase(c.epoch, c.seqNo); return true;
  }

  void fixPublishedTx_(Zfix::TxBody::Ref body, Zfix::TxContext c, ZuDateTime time) {
    ZmAssert(app()->txInvoked());
    if ((!body->administrative() || body->msgType() == "3") &&
	!m_store.published(c, time) && !body->administrative()) state()->fail();
  }

  void fixArchiveTx_(uint64_t, uint64_t begin, uint64_t end) {
    ZmAssert(app()->txInvoked()); state()->summary.archived += end - begin;
  }

  Zfix::TxReload fixRetrieveTx_(Zfix::TxContext c, uint64_t, uint64_t) {
    ZmAssert(app()->txInvoked()); ++state()->summary.retrieved;
    return m_store.fetch(c.epoch, c.seqNo);
  }

  void fixCompleteTx_(Zfix::TxBody::Ref, Zfix::TxContext, bool ok) {
    ZmAssert(app()->txInvoked());
    if (!ok && !options().badAuth) state()->fail();
    if (ok) ++state()->summary.localSent; // local transport completion, never Calls::complete
  }

  void fixSubmittedTx_(Zfix::TxBody::Ref body, unsigned result) {
    ZmAssert(app()->txInvoked());
    if (result == Zfix::TxSelect::Accepted || result == Zfix::TxSelect::Pending) return;
    if (result != Zfix::TxSelect::Busy && result != Zfix::TxSelect::Held) { state()->fail();
      return; }
    uint64_t bytes = body->memory() + ZmHeapAllocSize<sizeof(BodyQueue::Node)>{};
    if (m_queue.count_() >= options().messages() + options().requests() ||
	bytes > QueueBytes || m_queueBytes > QueueBytes - bytes) { state()->fail(); return; }
    m_queue.push(body); m_queueBytes += bytes; this->queued();
  }

  Zfix::TxBody::Ref fixDequeueTx_() {
    ZmAssert(app()->txInvoked()); auto node = m_queue.shift(); if (!node) return {};
    auto body = ZuMv(node->data());
    m_queueBytes -= body->memory() + ZmHeapAllocSize<sizeof(BodyQueue::Node)>{}; return body;
  }

  void fixSelectedTx_(Zfix::TxBody::Ref body, Zfix::TxContext, unsigned result) {
    if (result != Zfix::TxSelect::Accepted && !m_stopping) state()->fail();
  }

  void fixRejectedTx_(Zfix::PeerReject report) {
    ZmAssert(app()->txInvoked());
    if constexpr (Client) {
      // Reference is epoch + outbound FIX sequence, not queue ID or reject frame sequence.
      auto history = m_store.fetch(report.context.epoch, report.message.refSeqNo).history;
      auto key = history.body ? history.body->id() : 0;
      if (!key || history.body->msgType() != "U1" || report.message.refTagID != 38 ||
	  key == report.message.refSeqNo) { state()->fail(); return; }
      ++state()->summary.seqRejects;
      app()->rxRun([this, key] { m_calls.reject(key); ++m_messages; finished(); });
    } else state()->fail();
  }

  Zfix::TxBody::Ref fixLogonTx_(Zfix::TxBody::Ref body) {
    if (!options().token.length()) return body;
    // Handshake only: preserve every managed byte and validate added fields.
    auto token = Client && options().badAuth ? ZuCSpan{"wrong"} : options().token;
    if (!Zfix::Fmt::text(token)) return {};
    ZtString<ZtStringHeapID<"Zfix.Demo.Auth">> bytes{body->body()};
    bytes << "553=demo\1" "554=" << token << '\1';
    Zfix::Fmt::Error error;
    return Zfix::TxBody::restoreControl<LoginMsg>(bytes, body->id(), error);
  }

  unsigned fixAdmissionRx_(Zfix::SessionEvent, ZmRef<Zfix::RxQueue::Node> input,
      Zfix::AdmitRequest &) {
    ZmAssert(app()->rxInvoked());
    if (options().token.length()) {
      Zfix::Fmt::MessageHandler<Login> decoded{input->data().frame}; auto login = decoded.ctor();
      if (!decoded.valid() || login.user != "demo" || login.password != options().token) {
	++state()->summary.authRefused; state()->done.post(); return Zfix::Admission::Refused;
      }
    }
    ++state()->summary.authAccepted; return Zfix::Admission::Accepted;
  }

  void fixEventRx_(Zfix::SessionEvent event, ZmRef<Zfix::RxQueue::Node>) {
    ZmAssert(app()->rxInvoked());
    if (event.actions & Zfix::SessionAction::Failure) {
      if (!options().badAuth) state()->fail(); else state()->done.post();
    }
    if (!(event.actions & Zfix::SessionAction::Ready) || m_started) return;
    m_started = true;
    if constexpr (Client) {
      if (options().scenario == Scenario::Events) return;
      for (unsigned i = 0; i < options().calls(); ++i) {
	uint64_t key = 1001 + i;
	if (!m_calls.add(key)) state()->fail();
	submitRequest(key);
      }
      if (options().scenario == Scenario::Policy) submitRequest(1001);
    } else if (options().scenario == Scenario::Events) {
      Zfix::Fmt::Error error;
      if (!this->template send<Tick>(Event{"DEMO", 12345}, 20001, error)) state()->fail();
    }
  }

  template <typename Response>
  Zfix::Fmt::Error fixReplyRx_(Req *, Response *, const Zfix::RequestContext &context) {
    const auto &s = this->session_();
    return context.context().session == s.instance_() && context.context().epoch == s.epoch_() ?
      Zfix::Fmt::Error{} : Zfix::Fmt::Error{Zfix::Fmt::ErrorCode::Unresolved, 11};
  }

  Zfix::Fmt::Error fixReceiveRx_(Req *, const Request &request,
    const Zfix::RequestContext &context) {
    ZmAssert(app()->rxInvoked());
    if constexpr (Client) return {Zfix::Fmt::ErrorCode::Unresolved, 35};
    else {
      if (!request.qty) return {Zfix::Fmt::ErrorCode::Value, 38};
      if (request.symbol != "DEMO") return {Zfix::Fmt::ErrorCode::Value, 55};
      if (options().scenario == Scenario::Groups) {
	if (request.parties.length() != 2 || request.parties[0].id != "desk" ||
	    request.parties[1].id != "trader" || request.parties[1].role != 2)
	  return {Zfix::Fmt::ErrorCode::Value, 453};
	state()->summary.groups += request.parties.length();
      }
      typename W::Ref work;
      if (options().scenario == Scenario::Policy && !context.context().replay
	&& !m_results.find(request.key)) {
	Zfix::Fmt::Error error;
	work = this->template work<Req>(context, error,
	  ZmHeapAllocSize<sizeof(typename Jobs::Node)>{});
	if (!work) return error;
      }
      unsigned decision = m_results.admit(request.key, request.qty, context.context().replay);
      switch (decision) {
	case Decision::New: break;
	case Decision::Cached: return replies(context, request.key, false);
	default: return {Zfix::Fmt::ErrorCode::Value, 11};
      }
      if (options().scenario != Scenario::Policy) return replies(context, request.key, false);
      Zfix::Fmt::Error error;
      m_jobs.push(Job{work, request.key});
      if (m_results.count() != options().calls()) return {};
      // Reverse calls AND each call's report order. The client owns the terminal rule.
      while (auto node = m_jobs.pop()) {
	auto work = ZuMv(node->data().work); auto key = node->data().key; node = nullptr;
	if (auto error = replies(work->input(), key, true)) return error;
      }
      if (!this->template send<Ack>(Report{9999, 9999, "A", 0}, 9999, error)) return error;
      return {};
    }
  }

  template <typename Entry>
  Zfix::Fmt::Error fixReceiveRx_(Entry *, const Report &report, const Zfix::RequestContext &) {
    if constexpr (Client) { m_calls.report(report); ++m_messages; finished(); return {}; }
    else return {Zfix::Fmt::ErrorCode::Unresolved, 35};
  }

  Zfix::Fmt::Error fixReceiveRx_(Tick *, const Event &event, const Zfix::RequestContext &) {
    if constexpr (Client) {
      if (event.symbol != "DEMO" || event.price != 12345) state()->fail();
      ++m_events; ++m_messages; finished(); return {};
    } else return {Zfix::Fmt::ErrorCode::Unresolved, 35};
  }

  void fixStopRx_(uint64_t, uint64_t) {
    m_jobs.clean();
    if constexpr (Client) {
      const auto &s = m_calls.stats(); auto &out = state()->summary;
      out.registered = s.registered; out.complete = s.complete; out.duplicate = s.duplicate;
      out.unknown = s.unknown; out.rejected = s.rejected; out.firstComplete = s.firstComplete;
      out.events = m_events; if (s.invalid) state()->fail(); m_calls.clear();
    } else {
      state()->summary.fresh = m_results.stats().fresh;
      state()->summary.cached = m_results.stats().cached; m_results.clear();
    }
  }

  void fixStopTx_(uint64_t) {
    m_stopping = true; m_queue.clean(); m_queueBytes = 0; m_store.clear();
  }

  void fixStoppedRx_(bool) { state()->closed_(); }
private:
  State *state() const { return this->app()->state; }
  const Options &options() const { return state()->options; }

  void submitRequest(uint64_t key) {
    Request request{key, options().scenario == Scenario::Reject ? 0U : 10U, "DEMO", {}};
    if (options().scenario == Scenario::Groups) {
      request.parties.push(Party{"desk", 1}); request.parties.push(Party{"trader", 2});
    }
    Zfix::Fmt::Error error;
    if (!this->template send<Req>(request, key, error)) state()->fail();
  }

  Zfix::Fmt::Error replies(const Zfix::RequestContext &context, uint64_t key, bool reverse) {
    auto value = m_results.find(key); if (!value) return {Zfix::Fmt::ErrorCode::Unresolved, 11};
    Zfix::Fmt::Error error;
    auto ack = [this, &context, key, value, &error] { return this->template reply<Req,
      Ack>(context, Report{key, value->ack, "A", 0}, value->ack, error); };
    auto fill = [this, &context, key, value, &error] { return this->template reply<Req,
      Fill>(context, Report{key, value->fill, "F", value->qty}, value->fill, error); };
    if (reverse) { if (!fill() || !ack() || !fill()) return error; }
    else if (!ack() || !fill()) return error;
    return {};
  }

  void finished() {
    if (m_messages == options().messages()) state()->done.post();
    else if (m_messages > options().messages()) state()->fail();
  }

  static constexpr uint64_t	QueueBytes = uint64_t(Zfix::Fmt::Limits::FrameBytes)<<2;
  Calls				m_calls; // Rx-owned application correlation
  Results			m_results; // Rx-owned cached business results
  Jobs				m_jobs; // Rx-owned deferred work, bounded by link/hub leases
  unsigned			m_messages = 0, m_events = 0;
  bool				m_started = false;
  // One Tx group, isolated from preceding Rx call/result/job state.
  alignas(Zm::CacheLineSize)
  Store				m_store;
  BodyQueue			m_queue; // Tx-owned unsequenced queue
  uint64_t			m_queueBytes = 0;
  bool				m_stopping = false;
};

template <typename Net> ZiConnection *Hub<Net, false>::accepted(const ZiCxnInfo &info) {
  if (!connection) connection = new Link{this};
  return new typename Link::Cxn{connection, info};
}
} // ZfixDemo

#endif /* zfix_demo_io_HH */
