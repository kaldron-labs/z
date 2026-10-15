//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// WSS workload peer; Zum provisioning and hub lifetime belong to the fixture.

#include <stdio.h>
#include <string.h>

#include <iostream>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuSort.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZfCf.hh>
#include <zlib/ZvCf.hh>
#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiFile.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZtcAppTypes.hh>
#include <zlib/ZtcFB.hh>
#include <zlib/ZtcMsg.hh>
#include <zlib/Zws.hh>

namespace ZtcHubLoad_ {

struct Device {
  ZtString<> id;
};
ZfStruct(, (Device, Cf),
  (id, (Mutable, Required),	String));
using Devices = ZtArray<Device, ZtArrayHeapID<"Ztc.Load.Devices">>;
inline ZfCf::AsArray<ZfFieldTC::UDT> ZfCf_Fmt(Devices *);

struct Config {
  Devices devices;
  ZtString<> tokenFile;
  ZtString<> token;
  ZtString<> wss;
  ZtString<> ca;
  unsigned clients = 32;
  unsigned subs = 32;
  unsigned publishers = 256;
  unsigned rounds = 3;
  unsigned turn = 64;
  unsigned sloMS = 200;
  unsigned messagesPS = 0;
  bool direct = false;
  bool echo = false;
  ZtString<ZtStringHeapID<"Ztc.Load.Key">> key;
};
ZfStruct(, (Config, Cf),
  (devices,	(Mutable, Required),	UDT),
  (tokenFile,	(Mutable, Required),	String),
  (token,	(Mutable, Required),	String),
  (wss,		(Mutable, Required),	String),
  (ca,		(Mutable, Required),	String),
  (clients,	(Mutable),		UInt32),
  (subs,	(Mutable),		UInt32),
  (publishers,	(Mutable),		UInt32),
  (rounds,	(Mutable),		UInt32),
  (turn,	(Mutable),		UInt32),
  (sloMS,	(Mutable),		UInt32),
  (messagesPS,	(Mutable),		UInt32),
  (direct,	(Mutable),		Bool),
  (echo,	(Mutable),		Bool),
  (key,		(Mutable),		String));

using Frame = ZmRef<ZiIOBuf>;
using Buf = ZiIOBufAlloc<1024, 1U << 16, "Ztc.Load.Frame">;

static void send(auto &link, Frame frame)
{
  link.txStream([frame = ZuMv(frame)](auto &tx) {
    tx << ZuBSpan{frame->data(), frame->length};
    tx.flush();
  }, Zws::Opcode::Binary);
}

static Frame request(ZuCSpan device, uint64_t sub, bool subscribe)
{
  Ztc::Builder builder{Frame{new Buf}};
  auto nested = builder.startNested();
  auto body = Zfb::save(builder, Ztc::Request{
    .seqNo = 0, .interval = subscribe ? 1000U : 0U,
    .group = uint8_t(Ztc::Group::App), .subscribe = subscribe});
  builder.finish(Ztc::saveMsg(builder, nested,
    Ztc::Body::Request, body, sub, device));
  return builder.buf();
}

static bool loadTokens(ZuCSpan path, ZtArray<ZtString<>> &tokens)
{
  ZiFile file{Zi::Path{path}, ZiFile::ReadOnly | ZiFile::GC};
  if (!file) return false;
  auto length = file.size();
  if (!length || length > Zi::Offset(8U << 20)) return false;
  ZtString<> data;
  data.length(unsigned(length));
  if (file.read(data.data(), unsigned(length)) != int(length)) return false;
  unsigned begin = 0;
  for (unsigned i = 0; i <= data.length(); ++i) {
    if (i != data.length() && data[i] != '\n') continue;
    if (i > begin)
      tokens.push(ZtString<>{ZuCSpan{data.data() + begin, i - begin}});
    begin = i + 1;
  }
  return true;
}

struct App {
  struct LinkState {
    Frame frame;
    ZtArray<unsigned, ZtArrayHeapID<"Ztc.Load.Counts">> counts;
    ZtArray<uint64_t, ZtArrayHeapID<"Ztc.Load.Gens">> generations;
    unsigned index = 0;
    bool agent = false;
  };
  using Client = Zws::Client<App, Zhttp::H1TLS>;
  using Link = Client::Link;
  struct Route {
    Link *link;
    uint64_t seq;
  };

  // Requested single-message target, separate from the aggregate load gate.
  enum { LatencyNS = 200000 };
  struct Timing {
    int64_t encode;
    int64_t handoff;
  };

  Config *cf;
  ZtArray<Timing, ZtArrayHeapID<"Ztc.Load.Timing">> timings;
  unsigned maxInFlight = 0;
  Client *client = nullptr;
  ZtArray<Route, ZtArrayHeapID<"Ztc.Load.Routes">> routes;
  ZmSemaphore ready;
  ZmSemaphore done;
  ZmSemaphore down;
  ZmScheduler::Timer timer;
  int64_t start = 0;
  int64_t maxNS = 0;
  int64_t maxPublishNS = 0;
  uint64_t sent = 0;
  uint64_t received = 0;
  uint64_t txBytes = 0;
  uint64_t rxBytes = 0;
  unsigned agents = 0;
  bool failed = false;
  bool stopping = false;
  bool readyPosted = false;
  bool readyOK = false;

  uint64_t rate() const { return uint64_t(cf->clients) * cf->subs * cf->publishers; }

  uint64_t pace() const { return cf->messagesPS ? cf->messagesPS : rate(); }

  void fail(const char *reason = "unknown") {
    if (failed || stopping) return;
    failed = true;
    std::cerr << "load failure reason=" << reason << " sent=" << sent
	      << " received=" << received << '\n';
    if (!readyPosted) {
      readyPosted = true;
      ready.post();
    }
    done.post();
  }

  void arm() {
    if (failed || stopping || sent == rate() * cf->rounds) return;
    auto due = start + int64_t(sent * 1000000000/pace());
    client->mx()->add(&timer, ZuTime{due / 1000000000, int32_t(due % 1000000000)},
      ZmScheduler::Update, [this](auto &&arm) {
	return arm([this]() { tick(); });
      }, client->rxThread());
  }

  void tick() {
    if (failed || stopping) return;
    auto total = rate() * cf->rounds;
    // Batch at most one millisecond of records, even in reduced diagnostic runs.
    auto perMS = pace()/1000;
    auto turn = uint64_t(cf->turn);
    if (turn > perMS) turn = perMS ? perMS : 1;
    for (unsigned n = 0; n < turn && sent < total; ++n, ++sent) {
      timings[sent].encode = int64_t(Zm::now().nanosecs());
      auto &route = routes[sent % routes.length()];
      auto publisher = unsigned((sent / routes.length()) % cf->publishers);
      ZtString<> name{"publisher-"};
      name << publisher;
      Ztc::Builder builder{Frame{new Buf}};
      auto nested = builder.startNested();
      auto id = builder.string(name);
      auto value = Zfb::save(builder, Ztc::AppTelemetry{
	.version = ZuID{"load"}, .role = ZuID{"publisher"},
	.startTime = ZuTime{ZuTime::Nano{start + int64_t(sent * 1000000000/pace())}},
	.ztcver = ZuSemVer{publisher}});
      auto body = Ztc::saveTelemetry(builder, id, route.seq,
	Ztc::TelemetryBody::AppTelemetry, value);
      builder.finish(Ztc::saveMsg(builder, nested, Ztc::Body::Telemetry, body));
      timings[sent].handoff = int64_t(Zm::now().nanosecs());
      auto late = timings[sent].handoff -
	(start + int64_t(sent * 1000000000/pace()));
      if (late > maxPublishNS) maxPublishNS = late;
      auto inFlight = sent + 1 - received;
      if (inFlight > maxInFlight) maxInFlight = inFlight;
      auto frame = builder.buf();
      txBytes += frame->length;
      send(*route.link, ZuMv(frame));
    }
    arm();
  }

  template <typename L>
  void connected(L &link, const Zhttp::ConnectedInfo &) {
    auto &state = link.state();
    if (cf->direct) {
      state.counts.length(cf->subs);
      memset(state.counts.data(), 0, cf->subs * sizeof(unsigned));
      for (unsigned i = 0; i < cf->subs; ++i)
	routes.push(Route{&link, i + 1});
      if (routes.length() == uint64_t(cf->clients) * cf->subs) {
	readyPosted = readyOK = true;
	ready.post();
	start = int64_t(Zm::now().nanosecs()) + 100000000;
	arm();
      }
      return;
    }
    if (state.agent) {
      if (++agents == cf->devices.length()) {
	std::cout << "load agents connected\n" << std::flush;
	if (!readyPosted) {
	  readyPosted = readyOK = true;
	  ready.post();
	}
      }
      return;
    }
    state.counts.length(cf->subs);
    state.generations.length(cf->subs);
    memset(state.counts.data(), 0, cf->subs * sizeof(unsigned));
    memset(state.generations.data(), 0, cf->subs * sizeof(uint64_t));
    // Pre-authentication input is bounded. The first ACK proves admission
    // before we pipeline the remaining subscription requests.
    auto device = (uint64_t(state.index) * cf->subs) % cf->devices.length();
    send(link, request(cf->devices[device].id, 1, true));
  }

  template <typename L> int messageStart(L &link, Zws::Opcode::T opcode) {
    if (opcode != Zws::Opcode::Binary) { fail("opcode"); return -1; }
    auto &frame = link.state().frame;
    if (!frame) frame = new Buf;
    frame->length = 0;
    return 1;
  }
  template <typename L, typename Rx> int process(L &link, Rx &rx) {
    auto &frame = link.state().frame;
    return Zhttp::bodyEach(rx, [this, &frame](ZuSpan<uint8_t> span) {
      if (span.length() > (1U << 16) - frame->length) { fail("frame length"); return false; }
      auto size = frame->length + span.length();
      frame->append(span);
      if (frame->length != size) { fail("frame append"); return false; }
      return true;
    }) ? 1 : -1;
  }
  template <typename L> int messageEnd(L &link) {
    auto &state = link.state();
    auto msg = Ztc::msg(ZuBSpan{state.frame->data(), state.frame->length});
    if (!msg) { fail("invalid message"); return -1; }
    auto envelope = Ztc::msgBody(msg);
    if (state.agent) {
      auto body = envelope.tag() == Ztc::Body::Request ?
	envelope.get<Ztc::Body::Request>() : Zfb::View<Ztc::Request>{};
      if (!body ||
        !body.get<Ztc::ZfField(Request, seqNo)>()) { fail("agent request"); return -1; }
      if (!body.get<Ztc::ZfField(Request, subscribe)>()) return 1;
      if (routes.length() >= uint64_t(cf->clients) * cf->subs) { fail("route count"); return -1; }
      routes.push(Route{&link, body.get<Ztc::ZfField(Request, seqNo)>()});
      Ztc::Builder builder{Frame{new Buf}};
      auto nested = builder.startNested();
      auto ack = Zfb::save(builder,
	Ztc::Ack{ZuID{"publisher"}, body.get<Ztc::ZfField(Request, seqNo)>(), 1000});
      builder.finish(Ztc::saveMsg(builder, nested, Ztc::Body::Ack, ack));
      send(link, builder.buf());
      if (routes.length() == uint64_t(cf->clients) * cf->subs) {
	start = int64_t(Zm::now().nanosecs()) + 100000000;
	arm();
      }
      return 1;
    }
    auto sub = cf->direct ? envelope.get<Ztc::Body::Telemetry>()
      .get<Ztc::ZfField(Telemetry, seqNo)>() : msg.get<Ztc::ZfField(Msg, subID)>();
    if (!sub || sub > cf->subs || (!cf->direct &&
	(!msg.get<Ztc::ZfField(Msg, deviceID)>() || !msg.get<Ztc::ZfField(Msg, agentGen)>()))) {
      fail("subscription metadata"); return -1;
    }
    if (!cf->direct) {
      auto device = (uint64_t(state.index) * cf->subs + sub - 1) % cf->devices.length();
      if (msg.get<Ztc::ZfField(Msg, deviceID)>() != cf->devices[device].id) {
	fail("device mismatch"); return -1;
      }
      auto &generation = state.generations[sub - 1];
      if (envelope.tag() == Ztc::Body::Ack) {
	if (generation || envelope.get<Ztc::Body::Ack>().get<Ztc::ZfField(Ack, status)>() !=
	    Ztc::AckStatus::OK) {
	  fail("ack"); return -1;
	}
	generation = msg.get<Ztc::ZfField(Msg, agentGen)>();
	if (sub == 1) {
	  for (unsigned i = 1; i < cf->subs; ++i) {
	    auto device = (uint64_t(state.index) * cf->subs + i) % cf->devices.length();
	    send(link, request(cf->devices[device].id, i + 1, true));
	  }
	}
	return 1;
      }
    }
    auto body = envelope.tag() == Ztc::Body::Telemetry ?
      envelope.get<Ztc::Body::Telemetry>() : Zfb::View<Ztc::Telemetry>{};
    if (!body) {
      std::cerr << "unexpected body type=" << int(envelope.tag())
		<< " sub=" << sub << " index=" << state.index;
      if (envelope.tag() == Ztc::Body::Error)
	std::cerr << " code=" << envelope.get<Ztc::Body::Error>()
	  .get<Ztc::ZfField(Error, code)>();
      std::cerr << '\n';
      fail("telemetry body"); return -1;
    }
    if (!cf->direct && state.generations[sub - 1] != msg.get<Ztc::ZfField(Msg, agentGen)>()) {
      std::cerr << "telemetry generation expected=" << state.generations[sub - 1]
		<< " actual=" << msg.get<Ztc::ZfField(Msg, agentGen)>() << " sub=" << sub
		<< " index=" << state.index << '\n';
      fail("telemetry generation"); return -1;
    }
    auto payload = body.get<Ztc::ZfField(Telemetry, value)>();
    if (payload.tag() != Ztc::TelemetryBody::AppTelemetry) {
      fail("telemetry type"); return -1;
    }
    auto value = payload.get<Ztc::TelemetryBody::AppTelemetry>();
    auto version = value.get<Ztc::ZfField(AppTelemetry, ztcver)>();
    auto &count = state.counts[sub - 1];
    auto publisher = count % cf->publishers;
    ZtString<> name{"publisher-"};
    name << publisher;
    if (version.value() != publisher || body.get<Ztc::ZfField(Telemetry, id)>() != name) {
      std::cerr << "telemetry value expected=" << publisher
		<< " actual=" << version << " sub=" << sub
		<< " index=" << state.index << '\n';
      fail("telemetry value"); return -1;
    }
    ++count;
    rxBytes += state.frame->length;
    auto now = int64_t(Zm::now().nanosecs());
    auto scheduled = int64_t(
      value.get<Ztc::ZfField(AppTelemetry, startTime)>().as_time().nanosecs());
    auto index = (uint64_t(scheduled - start) * pace() + 999999999)/1000000000;
    if (index >= sent) { fail("sample timestamp"); return -1; }
    auto &timing = timings[index];
    timing.encode = now - timing.encode;
    timing.handoff = now - timing.handoff;
    auto elapsed = now - scheduled;
    if (elapsed > maxNS) maxNS = elapsed;
    if (++received == rate() * cf->rounds) done.post();
    return 1;
  }
  template <typename L> void disconnected(L &link, bool clean) {
    if (!failed && !stopping)
      std::cerr << "load disconnected agent=" << link.state().agent
		<< " index=" << link.state().index << " clean=" << clean << '\n';
    fail("disconnected");
    down.post();
  }
  template <typename L> void connectFailed(L &link, bool) {
    if (!failed)
      std::cerr << "load connect failed agent=" << link.state().agent
		<< " index=" << link.state().index << '\n';
    fail("connect failed");
    down.post();
  }
};

struct Echo {
  struct LinkState { Frame frame; };
  ZmSemaphore ready;
  bool listeningOK = false;

  void listening(const ZiListenInfo &) { listeningOK = true; ready.post(); }
  void listenFailed(bool) { ready.post(); }
  template <typename L>
  bool accept(L &, ZuBSpan, ZuBSpan target, ZuBSpan protocols,
      Zws::HandshakeString &selected) {
    if (target != "/ztc" || !Zws::token(protocols, Ztc::Protocol)) return false;
    selected = Ztc::Protocol;
    return true;
  }
  template <typename L> void connected(L &, const Zhttp::ConnectedInfo &) { }
  template <typename L> void disconnected(L &, bool) { }
  template <typename L> int messageStart(L &link, Zws::Opcode::T opcode) {
    if (opcode != Zws::Opcode::Binary) return -1;
    link.state().frame = new Buf;
    return 1;
  }
  template <typename L, typename Rx> int process(L &link, Rx &rx) {
    auto &frame = link.state().frame;
    return Zhttp::bodyEach(rx, [&frame](ZuSpan<uint8_t> span) {
      if (span.length() > (1U << 16) - frame->length) return false;
      auto size = frame->length + span.length();
      frame->append(span);
      return frame->length == size;
    }) ? 1 : -1;
  }
  template <typename L> int messageEnd(L &link) {
    auto frame = ZuMv(link.state().frame);
    if (!Ztc::msg(frame->cspan())) return -1;
    send(link, ZuMv(frame));
    return 1;
  }
};

static bool runEcho(const Config &cf, const Zws::URI &uri)
{
  ZiMxParams params;
  params.scheduler([](auto &s) {
    s.nThreads(4);
    for (unsigned i = 1; i <= 4; ++i) s.thread(i).isolated(true);
  }).rxThread(1).txThread(2);
  ZiMultiplex mx{ZuMv(params)};
  if (!mx.start()) return false;
  Echo app;
  Zws::Server<Echo, Zhttp::H1TLS> server{&app, ZiIP{uri.host}, uri.port};
  auto tls = Zhttp::TLSConfig{}.certPath(cf.ca).keyPath(cf.key);
  if (!server.init(Zhttp::HubConfig{&mx, "1", "2"}, tls) || !server.start()) {
    server.final();
    mx.stop();
    return false;
  }
  app.ready.wait();
  if (app.listeningOK) {
    std::cout << "echo listening\n" << std::flush;
    ::getchar();
  }
  bool stopped = ZmBlock<bool>{}([&server](auto wake) {
    server.stop([wake = ZuMv(wake)](bool ok) mutable { wake(ok); });
  });
  server.final();
  mx.stop();
  return app.listeningOK && stopped;
}

static bool run(const char *path)
{
  auto source = ZvCf::load(path);
  Config cf;
  ZfCf::handler<Config>(source.p<1>()).update(cf);
  if ((!cf.direct && !cf.devices) || !cf.clients || !cf.subs || !cf.publishers ||
      !cf.rounds || !cf.turn || !cf.sloMS || cf.messagesPS > 1000000000) return false;
  Zws::URI uri;
  if (!Zws::URI::parse(uri, cf.wss).ok() || !uri.secure()) return false;
  if (cf.echo) return runEcho(cf, uri);
  ZtArray<ZtString<>> tokens;
  if (!cf.direct && (!loadTokens(cf.tokenFile, tokens) ||
    tokens.length() != cf.devices.length())) {
    return false;
  }
  ZiMxParams params;
  params.scheduler([](auto &s) {
    s.nThreads(8);
    for (unsigned i = 1; i <= 8; ++i) s.thread(i).isolated(true);
  }).rxThread(1).txThread(2);
  ZiMultiplex mx{ZuMv(params)};
  if (!mx.start()) return false;
  App app{&cf};
  app.timings.length(app.rate() * cf.rounds);
  App::Client client{&app};
  app.client = &client;
  Zws::Config ws;
  ws.maxMessage = 1U << 16;
  ws.maxQueuedInput = 1U << 20;
  ws.handshakeTimeout = 60;
  ws.closeTimeout = 5;
  auto tls = Zhttp::H2Config{}.caPath(cf.ca);
  if (!client.init(Zhttp::HubConfig{&mx, "3", "4"}, tls, ws) || !client.start()) {
    client.final();
    mx.stop();
    return false;
  }
  ZtArray<ZmRef<App::Link>, ZtArrayHeapID<"Ztc.Load.Links">> links;
  ZtArray<ZtString<>, ZtArrayHeapID<"Ztc.Load.Auth">> authorizations;
  unsigned agents = cf.direct ? 0 : cf.devices.length();
  authorizations.length(agents + cf.clients);
  for (unsigned i = 0; i < agents + cf.clients; ++i) {
    bool agent = i < agents;
    auto &token = agent ? tokens[i] : cf.token;
    authorizations[i] = "Bearer ";
    authorizations[i] << token;
    auto link = ZmRef<App::Link>{new App::Link{&client, uri, Ztc::Protocol,
      authorizations[i]}};
    link->state().agent = agent;
    link->state().index = agent ? i : i - agents;
    links.push(ZuMv(link));
  }
  auto fanoutStart = Zm::now().nanosecs();
  for (unsigned i = 0, n = cf.direct ? links.length() : agents; i < n; ++i) {
    links[i]->connect();
  }
  auto fanoutNS = Zm::now().nanosecs() - fanoutStart;
  app.ready.wait();
  // The parent waits for the hub to verify every agent before opening clients.
  bool completed = false;
  unsigned started = cf.direct ? links.length() : agents;
  if (cf.direct) {
    completed = app.readyOK && app.done.timedwait(Zm::now(300)) == 0;
  } else if (app.readyOK && ::getchar() == '\n') {
    for (unsigned i = cf.devices.length(); i < links.length(); ++i) {
      links[i]->connect();
      ++started;
      ::usleep(250000);
    }
    // Admission of the declared 2,048-agent workload can take several
    // minutes on a single SQLite-backed Zum fixture before all subscriptions
    // are established.  The data-plane SLO remains independently enforced.
    completed = app.done.timedwait(Zm::now(300)) == 0;
  }
  bool delivered = ZmBlock<bool>{}([&client, &app, completed](auto wake) {
    client.rxRun([&client, &app, completed, wake = ZuMv(wake)]() mutable {
      if (!completed) app.failed = true;
      app.stopping = true;
      client.mx()->del(&app.timer);
      wake(completed && !app.failed);
    });
  });
  bool drained = true;
  if (delivered) {
    for (const auto &link: links)
      client.rxRun([link]() mutable { link->close(); });
    auto deadline = Zm::now(ws.closeTimeout);
    for (unsigned i = 0, n = links.length(); i < n; ++i)
      if (app.down.timedwait(deadline) != 0) { drained = false; break; }
  } else {
    // Failed admission can leave TLS handshakes in progress. Drain every
    // started connection before releasing links or stopping the scheduler.
    for (unsigned i = 0; i < started; ++i)
      client.rxRun([link = links[i]]() mutable { link->disconnect(); });
    for (unsigned i = 0; i < started; ++i) app.down.wait();
  }
  bool stopped = ZmBlock<bool>{}([&client](auto wake) {
    client.stop([wake = ZuMv(wake)](bool ok) mutable { wake(ok); });
  });
  links.null();
  client.final();
  mx.stop();
  std::cout << "# load direct=" << cf.direct << " agents=" << agents << " clients=" <<
    cf.clients <<
    " subscriptions=" << uint64_t(cf.clients) * cf.subs << " publishers=" << cf.publishers <<
    " samples=" << app.received << " max_latency_us=" << app.maxNS / 1000 <<
    " max_publish_lag_us=" << app.maxPublishNS/1000 <<
    " tx_bytes=" << app.txBytes << " rx_bytes=" << app.rxBytes <<
    " fanout_us=" << uint64_t(fanoutNS / 1000) << '\n';
  if (app.received == app.timings.length()) {
    auto report = [&app](ZuCSpan label, auto member) {
      ZuSort(app.timings.data(), app.timings.length(),
	[member](const auto &a, const auto &b) {
	  return ZuCmp<int64_t>::cmp(a.*member, b.*member);
	});
      auto n = app.timings.length();
      unsigned over = 0;
      for (const auto &t: app.timings) if (t.*member > App::LatencyNS) ++over;
      std::cout << "# latency " << label << " samples=" << n
	<< " min_ns=" << app.timings[0].*member
	<< " p50_ns=" << app.timings[(n - 1) * 50/100].*member
	<< " p95_ns=" << app.timings[(n - 1) * 95/100].*member
	<< " p99_ns=" << app.timings[(n - 1) * 99/100].*member
	<< " max_ns=" << app.timings[n - 1].*member
	<< " over_200us=" << over << " max_in_flight=" << app.maxInFlight << '\n';
    };
    report("encode_to_receive", &App::Timing::encode);
    report("handoff_to_receive", &App::Timing::handoff);
  }
  bool deliveredAll = drained && stopped && !app.failed &&
    app.received == app.rate() * cf.rounds;
#ifdef NDEBUG
  return deliveredAll &&
    (cf.devices.length() < 100 || fanoutNS <= 10000000) &&
    app.maxNS <= int64_t(cf.sloMS) * 1000000;
#else
  // Debug runs establish correctness; performance acceptance is release-only.
  return deliveredAll;
#endif
}

} // ZtcHubLoad_

int main(int argc, char **argv)
{
  using namespace ZuTestUtil;
  ZuTestMain();
  ZiLog::init("ztchubloadtest");
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  ZuCheck(argc == 2 && ZtcHubLoad_::run(argv[1]));
  ZiLog::stop();
}
