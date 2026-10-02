//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// WSS workload peer; Zum provisioning and hub lifetime belong to the fixture.

#include <stdio.h>
#include <string.h>

#include <iostream>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZfCf.hh>
#include <zlib/ZvCf.hh>
#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiFile.hh>
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
  (((id), (Mutable, Required)),	String));
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
};
ZfStruct(, (Config, Cf),
  (((devices), (Mutable, Required)),	UDT),
  (((tokenFile), (Mutable, Required)),	String),
  (((token), (Mutable, Required)),	String),
  (((wss), (Mutable, Required)),	String),
  (((ca), (Mutable, Required)),		String),
  (((clients), (Mutable)),		UInt32),
  (((subs), (Mutable)),			UInt32),
  (((publishers), (Mutable)),		UInt32),
  (((rounds), (Mutable)),		UInt32),
  (((turn), (Mutable)),			UInt32),
  (((sloMS), (Mutable)),		UInt32));

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
  Zfb::IOBuilder builder{Frame{new Buf}};
  auto id = builder.CreateString(device.data(), device.length());
  auto body = ZfbStruct::save(builder, Ztc::Request{
    .seqNo = 0, .interval = subscribe ? 1000U : 0U,
    .group = uint8_t(Ztc::fbs::Group::App), .subscribe = subscribe});
  builder.Finish(Ztc::saveMsg(builder,
    Ztc::fbs::Body::Request, body.Union(), sub, id));
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

  Config *cf;
  Client *client = nullptr;
  ZtArray<Route, ZtArrayHeapID<"Ztc.Load.Routes">> routes;
  ZmSemaphore ready;
  ZmSemaphore done;
  ZmSemaphore down;
  ZmScheduler::Timer timer;
  int64_t start = 0;
  int64_t maxNS = 0;
  uint64_t sent = 0;
  uint64_t received = 0;
  unsigned agents = 0;
  bool failed = false;
  bool stopping = false;
  bool readyPosted = false;
  bool readyOK = false;

  uint64_t rate() const { return uint64_t(cf->clients) * cf->subs * cf->publishers; }

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
    auto due = start + int64_t(sent * 1000000000 / rate());
    client->mx()->add(&timer, ZuTime{due / 1000000000, int32_t(due % 1000000000)},
      ZmScheduler::Update, [this](auto &&arm) {
        return arm([this]() { tick(); });
      }, client->rxThread());
  }

  void tick() {
    if (failed || stopping) return;
    auto total = rate() * cf->rounds;
    // Batch at most one millisecond of records, even in reduced diagnostic runs.
    auto perMS = rate() / 1000;
    auto turn = uint64_t(cf->turn);
    if (turn > perMS) turn = perMS ? perMS : 1;
    for (unsigned n = 0; n < turn && sent < total; ++n, ++sent) {
      auto &route = routes[sent % routes.length()];
      auto publisher = unsigned((sent / routes.length()) % cf->publishers);
      ZtString<> name{"publisher-"};
      name << publisher;
      Zfb::IOBuilder builder{Frame{new Buf}};
      auto id = builder.CreateString(name.data(), name.length());
      auto value = ZfbStruct::save(builder, Ztc::AppTelemetry{
        .version = ZuID{"load"}, .role = ZuID{"publisher"},
        .startTime = ZuTime{ZuTime::Nano{start + int64_t(sent * 1000000000 / rate())}},
        .ztcver = ZuSemVer{publisher}});
      auto body = Ztc::saveTelemetry(builder, id, route.seq,
        Ztc::fbs::TelemetryBody::AppTelemetry, value.Union());
      builder.Finish(Ztc::saveMsg(builder, Ztc::fbs::Body::Telemetry, body.Union()));
      send(*route.link, builder.buf());
    }
    arm();
  }

  template <typename L>
  void connected(L &link, const Zhttp::ConnectedInfo &) {
    auto &state = link.state();
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
    for (unsigned i = 0; i < cf->subs; ++i) {
      auto device = (uint64_t(state.index) * cf->subs + i) % cf->devices.length();
      send(link, request(cf->devices[device].id, i + 1, true));
    }
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
    if (state.agent) {
      auto body = msg->body_as_Request();
      if (!body || !body->seqNo()) { fail("agent request"); return -1; }
      if (!body->subscribe()) return 1;
      if (routes.length() >= uint64_t(cf->clients) * cf->subs) { fail("route count"); return -1; }
      routes.push(Route{&link, body->seqNo()});
      Zfb::IOBuilder builder{Frame{new Buf}};
      auto ack = ZfbStruct::save(builder,
        Ztc::Ack{ZuID{"publisher"}, body->seqNo(), 1000});
      builder.Finish(Ztc::saveMsg(builder, Ztc::fbs::Body::Ack, ack.Union()));
      send(link, builder.buf());
      if (routes.length() == uint64_t(cf->clients) * cf->subs) {
        start = int64_t(Zm::now().nanosecs()) + 100000000;
        arm();
      }
      return 1;
    }
    auto sub = msg->subId();
    if (!sub || sub > cf->subs || !msg->deviceId() || !msg->agentGen()) {
      fail("subscription metadata"); return -1;
    }
    auto device = (uint64_t(state.index) * cf->subs + sub - 1) % cf->devices.length();
    if (Zfb::Load::str(msg->deviceId()) != cf->devices[device].id) { fail("device mismatch"); return -1; }
    auto &generation = state.generations[sub - 1];
    if (msg->body_type() == Ztc::fbs::Body::Ack) {
      if (generation || msg->body_as_Ack()->status() != Ztc::fbs::AckStatus::OK) {
        fail("ack"); return -1;
      }
      generation = msg->agentGen();
      return 1;
    }
    auto body = msg->body_as_Telemetry();
    if (!body) {
      std::cerr << "unexpected body type=" << int(msg->body_type())
                << " sub=" << sub << " index=" << state.index;
      if (auto error = msg->body_as_Error())
        std::cerr << " code=" << int(error->code());
      std::cerr << '\n';
      fail("telemetry body"); return -1;
    }
    if (generation != msg->agentGen()) {
      std::cerr << "telemetry generation expected=" << generation
                << " actual=" << msg->agentGen() << " sub=" << sub
                << " index=" << state.index << '\n';
      fail("telemetry generation"); return -1;
    }
    if (body->value_type() != Ztc::fbs::TelemetryBody::AppTelemetry) {
      fail("telemetry type"); return -1;
    }
    auto value = ZfbStruct::ctor<Ztc::AppTelemetry>(body->value_as_AppTelemetry());
    auto &count = state.counts[sub - 1];
    auto publisher = count % cf->publishers;
    ZtString<> name{"publisher-"};
    name << publisher;
    if (value.ztcver.value() != publisher || Zfb::Load::str(body->id()) != name) {
      std::cerr << "telemetry value expected=" << publisher
                << " actual=" << value.ztcver << " sub=" << sub
                << " index=" << state.index << '\n';
      fail("telemetry value"); return -1;
    }
    ++count;
    auto elapsed = int64_t(Zm::now().nanosecs()) - int64_t(value.startTime.as_time().nanosecs());
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
  }
};

static bool run(const char *path)
{
  auto source = ZvCf::load(path);
  Config cf;
  ZfCf::handler<Config>(source.p<1>()).update(cf);
  if (!cf.devices || !cf.clients || !cf.subs || !cf.publishers ||
      !cf.rounds || !cf.turn || !cf.sloMS) return false;
  Zws::URI uri;
  if (!Zws::URI::parse(uri, cf.wss).ok() || !uri.secure()) return false;
  ZtArray<ZtString<>> tokens;
  if (!loadTokens(cf.tokenFile, tokens) || tokens.length() != cf.devices.length()) {
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
  authorizations.length(cf.devices.length() + cf.clients);
  for (unsigned i = 0; i < cf.devices.length() + cf.clients; ++i) {
    bool agent = i < cf.devices.length();
    auto &token = agent ? tokens[i] : cf.token;
    authorizations[i] = "Bearer ";
    authorizations[i] << token;
    auto link = ZmRef<App::Link>{new App::Link{&client, uri, Ztc::Protocol,
      authorizations[i]}};
    link->state().agent = agent;
    link->state().index = agent ? i : i - cf.devices.length();
    links.push(ZuMv(link));
  }
  auto fanoutStart = Zm::now().nanosecs();
  for (unsigned i = 0; i < cf.devices.length(); ++i) {
    links[i]->connect();
  }
  auto fanoutNS = Zm::now().nanosecs() - fanoutStart;
  bool fanoutOK = cf.devices.length() < 100 || fanoutNS <= 10000000;
  app.ready.wait();
  // The parent waits for the hub to verify every agent before opening clients.
  bool completed = false;
  if (app.readyOK && ::getchar() == '\n') {
    for (unsigned i = cf.devices.length(); i < links.length(); ++i) {
      links[i]->connect();
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
    for (unsigned i = 0; i < links.length(); ++i)
      if (app.down.timedwait(deadline) != 0) { drained = false; break; }
  }
  bool stopped = ZmBlock<bool>{}([&client](auto wake) {
    client.stop([wake = ZuMv(wake)](bool ok) mutable { wake(ok); });
  });
  links.null();
  client.final();
  mx.stop();
  std::cout << "# load agents=" << cf.devices.length() << " clients=" << cf.clients <<
    " subscriptions=" << uint64_t(cf.clients) * cf.subs << " publishers=" << cf.publishers <<
    " samples=" << app.received << " max_latency_us=" << app.maxNS / 1000 <<
    " fanout_us=" << uint64_t(fanoutNS / 1000) << '\n';
  return drained && stopped && fanoutOK && !app.failed && app.received == app.rate() * cf.rounds &&
    app.maxNS <= int64_t(cf.sloMS) * 1000000;
}

} // ZtcHubLoad_

int main(int argc, char **argv)
{
  using namespace ZuTestUtil;
  ZuTestMain();
  ZuCheck(argc == 2 && ZtcHubLoad_::run(argv[1]));
}
