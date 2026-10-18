//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmContext.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZiResolver.hh>
#include <zlib/ZiTx.hh>
#include <zlib/ZgrpcHTTP.hh>
#include "ZhttpTestUtil.hh"

using namespace ZuTestUtil;
using namespace Zgrpc;

#include "ZgrpcWire.hh"

#include <zlib/ZgrpcClient.hh>
#include "../../zhttp/src/ZhttpServerImpl.hh"

using Methods = ZuTypeList<Unary>;
ZgrpcCatalogDerive(Matrix, Methods);
ZgrpcCatalogImpl(Matrix);

struct ClientApp { };
struct Call : ZmObject, ZmNHeap<Call> {
friend inline ZuStringT<"Zgrpc.ClientTest.Call"> ZmNHeapID(Call *);
  explicit Call(State *s) : state{s} { }
  void headers(const Metadata &) {
    if (!state->mx->invoked(3)) ++state->errors;
    ++state->headers;
  }
  void process(Result<Packet> result) {
    if (!state->mx->invoked(3) || result.status.code != Code::OK ||
	!result.message.is<Message<Packet>>() || result.http.httpVersion != state->version)
      ++state->errors;
    if (result.message.is<Message<Packet>>()) {
      auto &message = result.message.p<Message<Packet>>();
      if (message.object().seq != 42 || message.object().payload != ZuBSpan{state->payload})
	++state->errors;
      state->result = ZuMv(result.message);
      ++state->received;
    }
    state->done.post();
  }
  void failed(Failure failure) {
    if (!state->mx->invoked(3) || failure.code != state->expectedFailure ||
        failure.metadata != metadata || (httpStatus &&
	(failure.http.status != httpStatus || failure.http.redirects || failure.http.retries))) {
      log_("unexpected client failure ", int(failure.code), " metadata=", int(failure.metadata),
	" HTTP=", failure.http.status);
      ++state->errors;
    }
    if (state->peerCancel &&
	((state->version == Zhttp::Version::H2 ?
	  failure.http.errorDomain != Zhttp::ErrorDomain::H2Reset :
	  failure.http.errorDomain != Zhttp::ErrorDomain::H3Reset &&
	  failure.http.errorDomain != Zhttp::ErrorDomain::H3Stop) ||
	 failure.http.errorCode != (state->version == Zhttp::Version::H2 ? 8 : 0x10c)))
      ++state->errors;
    ++state->failures;
    state->done.post();
  }
  State *state;
  MetaError::T metadata = MetaError::OK;
  unsigned httpStatus = 0;
};

struct Mode { enum { Normal, Expired, Ingress, Deadline, LateRx, Redirect, PeerCancel }; };

static void wire(bool h3, bool empty, int mode = Mode::Normal)
{
  ZuTestScope(wire);
  Zhttp::Test::TempDir temp;
  ZuCheck(temp.init("ZgrpcWire"), (return));
  ZtString<> cert, key;
  ZuCheck(Zhttp::Test::writeLocalhostCert(temp, cert, key), (return));
  State state;
  state.peerCancel = mode == Mode::PeerCancel;
  state.redirect = mode == Mode::Redirect;
  if (state.redirect) state.expectedFailure = Code::UNKNOWN;
  else if (mode == Mode::Ingress || state.peerCancel) state.expectedFailure = Code::CANCELLED;
  else if (mode != Mode::Normal) state.expectedFailure = Code::DEADLINE_EXCEEDED;
  state.version = h3 ? Zhttp::Version::H3 : Zhttp::Version::H2;
  if (!empty) {
    state.payload.length((256U << 10) + 7);
    memset(state.payload.data(), 0x71, state.payload.length());
  }
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(4)
      .thread(1, [](auto &t) { t.isolated(1); })
      .thread(2, [](auto &t) { t.isolated(1); })
      .thread(3, [](auto &t) { t.isolated(1); })
      .thread(4, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2)};
  state.mx = &mx;
  ZuCheck(mx.start(), (return));
  unsigned port = peerPort(h3);
  ZuCheck(port, ({ mx.stop(); return; }));
  Zhttp::HubConfig hub{&mx, "3", "4"};
  App server{&state};
  Zhttp::ServerConfig serverConfig;
  serverConfig.localIP(ZiIP{"127.0.0.1"}).port(port).bodyMax(uint64_t(-1))
    .retainedBodyMax(1U << 20);
  if (h3) serverConfig.quic(Zhttp::QUICConfig{}.certPath(cert).keyPath(key));
  else serverConfig.tls(Zhttp::H2Config{}.tls([&cert, &key](auto &tls) {
    tls.certPath(cert).keyPath(key);
  })
    .policy(Zhttp::H2Policy::Force));
  bool initialized = server.init(hub, serverConfig);
  ZuCheck(initialized);
  bool started = initialized && server.start();
  ZuCheck(started);
  bool listening = started && state.listening.timedwait(Zm::now(10)) == 0;
  ZuCheck(listening && !state.errors.load_());
  ClientApp app;
  Zgrpc::Client<ClientApp, Matrix> client;
  ClientConfig config;
  if (mode >= Mode::Ingress) { Limits limits; limits.maxPending = 1; config.limits(limits); }
  config.bodyMax(uint64_t(-1)).retainedBodyMax(1U << 20)
    .maxRedirects(0).protocol(h3 ? Zhttp::ProtoPolicy::ForceH3 : Zhttp::ProtoPolicy::DisableH3);
  if (state.redirect) config.maxRetries(3).maxRedirects(3);
  bool cliInit = listening && client.init(hub, 1, config, &app, {},
    Zhttp::H2Config{}.tls([&cert](auto &tls) {
      tls.caPath(cert);
    }).policy(Zhttp::H2Policy::Force),
    Zhttp::QUICConfig{}.caPath(cert));
  ZuCheck(cliInit);
  bool pooled = cliInit && client.pool(0, Zhttp::Destination{"localhost", uint16_t(port)});
  ZuCheck(pooled);
  if (state.redirect && pooled)
    ZuCheck(!client.config(0)->maxRetries() && !client.config(0)->maxRedirects());
  bool cliUp = pooled && client.start();
  ZuCheck(cliUp);
  ZmSemaphore held, release;
  bool blocked = cliUp && (mode == Mode::Ingress || mode == Mode::Deadline);
  if (blocked) {
    mx.run([&held, &release]() { held.post(); release.wait(); }, 4);
    ZuCheck(held.timedwait(Zm::now(10)) == 0);
  }
  if (mode == Mode::LateRx) {
    state.holdRx = &held;
    state.releaseRx = &release;
  }
  ZmRef<Call> call = new Call{&state};
  if (state.redirect) { call->metadata = MetaError::MissingStatus; call->httpStatus = 307; }
  ZmRef<ClientCall<Unary>> token;
  bool admitted = false;
  if (cliUp) {
    CallOptions options;
    if (mode == Mode::Expired) options.timeout = 0;
    if (mode == Mode::Deadline) options.timeout = 10000000;
    if (mode == Mode::LateRx) options.timeout = 1000000000;
    if (blocked) {
      token = client.open<Unary>(0, call, ZuMv(options));
      admitted = bool(token);
      auto denied = client.open<Unary>(0, call);
      ZuCheck(!denied && client.pending() == 1);
      if (token && mode == Mode::Ingress) ZuCheck(token->cancel());
    } else admitted = client.call<Unary>(0, Packet{42, state.payload}, call, ZuMv(options));
  }
  ZuCheck(admitted);
  if (mode == Mode::LateRx && admitted) {
    ZuCheck(held.timedwait(Zm::now(10)) == 0);
    ZmScheduler::Timer probe;
    ZmSemaphore expired;
    mx.add(&probe, Zm::now(2), ZmScheduler::Update, [&expired](auto &&arm) {
      return arm([&expired]() { expired.post(); });
    }, 1);
    ZuCheck(expired.timedwait(Zm::now(10)) == 0);
    mx.del(&probe);
    release.post();
  }
  bool done = admitted && state.done.timedwait(Zm::now(15)) == 0;
  ZuCheck(done);
  if (blocked) {
    ZuCheck(client.pending() == 1); // logical failure does not bypass Tx drain
    if (token) {
      ZuCheck(!token->live() && !token->send(Packet{}) && !token->end() && !token->cancel());
    }
    release.post();
  }
  if (cliUp) ZuCheck(client.stop());
  ZuCheck(!client.pending());
  client.final();
  if (mode == Mode::LateRx) ZuCheck(!state.headers);
  if (started) ZuCheck(server.stop());
  server.final();
  ZiResolver::stopSync();
  ZiResolver::final();
  ZuCheck(mx.stop());
  ZuCheck(!state.errors.load_());
  if (state.redirect) ZuCheck(state.peerRequests.load_() == 1);
  if (mode == Mode::Normal) {
    ZuCheck(state.received == 1 && !state.failures);
    if (state.result.is<Message<Packet>>()) {
      ZuCheck(state.result.p<Message<Packet>>().object().payload == ZuBSpan{state.payload});
    } else ZuCheck(false);
  } else ZuCheck(!state.received && state.failures == 1);

}

static void initFinal()
{
  ZuTestScope(initFinal);
  ZiMultiplex mx{ZiMxParams{}};
  ClientApp app;
  Zgrpc::Client<ClientApp, Matrix> client;
  ZuCheck(client.init(Zhttp::HubConfig{&mx}, 1, ClientConfig{}, &app));
  client.final(); // no scheduler thread is running to service a posted stop
  ZuCheck(!client.up() && !client.pending());
}

// Drive an already expired field provider on Tx while Rx is held. This isolates
// the construction boundary from the timer, which cannot run until Rx resumes.
static void encodeExpiry()
{
  ZuTestScope(encodeExpiry);
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(4);
    for (unsigned i = 1; i <= 4; ++i) s.thread(i, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2)};
  State state;
  state.mx = &mx;
  state.expectedFailure = Code::DEADLINE_EXCEEDED;
  ZuCheck(mx.start(), (return));
  ClientApp app;
  Zgrpc::Client<ClientApp, Matrix> client;
  bool initialized = client.init(Zhttp::HubConfig{&mx, "3", "4"}, 1, ClientConfig{}, &app);
  ZuCheck(initialized);
  if (initialized) {
    ZmSemaphore held, release, encoded, drained;
    mx.run([&held, &release]() { held.post(); release.wait(); }, 3);
    ZuCheck(held.timedwait(Zm::now(10)) == 0);
    using Work = ClientWorkT<ClientApp, Matrix, Unary, Call>;
    ZmRef<Work> work = new Work{&client, 1, 0, deadlineNow() - 1, ZmRef<Call>{new Call{&state}}};
    auto token = work->token();
    ZmRef<ClientReqQ<ClientApp, Matrix>::Node> request = new ClientReqQ<ClientApp, Matrix>::Node{};
    request->work = ZuMv(work);
    bool emitted = false;
    mx.run([request = ZuMv(request), &emitted, &encoded]() {
      request->operation([&emitted](auto, auto &&) { emitted = true; });
      encoded.post();
    }, 4);
    ZuCheck(encoded.timedwait(Zm::now(10)) == 0 && !emitted);
    // Terminal selection is queued to Rx; it cannot invalidate the token on Tx.
    ZuCheck(token->live());
    release.post();
    ZuCheck(state.done.timedwait(Zm::now(10)) == 0);
    ZuCheck(!token->live() && token->cancelled() && state.failures == 1 && !state.received);
    mx.run([&mx, &drained]() {
      mx.run([&drained]() { drained.post(); }, 3);
    }, 4);
    ZuCheck(drained.timedwait(Zm::now(10)) == 0);
    client.final();
  }
  ZuCheck(mx.stop() && !state.errors.load_());
}

// Force the HTTP result ahead of the input's queued terminal callback on Rx.
// No timing assumption about transport acknowledgement or scheduler load.
static void inputFailure(bool metadata, Zhttp::ResultCode::T code, bool expired = false)
{
  ZuTestScope(inputFailure);
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(4);
    for (unsigned i = 1; i <= 4; ++i) s.thread(i, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2)};
  State state;
  state.mx = &mx;
  state.expectedFailure = expired ? Code::DEADLINE_EXCEEDED : Code::UNIMPLEMENTED;
  ZuCheck(mx.start(), (return));
  ClientApp app;
  Zgrpc::Client<ClientApp, Matrix> client;
  bool initialized = client.init(Zhttp::HubConfig{&mx, "3", "4"}, 1, ClientConfig{}, &app);
  ZuCheck(initialized);
  if (initialized) {
    ZmRef<Call> observer = new Call{&state};
    observer->metadata = metadata ? MetaError::Encoding : MetaError::OK;
    using Work = ClientWorkT<ClientApp, Matrix, Unary, Call>;
    ZmRef<Work> work = new Work{&client, 1, 0, expired ? deadlineNow() - 1 : -1, ZuMv(observer)};
    auto token = work->token();
    mx.run([work = ZuMv(work), metadata, code]() {
      ClientParser<ClientApp, Matrix> parser;
      work->parseFn(work.ptr(), parser);
      auto input = parser.input();
      input->route(work->rx);
      if (metadata) {
	ZuCheck(!input->meta().header(Zhttp::FieldSection::Final, "grpc-encoding", "gzip"));
	ZuCheck(!input->failed() && input->meta().failed());
      } else {
	input->fail(Code::UNIMPLEMENTED);
	ZuCheck(input->failed() && !input->finished());
      }
      // This generic transport cancellation must retain the local diagnostic.
      work->resultFn(work.ptr(), Zhttp::Result{.code = code});
      work->resultFn(work.ptr(), Zhttp::Result{.code = Zhttp::ResultCode::Failed});
    }, 3);
    ZuCheck(state.done.timedwait(Zm::now(10)) == 0);
    ZuCheck(!token->live() && token->cancelled() == expired);
    ZmSemaphore drained;
    mx.run([&mx, &drained]() {
      mx.run([&drained]() { drained.post(); }, 3);
    }, 4);
    ZuCheck(drained.timedwait(Zm::now(10)) == 0);
    client.final();
  }
  ZuCheck(mx.stop());
  ZuCheck(state.failures == 1 && !state.received && !state.errors.load_());
}

// A test-owned native peer emits GOAWAY after opening the request stream.
// It deliberately leaves the RPC unanswered, so refusal is the only completion.
struct GoState : State {
  unsigned port = 0;
  ZmAtomic<unsigned> goaways = 0;
  ZmSemaphore firstOpened;
  bool drain = false;
};
template <typename Profile>
struct GoParser : Zhttp::Parser,
  Zhttp::MessageTraits<Profile>::template RequestParser<GoParser<Profile>> {
  using Base = Zhttp::MessageTraits<Profile>::template RequestParser<GoParser>;
  using State = Base::State;
  const Zhttp::CatalogView &catalog() const {
    return Zhttp::CatalogData<HdrCatalog>::configure();
  }
  using Zhttp::Parser::header;
  void header(Zhttp::FieldSection::T, int, ZuBSpan, ZuSpan<uint8_t>) { }
  bool bodyInfo(Zhttp::BodyType::T, uint64_t) { return true; }
  template <typename Rx> bool body(Rx &rx) { return Zhttp::bodyDrain(rx); }
  void complete(typename State::T) { }
};
template <typename Profile>
struct GoSession {
  GoParser<Profile> parser;
  template <typename Link> void connected(Link &) { }
  template <typename Link> void disconnected(Link &, bool) { }
  template <typename Link, typename Rx> int process(Link &link, Rx &rx) {
    using State = GoParser<Profile>::State;
    return link.receive(parser, rx) == State::Error ? -1 : 0;
  }
};
struct GoTrailers : Zhttp::Builder {
  template <typename Key> void headerOffset(uint64_t, unsigned) const { }
  void fields(Zhttp::FieldOutput &out) const {
    Zhttp::BuilderFields<const GoTrailers> adapter{this, fieldCatalog};
    adapter.fields(out);
  }
  const Zhttp::CatalogView *fieldCatalog =
    &Zhttp::CatalogData<HdrCatalog>::configure();
  using Zhttp::Builder::header;
  template <typename L> void header(L &&emit) const { emit("grpc-status", "0"); }
};
template <typename Profile>
struct GoReply : Zhttp::Builder,
  Zhttp::MessageTraits<Profile>::template Response<GoReply<Profile>, true, true> {
  using Base = Zhttp::MessageTraits<Profile>::template Response<GoReply, true, true>;
  using Base::body;
  unsigned hdrPlan() const { return unsigned(-1); }
  void fields(Zhttp::FieldOutput &out) {
    Zhttp::BuilderFields<GoReply> adapter{this, fieldCatalog};
    adapter.fields(out);
  }
  const Zhttp::CatalogView *fieldCatalog =
    &Zhttp::CatalogData<HdrCatalog>::configure();
  template <typename Key> void headerOffset(uint64_t, unsigned) { }
  using Zhttp::Builder::header;
  unsigned status() const { return 200; }
  bool streamResponse() const { return false; }
  template <typename L> void header(L &&emit) const {
    emit("content-type", "application/grpc");
  }
};
template <typename Profile> struct GoPeer;
template <typename Profile>
struct GoLink : Zhttp::ServerLink<GoPeer<Profile>, GoLink<Profile>, Profile, GoSession<Profile>> {
  using Base = Zhttp::ServerLink<GoPeer<Profile>, GoLink, Profile, GoSession<Profile>>;
  using Base::Base;
};
template <typename Profile>
struct GoPeer : Zhttp::ProtocolServer<GoPeer<Profile>, Profile, GoLink<Profile>> {
  GoState *state;
  explicit GoPeer(GoState *s) : state{s} { }
  ZmRef<GoLink<Profile>> first;
  ZmContext firstNative;
  uintptr_t firstID = 0;
  void reply(ZmRef<GoLink<Profile>> link) {
    this->txRun([link = ZuMv(link), s = state]() mutable {
      GoReply<Profile> builder;
      auto tx = link->transmit_(builder);
      builder.begin(tx);
      FrameError::T error;
      auto buf = encodeFrame(ZmRef<ZiIOBuf>{new ZiIOBufAlloc<>{}},
	0, Default::MaxMessageBytes, Packet{42, {}}, error);
      if (!buf || error != FrameError::OK) { ++s->errors; return; }
      auto body = builder.body(tx);
      body << buf->cspan();
      if (!body.flush() || !builder.trailers(tx, GoTrailers{}) || !tx.flush())
	++s->errors;
      link->finish();
    });
  }
  void replyFirst() {
    this->rxRun([this]() { reply(ZuMv(first)); });
  }
  void dropFirst() {
    this->rxRun([this]() { first = nullptr; firstNative = {}; });
  }
  ZiIP localIP() const { return ZiIP{"127.0.0.1"}; }
  unsigned localPort() const { return state->port; }
  unsigned idleTimeout() const { return 0; }
  template <typename ...Args> void listening(Args &&...) { state->listening.post(); }
  void listenFailed(bool) { ++state->errors; state->listening.post(); }
  void connected(GoLink<Profile> &appLink, Zhttp::ConnectedInfo) {
    unsigned n = ++state->peerRequests;
    if (state->drain) {
      switch (n) {
	case 1:
	  first = &appLink;
	  firstID = appLink.session().id;
	  if constexpr (ZuIsSame<Profile, Zhttp::H3QUIC>{})
	    firstNative = ZmRef<Zhttp::H3_::SrvLink<GoPeer, GoLink<Profile>>>{
	      reinterpret_cast<Zhttp::H3_::SrvLink<GoPeer, GoLink<Profile>> *>(firstID)};
	  else
	    firstNative = ZmRef<Zhttp::H2_::SrvLink<GoPeer, GoLink<Profile>>>{
	      reinterpret_cast<Zhttp::H2_::SrvLink<GoPeer, GoLink<Profile>> *>(firstID)};
	  state->firstOpened.post();
	  return;
	case 2:
	  if (appLink.session().id != firstID) ++state->errors;
	  break;
	case 3:
	  // firstNative retains the old allocation, excluding address reuse.
	  if (appLink.session().id == firstID) ++state->errors;
	  reply(ZmRef{&appLink});
	  return;
	default:
	  ++state->errors;
	  return;
      }
    }
    if constexpr (ZuIsSame<Profile, Zhttp::H3QUIC>{}) {
      using Native = Zhttp::H3_::SrvLink<GoPeer, GoLink<Profile>>;
      ZmRef<Native> link{reinterpret_cast<Native *>(appLink.session().id)};
      auto control = link->h3.control; // Rx snapshot before Tx dispatch
      if (!control) { ++state->errors; return; }
      this->txRun([link = ZuMv(link), control = ZuMv(control), s = state]() {
	auto tx = control->txStream_();
	Zhttp::H3::TxBytes out{tx};
	if (Zhttp::H3::putVar(out, 0x07) < 0 ||
	    Zhttp::H3::putVar(out, 1) < 0 ||
	    Zhttp::H3::putVar(out, s->drain ? 4 : 0) < 0 || !tx.flush())
	  ++s->errors;
	else ++s->goaways;
      });
    } else {
      using Native = Zhttp::H2_::SrvLink<GoPeer, GoLink<Profile>>;
      ZmRef<Native> link{reinterpret_cast<Native *>(appLink.session().id)};
      this->txRun([link = ZuMv(link), s = state]() {
	auto tx = link->txStream_();
	Zhttp::H2_::StreamBytes out{tx};
	if (Zhttp::H2::putGoaway(out, s->drain ? 1 : 0, Zhttp::H2::Error::NoError) < 0 || !tx.flush())
	  ++s->errors;
	else ++s->goaways;
      });
    }
  }
  void disconnected(GoLink<Profile> &, bool) { }
};
struct GoCall : ZmObject, ZmNHeap<GoCall> {
friend inline ZuStringT<"Zgrpc.Goaway.Call"> ZmNHeapID(GoCall *);
  GoState *state;
  bool success;
  bool goaway = true;
  explicit GoCall(GoState *s, bool ok = false) : state{s}, success{ok} { }
  void process(Result<Packet> result) {
    if (!success || !state->mx->invoked(3) || result.status.code != Code::OK ||
	result.http.httpVersion != state->version || !result.message.is<Message<Packet>>())
      ++state->errors;
    else if (result.message.p<Message<Packet>>().object().seq != 42) ++state->errors;
    ++state->received;
    state->done.post();
  }
  void failed(Failure f) {
    if (success || !state->mx->invoked(3) || f.code != Code::UNAVAILABLE ||
	f.metadata != MetaError::OK || f.http.code != Zhttp::ResultCode::ReplayUnsafe ||
	f.http.httpVersion != state->version || !f.http.request || !f.http.attempt ||
	f.http.redirects || f.http.retries || f.http.errorCode ||
	(goaway && f.http.errorDomain != (state->version == Zhttp::Version::H2 ?
	  Zhttp::ErrorDomain::H2Goaway : Zhttp::ErrorDomain::H3Goaway))) {
      log_("GOAWAY failure RPC=", int(f.code), " HTTP=", int(f.http.code),
	" version=", int(f.http.httpVersion), " request=", f.http.request,
	" attempt=", f.http.attempt);
      ++state->errors;
    }
    ++state->failures;
    state->done.post();
  }
};
template <typename Profile>
static void goaway(bool drain = false)
{
  ZuTestScope(goaway);
  constexpr bool h3 = ZuIsSame<Profile, Zhttp::H3QUIC>{};
  Zhttp::Test::TempDir temp;
  ZuCheck(temp.init("ZgrpcGoaway"), (return));
  ZtString<> cert, key;
  ZuCheck(Zhttp::Test::writeLocalhostCert(temp, cert, key), (return));
  GoState state;
  state.drain = drain;
  state.version = h3 ? Zhttp::Version::H3 : Zhttp::Version::H2;
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(4);
    for (unsigned i = 1; i <= 4; ++i) s.thread(i, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2)};
  state.mx = &mx;
  ZuCheck(mx.start(), (return));
  state.port = peerPort(h3);
  ZuCheck(state.port, ({ mx.stop(); return; }));
  Zhttp::HubConfig hub{&mx, "3", "4"};
  GoPeer<Profile> server{&state};
  bool initialized;
  if constexpr (h3) initialized = server.init(hub,
    Zhttp::QUICConfig{}.certPath(cert).keyPath(key));
  else initialized = server.init(hub,
    Zhttp::H2Config{}.tls([&cert, &key](auto &tls) {
      tls.certPath(cert).keyPath(key);
    }).policy(Zhttp::H2Policy::Force));
  ZuCheck(initialized);
  bool started = initialized && server.start();
  ZuCheck(started);
  bool listening = started && state.listening.timedwait(Zm::now(10)) == 0;
  ZuCheck(listening && !state.errors.load_());
  ClientApp app;
  Zgrpc::Client<ClientApp, Matrix> client;
  ClientConfig config;
  if (drain) config.links(1).linkMax(2).concurrency(2);
  config.protocol(h3 ? Zhttp::ProtoPolicy::ForceH3 : Zhttp::ProtoPolicy::DisableH3);
  bool cliInit = listening && client.init(hub, 1, config, &app, {},
    Zhttp::H2Config{}.tls([&cert](auto &tls) {
      tls.caPath(cert);
    }).policy(Zhttp::H2Policy::Force),
    Zhttp::QUICConfig{}.caPath(cert));
  ZuCheck(cliInit);
  bool pooled = cliInit && client.pool(0, Zhttp::Destination{"localhost", uint16_t(state.port)});
  ZuCheck(pooled);
  bool cliUp = pooled && client.start();
  ZuCheck(cliUp);
  ZmRef<GoCall> first, last;
  if (drain && cliUp) {
    first = new GoCall{&state, true};
    ZuCheck(client.call<Unary>(0, Packet{42, {}}, first));
    ZuCheck(state.firstOpened.timedwait(Zm::now(10)) == 0);
  }
  ZmRef<GoCall> call = new GoCall{&state};
  CallOptions options;
  options.timeout = 5000000000;
  bool admitted = cliUp && client.call<Unary>(0, Packet{42, {}}, call, ZuMv(options));
  ZuCheck(admitted);
  ZuCheck(admitted && state.done.timedwait(Zm::now(15)) == 0);
  if (drain && cliUp) {
    ZuCheck(state.failures == 1 && !state.received);
    server.replyFirst();
    ZuCheck(state.done.timedwait(Zm::now(15)) == 0);
    ZuCheck(state.received == 1 && state.failures == 1);
    last = new GoCall{&state, true};
    CallOptions next;
    next.timeout = 5000000000;
    ZuCheck(client.call<Unary>(0, Packet{42, {}}, last, ZuMv(next)));
    ZuCheck(state.done.timedwait(Zm::now(15)) == 0);
    server.dropFirst();
  }
  if (cliUp) ZuCheck(client.stop());
  ZuCheck(!client.pending());
  client.final();
  if (started) ZuCheck(server.stop());
  server.final();
  ZiResolver::stopSync();
  ZiResolver::final();
  ZuCheck(mx.stop());
  ZuCheck(!state.errors.load_());
  ZuCheck(state.goaways.load_() == 1 && state.peerRequests.load_() == (drain ? 3 : 1));
  ZuCheck(state.failures == 1 && state.received == (drain ? 2 : 0));
}

static void transition()
{
  ZuTestScope(transition);
  Zhttp::Test::TempDir temp, other;
  ZuCheck(temp.init("ZgrpcTransition") && other.init("ZgrpcUntrusted"), (return));
  ZtString<> cert, key, badCert, badKey;
  ZuCheck(Zhttp::Test::writeLocalhostCert(temp, cert, key) &&
    Zhttp::Test::writeLocalhostCert(other, badCert, badKey), (return));
  GoState state, bad;
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(4);
    for (unsigned i = 1; i <= 4; ++i) s.thread(i, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2)};
  state.mx = bad.mx = &mx;
  ZuCheck(mx.start(), (return));
  unsigned port = peerPort(false);
  bad.port = peerPort(true);
  state.altPort = bad.port;
  ZuCheck(port && bad.port, ({ mx.stop(); return; }));
  Zhttp::HubConfig hub{&mx, "3", "4"};
  App server{&state};
  Zhttp::ServerConfig serverConfig;
  serverConfig.localIP(ZiIP{"127.0.0.1"}).port(port)
    .tls(Zhttp::H2Config{}.tls([&cert, &key](auto &tls) {
      tls.certPath(cert).keyPath(key);
    }).policy(Zhttp::H2Policy::Force));
  bool initialized = server.init(hub, serverConfig);
  ZuCheck(initialized);
  bool started = initialized && server.start();
  ZuCheck(started);
  GoPeer<Zhttp::H3QUIC> untrusted{&bad};
  bool badInit = untrusted.init(hub,
    Zhttp::QUICConfig{}.certPath(badCert).keyPath(badKey));
  ZuCheck(badInit);
  bool badUp = badInit && untrusted.start();
  ZuCheck(badUp);
  bool listening = started && badUp && state.listening.timedwait(Zm::now(10)) == 0 &&
    bad.listening.timedwait(Zm::now(10)) == 0;
  ZuCheck(listening && !state.errors.load_() && !bad.errors.load_());
  ClientApp app;
  Zgrpc::Client<ClientApp, Matrix> client;
  ClientConfig config;
  config.protocol(Zhttp::ProtoPolicy::PreferH3).maxRetries(3).maxRedirects(3);
  bool cliInit = listening && client.init(hub, 1, config, &app, {},
    Zhttp::H2Config{}.tls([&cert](auto &tls) {
      tls.caPath(cert);
    }).policy(Zhttp::H2Policy::Force),
    Zhttp::QUICConfig{}.caPath(cert));
  ZuCheck(cliInit);
  bool pooled = cliInit && client.pool(0, Zhttp::Destination{"localhost", uint16_t(port)});
  ZuCheck(pooled);
  if (pooled) ZuCheck(!client.config(0)->maxRetries() && !client.config(0)->maxRedirects());
  bool cliUp = pooled && client.start();
  ZuCheck(cliUp);
  ZmRef<GoCall> first = new GoCall{&state, true};
  if (cliUp) {
    CallOptions options;
    options.timeout = 5000000000;
    ZuCheck(client.call<Unary>(0, Packet{42, {}}, first, ZuMv(options)));
    ZuCheck(state.done.timedwait(Zm::now(15)) == 0);
    ZuCheck(state.received == 1 && !state.failures && state.peerRequests.load_() == 1);
    // Discovery applies to a new RPC; failure of that H3 attempt cannot replay it.
    state.version = Zhttp::Version::H3;
    ZmRef<GoCall> failed = new GoCall{&state};
    failed->goaway = false;
    CallOptions next;
    next.timeout = 5000000000;
    ZuCheck(client.call<Unary>(0, Packet{42, {}}, failed, ZuMv(next)));
    ZuCheck(state.done.timedwait(Zm::now(15)) == 0);
    ZuCheck(client.stop());
  }
  ZuCheck(!client.pending());
  client.final();
  if (badUp) ZuCheck(untrusted.stop());
  untrusted.final();
  if (started) ZuCheck(server.stop());
  server.final();
  ZiResolver::stopSync();
  ZiResolver::final();
  ZuCheck(mx.stop());
  ZuCheck(!state.errors.load_() && !bad.errors.load_());
  ZuCheck(state.received == 1 && state.failures == 1);
  ZuCheck(state.peerRequests.load_() == 1 && !bad.peerRequests.load_());
}

static void nativeCodes()
{
  ZuTestScopeRT(nativeCodes);
  auto check = [](Zhttp::ErrorDomain::T domain, uint64_t native, Code::T expected) {
    Zhttp::Result result{.code = Zhttp::ResultCode::Failed,
      .errorDomain = domain, .errorCode = native};
    ZuCheckRT(httpCode(result) == expected);
    result.code = Zhttp::ResultCode::TimedOut;
    ZuCheckRT(httpCode(result) == Code::DEADLINE_EXCEEDED);
    result.code = Zhttp::ResultCode::Cancelled;
    ZuCheckRT(httpCode(result) == Code::CANCELLED);
  };
  check(Zhttp::ErrorDomain::H2Reset, 0, Code::INTERNAL);
  check(Zhttp::ErrorDomain::H2Reset, 7, Code::UNAVAILABLE);
  check(Zhttp::ErrorDomain::H2Reset, 8, Code::CANCELLED);
  check(Zhttp::ErrorDomain::H2Reset, 11, Code::RESOURCE_EXHAUSTED);
  check(Zhttp::ErrorDomain::H2Reset, 12, Code::PERMISSION_DENIED);
  check(Zhttp::ErrorDomain::H2Goaway, 0, Code::UNAVAILABLE);
  check(Zhttp::ErrorDomain::H3Goaway, 0, Code::UNAVAILABLE);
  for (auto domain : {Zhttp::ErrorDomain::H3Reset, Zhttp::ErrorDomain::H3Stop,
      Zhttp::ErrorDomain::H3Error}) {
    check(domain, 0x100, Code::INTERNAL);
    check(domain, 0x107, Code::RESOURCE_EXHAUSTED);
    check(domain, 0x10b, Code::UNAVAILABLE);
    check(domain, 0x10c, Code::CANCELLED);
  }
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZiTestResidue::init("zgrpcclienttest");
  ZuTestMain();
  ZuTestCall(nativeCodes);
  ZuTestCall(initFinal);
  ZuTestCall(transition);
  ZuTestCall((goaway<Zhttp::H2TLS>));
  ZuTestCall((goaway<Zhttp::H3QUIC>));
  ZuTestCall((goaway<Zhttp::H2TLS>), true);
  ZuTestCall((goaway<Zhttp::H3QUIC>), true);
  ZuTestCall(wire, false, true, Mode::PeerCancel);
  ZuTestCall(wire, true, true, Mode::PeerCancel);
  ZuTestCall(wire, false, true, Mode::Redirect);
  ZuTestCall(wire, true, true, Mode::Redirect);
  ZuTestCall(encodeExpiry);
  ZuTestCall(inputFailure, false, Zhttp::ResultCode::Cancelled);
  ZuTestCall(inputFailure, false, Zhttp::ResultCode::Failed);
  ZuTestCall(inputFailure, false, Zhttp::ResultCode::TimedOut);
  ZuTestCall(inputFailure, true, Zhttp::ResultCode::Cancelled);
  ZuTestCall(inputFailure, true, Zhttp::ResultCode::Failed);
  ZuTestCall(inputFailure, true, Zhttp::ResultCode::TimedOut);
  ZuTestCall(inputFailure, true, Zhttp::ResultCode::TimedOut, true);
  ZuTestCall(wire, false, false);
  ZuTestCall(wire, false, true);
  ZuTestCall(wire, true, false);
  ZuTestCall(wire, true, true);
  ZuTestCall(wire, false, true, Mode::Expired);
  ZuTestCall(wire, true, true, Mode::Expired);
  ZuTestCall(wire, false, true, Mode::Ingress);
  ZuTestCall(wire, true, true, Mode::Ingress);
  ZuTestCall(wire, false, true, Mode::Deadline);
  ZuTestCall(wire, true, true, Mode::Deadline);
  ZuTestCall(wire, false, true, Mode::LateRx);
  ZuTestCall(wire, true, true, Mode::LateRx);
}
