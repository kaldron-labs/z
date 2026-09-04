//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZtString.hh>

#include <zlib/ZmBlock.hh>

#include <zlib/Zfb.hh>

#include <zlib/ZiFile.hh>
#include <zlib/ZiLog.hh>

#include <zlib/ZtcApp.hh>
#include <zlib/ZtcRing.hh>

#include "ZiTestResidue.hh"
#include "ZtcTestClient.hh"

// Compile the private agent implementation into this isolated unit target so
// routing state can be tested without exporting a test surface.
#include "../src/ZtcAgent.cc"

using namespace ZuTestUtil;

static Ztc::AgentEnv env(ZuCSpan, ZuCSpan);

namespace ZtcAgentTest_ {

enum { ReqRingSize = 1U<<18 };

struct ReqRing {
  ReqRing(const char *stem) :
    name{ZiTestResidue::uniqueName(stem)},
    ring{ZiRingParams{name, ReqRingSize}.timeout(1)}
  {
    ZiTestResidue::addShm(name);
  }

  bool open()
  {
    Ztc::Ring creator{ZiRingParams{name, ReqRingSize}.timeout(1)};
    if (creator.open(Ztc::Ring::Write) != Zu::OK ||
	creator.reset() != Zu::OK)
      return false;
    creator.close();
    return ring.open(Ztc::Ring::Read) == Zu::OK &&
      ring.attach() == Zu::OK;
  }

  bool read(Ztc::Request &request)
  {
    const void *ptr = ring.shift();
    if (!ptr) return false;
    unsigned size = Ztc::ringSize(ptr);
    auto message = Ztc::msg(static_cast<const Ztc::Hdr *>(ptr));
    auto request_ = message &&
	message->body_type() == Ztc::fbs::Body::Request ?
	message->body_as_Request() : nullptr;
    if (request_) request = ZfbStruct::ctor<Ztc::Request>(request_);
    ring.shift2(size);
    return request_;
  }

  bool empty()
  {
    const void *ptr = ring.tryShift();
    if (!ptr) return true;
    ring.shift2(Ztc::ringSize(ptr));
    return false;
  }

  ~ReqRing()
  {
    if (ring.rdrID() >= 0) ring.detach();
    ring.close();
  }

  Zi::Name	name;
  Ztc::Ring	ring;
};

static Ztc::AgentCf agentCf(unsigned fanoutBatch = 64)
{
  Ztc::AgentCf cf;
  cf.fanoutBatch = fanoutBatch;
  return cf;
}

struct RouteHarness {
  RouteHarness(unsigned fanoutBatch = 64) :
    cf{agentCf(fanoutBatch)}, state{&agent, cf, env("unused", "unused")}
  {
    state.mx = new ZiMultiplex{Ztc::mxParams(cf)};
    started = state.mx->start();
    state.routeCxnGen = 1;
  }

  template <typename L>
  void route(L &&l)
  {
    ZmBlock<>{}([this, l = ZuFwd<L>(l)](auto wake) mutable {
      state.mx->run([
	l = ZuMv(l), wake = ZuMv(wake)
      ]() mutable { l(); wake(); }, cf.routeThread);
    });
  }

  void drain()
  {
    route([this]() {
      Ztc::fenceRoute(&state, 0);
      while (state.apps.count_()) {
	Ztc::Agent_::App *app = nullptr;
	{
	  auto i = state.apps.iter();
	  app = i();
	}
	if (!app) break;
	Ztc::delApp(&state, app);
      }
    });
  }

  ~RouteHarness()
  {
    if (started) {
      drain();
      state.mx->stop();
    }
    delete state.mx;
    state.mx = nullptr;
  }

  Ztc::AgentCf		cf;
  Ztc::Agent		agent;
  Ztc::Agent_::State	state;
  bool			started = false;
};

static ZmRef<ZiIOBuf> appFrame(
    const ZuID &id, uint64_t seqNo, uint32_t version = Z_VERSION)
{
  Zfb::IOBuilder fbb{
    Ztc::frameBuf(ZmRef<ZiIOBuf>{new Ztc::Agent_::Frame})};
  Ztc::AppTelemetry data;
  data.ztcver = version;
  auto id_ = fbb.CreateString(id.data(), id.length());
  auto value = ZfbStruct::save(fbb, data);
  auto telemetry = Ztc::fbs::CreateTelemetry(
    fbb, id_, seqNo, Ztc::fbs::TelemetryBody::AppTelemetry,
    value.Union());
  fbb.Finish(Ztc::fbs::CreateMsg(
    fbb, Ztc::fbs::Body::Telemetry, telemetry.Union()));
  return Ztc::saveHdr(fbb);
}

static void announce(
    RouteHarness &harness, const ZuID &id, uint64_t seqNo = 0)
{
  auto frame = appFrame(id, seqNo);
  harness.route([&harness, frame = ZuMv(frame)]() {
    auto message = Ztc::msg(frame->ptr<Ztc::Hdr>());
    Ztc::discoverApp(&harness.state, message->body_as_Telemetry());
  });
}

static Ztc::Request request(
    uint64_t seqNo, ZuCSpan id = {}, uint32_t interval = 0,
    ZuCSpan filter = "*")
{
  Ztc::Request request;
  request.filter = filter;
  request.id = id;
  request.seqNo = seqNo;
  request.interval = interval;
  request.group = uint8_t(Ztc::fbs::Group::App);
  request.subscribe = true;
  return request;
}

static void admit(RouteHarness &harness, const Ztc::Request &request)
{
  auto frame = Ztc::requestFrame(request);
  harness.route([&harness, frame = ZuMv(frame)]() {
    Ztc::admitReq(&harness.state, frame, 1);
  });
}

} // ZtcAgentTest_

using namespace ZtcAgentTest_;

static void setEnv(const char *name, ZuCSpan value)
{
  ZtString<> text{value};
#ifdef _WIN32
  _putenv_s(name, text.data());
#else
  setenv(name, text.data(), 1);
#endif
}

static Ztc::AgentEnv env(
    ZuCSpan ring = "ztc-agent-test", ZuCSpan pidDir = "ztc-agent-test")
{
  return {
    .token = "test-token",
    .ring = ring,
    .pidDir = pidDir,
    .enrollURL = "https://localhost:443/v1/enroll"
  };
}

static void initialAppSnapshot()
{
  ZuTestScope(initial_app_snapshot);
  RouteHarness harness;
  ReqRing a{"initial-a"};
  ReqRing b{"initial-b"};
  ReqRing late{"initial-late"};
  ZuCheck(harness.started && a.open() && b.open() && late.open());
  announce(harness, a.name);
  announce(harness, b.name);
  admit(harness, request(10));

  unsigned legs = 0;
  harness.route([&]() {
    auto req = harness.state.reqs.findPtr(uint64_t{10});
    legs = req ? req->legs.count_() : 0;
  });
  Ztc::Request aReq, bReq;
  ZuCheck(legs == 2 && a.read(aReq) && b.read(bReq));
  ZuCheck(aReq.seqNo != bReq.seqNo && aReq.filter == "*");

  announce(harness, late.name);
  harness.route([&]() {
    auto req = harness.state.reqs.findPtr(uint64_t{10});
    legs = req ? req->legs.count_() : 0;
  });
  ZuCheck(legs == 2 && late.empty());

  RouteHarness empty;
  admit(empty, request(11));
  unsigned requests = 1;
  empty.route([&]() { requests = empty.state.reqs.count_(); });
  ZuCheck(!requests);
}

static void requestSelection()
{
  ZuTestScope(request_selection);
  RouteHarness harness;
  ReqRing a{"select-a"};
  ReqRing b{"select-b"};
  ZuCheck(harness.started && a.open() && b.open());
  announce(harness, a.name);
  announce(harness, b.name);

  admit(harness, request(20, a.name, 0, "role=primary"));
  Ztc::Request selected;
  ZuCheck(a.read(selected) && b.empty());
  ZuCheck(selected.id == a.name && selected.filter == "role=primary" &&
    selected.seqNo == 1);

  harness.route([&]() {
    if (auto req = harness.state.reqs.findPtr(uint64_t{20}))
      Ztc::delReq(&harness.state, req);
  });
  Ztc::Request cancel;
  ZuCheck(a.read(cancel) && !cancel.subscribe && cancel.seqNo == 1);

  admit(harness, request(21, {}, 0, "publisher-filter"));
  Ztc::Request aReq, bReq;
  ZuCheck(a.read(aReq) && b.read(bReq));
  ZuCheck(aReq.filter == "publisher-filter" &&
    bReq.filter == "publisher-filter" && aReq.id == ZuID{} &&
    bReq.id == ZuID{});
}

static void fanoutBatching()
{
  ZuTestScope(fanout_batching);
  RouteHarness harness{1};
  ReqRing a{"batch-a"};
  ReqRing b{"batch-b"};
  ReqRing c{"batch-c"};
  ZuCheck(harness.started && a.open() && b.open() && c.open());
  announce(harness, a.name);
  announce(harness, b.name);
  announce(harness, c.name);

  auto frame = Ztc::requestFrame(request(30));
  unsigned pending = 0;
  unsigned sent = 0;
  harness.route([&]() {
    Ztc::admitReq(&harness.state, frame, 1);
    auto req = harness.state.reqs.findPtr(uint64_t{30});
    pending = req ? req->pending.count_() : 0;
    sent = req ? req->legs.count_() : 0;
  });
  ZuCheck(pending == 2 && sent == 1);

  // Two continuation turns complete the remaining bounded fan-out.
  harness.route([]() { });
  harness.route([]() { });
  harness.route([&]() {
    auto req = harness.state.reqs.findPtr(uint64_t{30});
    pending = req ? req->pending.count_() : UINT_MAX;
    sent = req ? req->legs.count_() : 0;
  });
  Ztc::Request aReq, bReq, cReq;
  ZuCheck(!pending && sent == 3 &&
    a.read(aReq) && b.read(bReq) && c.read(cReq));
  uint64_t sum = aReq.seqNo + bReq.seqNo + cReq.seqNo;
  ZuCheck(sum == 6 && aReq.seqNo && bReq.seqNo && cReq.seqNo &&
    aReq.seqNo != bReq.seqNo && aReq.seqNo != cReq.seqNo &&
    bReq.seqNo != cReq.seqNo);
}

static void periodicLateApp()
{
  ZuTestScope(periodic_late_app);
  RouteHarness harness;
  ReqRing a{"periodic-a"};
  ReqRing b{"periodic-b"};
  ZuCheck(harness.started && a.open() && b.open());

  admit(harness, request(40, {}, 1));
  unsigned requests = 0;
  harness.route([&]() { requests = harness.state.reqs.count_(); });
  ZuCheck(requests == 1);
  announce(harness, a.name);
  harness.route([]() { });
  Ztc::Request first;
  ZuCheck(a.read(first));

  Ztc::Request unsubscribe = request(40, a.name, 1);
  unsubscribe.subscribe = false;
  admit(harness, unsubscribe);
  Ztc::Request cancelled;
  ZuCheck(a.read(cancelled) && !cancelled.subscribe);
  announce(harness, a.name, 0);
  harness.route([]() { });
  ZuCheck(a.empty());

  announce(harness, b.name);
  admit(harness, request(41, b.name, 1));
  Ztc::Request beforeRestart;
  ZuCheck(b.read(beforeRestart));
  announce(harness, b.name, 0);
  harness.route([]() { });
  Ztc::Request afterRestart;
  ZuCheck(b.read(afterRestart));
  ZuCheck(afterRestart.seqNo > beforeRestart.seqNo &&
    afterRestart.id == b.name);
}

static void routingCleanup()
{
  ZuTestScope(routing_cleanup);
  RouteHarness harness;
  ReqRing a{"cleanup-a"};
  ReqRing b{"cleanup-b"};
  ZuCheck(harness.started && a.open() && b.open());
  announce(harness, a.name);
  announce(harness, b.name);
  admit(harness, request(50, {}, 1));
  Ztc::Request aReq, bReq;
  ZuCheck(a.read(aReq) && b.read(bReq));

  Ztc::Request unsubscribe = request(50, a.name, 1);
  unsubscribe.subscribe = false;
  admit(harness, unsubscribe);
  Ztc::Request aCancel;
  ZuCheck(a.read(aCancel) && !aCancel.subscribe);
  unsigned requests = 0;
  unsigned excluded = 0;
  harness.route([&]() {
    auto req = harness.state.reqs.findPtr(uint64_t{50});
    requests = harness.state.reqs.count_();
    excluded = req ? req->excl.count_() : 0;
    Ztc::fenceRoute(&harness.state, 1);
  });
  Ztc::Request bCancel;
  ZuCheck(requests == 1 && excluded == 1 &&
    b.read(bCancel) && !bCancel.subscribe);
  harness.route([&]() { requests = harness.state.reqs.count_(); });
  ZuCheck(!requests);
}

static void sequenceRouting()
{
  ZuTestScope(sequence_routing);
  RouteHarness harness;
  ReqRing app{"sequence-app"};
  ZuCheck(harness.started && app.open());
  announce(harness, app.name);
  admit(harness, request(0, app.name));
  admit(harness, request(2, app.name));
  admit(harness, request(1, app.name));
  admit(harness, request(2, app.name));

  unsigned requests = 0;
  uint64_t watermark = 0;
  harness.route([&]() {
    requests = harness.state.reqs.count_();
    watermark = harness.state.minHubSeqNo;
  });
  Ztc::Request zero, gap;
  ZuCheck(requests == 2 && watermark == 3 &&
    app.read(zero) && app.read(gap));
  ZuCheck(zero.seqNo == 1 && gap.seqNo == 2 && app.empty());

  harness.route([&]() { Ztc::fenceRoute(&harness.state, 1); });
  Ztc::Request zeroCancel, gapCancel;
  ZuCheck(app.read(zeroCancel) && app.read(gapCancel) &&
    !zeroCancel.subscribe && !gapCancel.subscribe);

  bool lastAdded = false;
  bool sentinelAdded = true;
  uint64_t lastSeqNo = 0;
  harness.route([&]() {
    auto req = new Ztc::Agent_::Reqs::Node{request(3, app.name), 2};
    harness.state.reqs.addNode(req);
    harness.state.nextSeqNo = ZuCmp<uint64_t>::null() - 1;
    auto app_ = harness.state.apps.findPtr(app.name);
    lastAdded = Ztc::addLeg(&harness.state, req, app_);
    {
      auto i = req->pending.iter();
      if (auto leg = i()) lastSeqNo = leg->seqNo;
    }
    sentinelAdded = Ztc::addLeg(&harness.state, req, app_);
    Ztc::delReq(&harness.state, req);
  });
  ZuCheck(lastAdded && !sentinelAdded &&
    lastSeqNo == ZuCmp<uint64_t>::null() - 1);
}

static void invalidInput()
{
  ZuTestScope(invalid_input);
  RouteHarness harness;
  auto telemetry = appFrame("invalid", 0);
  harness.route([&]() { Ztc::admitReq(&harness.state, telemetry, 1); });

  ZmRef<ZiIOBuf> malformed = new Ztc::Agent_::Frame;
  ZuCheck(malformed->alloc(sizeof(Ztc::Hdr)));
  malformed->length = sizeof(Ztc::Hdr);
  malformed->ptr<Ztc::Hdr>()->length = 0;
  harness.route([&]() { Ztc::admitReq(&harness.state, malformed, 1); });
  unsigned requests = 1;
  harness.route([&]() { requests = harness.state.reqs.count_(); });
  ZuCheck(!requests);
}

static void enrollmentRetry()
{
  ZuTestScope(enrollment_retry);
  RouteHarness harness;
  bool prepared = false;
  harness.route([&]() {
    prepared = Ztc::prepareEnrollment(&harness.state);
  });
  ZuCheck(prepared && harness.cf.enrollRetry == 1);

  auto &pending = harness.state.enrollment.p<Ztc::Agent_::PendingEnroll>();
  auto key = pending.key;
  Ztc::Agent_::CSR csr = pending.csr;
  Ztc::Agent_::Nonce nonce = pending.nonce;
  Ztc::Agent_::EnrollBody body = pending.body;
  Ztc::Agent_::EnrollClient client{&harness.state};
  harness.state.enrollClient = &client;
  Ztc::Agent_::EnrollReq_ request;
  request.state = &harness.state;
  request.client = &client;
  Zhttp::Result result;
  result.code = Zhttp::ResultCode::Indeterminate;
  client.completed(&request, result);
  harness.route([]() { });

  bool identical = false;
  bool armed = false;
  harness.route([&]() {
    auto &retry =
      harness.state.enrollment.p<Ztc::Agent_::PendingEnroll>();
    identical = retry.key == key && retry.csr == csr &&
      retry.nonce == nonce && retry.body == body &&
      harness.state.phase == Ztc::Agent_::Phase::Enrolling;
    armed = bool(client.retryTimer);
    client.cancelRetry();
    harness.state.enrollClient = nullptr;
    harness.state.enrollment.null();
    harness.state.phase = Ztc::Agent_::Phase::Down;
  });
  ZuCheck(identical && armed);
}

static void enrollmentReconnect()
{
  ZuTestScope(enrollment_reconnect);
  RouteHarness harness;
  harness.route([&]() {
    Ztc::Agent_::Identity identity{
      .host = "localhost",
      .agentID = "agent",
      .port = 443
    };
    harness.state.enrollment.p<Ztc::Agent_::Identity>(ZuMv(identity));
    harness.state.phase = Ztc::Agent_::Phase::Up;
  });
  admit(harness, request(70, {}, 1));
  unsigned before = 0;
  bool retained = false;
  harness.route([&]() {
    before = harness.state.reqs.count_();
    Ztc::fenceRoute(&harness.state, 1);
    harness.state.routeCxnGen = 2;
    harness.state.minHubSeqNo = 0;
    retained = harness.state.enrollment.is<Ztc::Agent_::Identity>() &&
	harness.state.enrollment.p<Ztc::Agent_::Identity>().host ==
	  "localhost";
  });
  unsigned after = 1;
  harness.route([&]() { after = harness.state.reqs.count_(); });
  auto initial = harness.state.reconn.initial();
  auto next = harness.state.reconn.backoff(initial);
  ZuCheck(before == 1 && !after && retained && next >= initial);
}

static void tlsAuthRejection()
{
  ZuTestScope(tls_auth_rejection);
  RouteHarness harness;
  bool callback = false;
  bool callbackOK = true;
  harness.route([&]() {
    ZuCheck(Ztc::prepareEnrollment(&harness.state));
    harness.state.startPending = true;
    harness.state.startFn = {[&](bool ok) {
      callback = true;
      callbackOK = ok;
    }};
  });

  Ztc::Agent_::EnrollClient client{&harness.state};
  harness.state.enrollClient = &client;
  Ztc::Agent_::EnrollReq_ request;
  request.state = &harness.state;
  request.client = &client;
  request.status = 302;
  request.contentType = true;
  request.cacheControl = true;
  client.completed(&request, Zhttp::Result{});
  harness.route([]() { });

  bool rejected = false;
  harness.route([&]() {
    rejected = callback && !callbackOK &&
	harness.state.phase == Ztc::Agent_::Phase::Down &&
	!client.retryTimer;
    harness.state.enrollClient = nullptr;
    harness.state.enrollment.null();
  });
  ZuCheck(rejected);
}

static void hubOverload()
{
  ZuTestScope(hub_overload);
  RouteHarness harness;
  harness.state.currentCxnGen = 1;
  harness.state.reqPendingFrames = harness.cf.reqFrames - 1;
  harness.state.reqPendingBytes = 0;
  ZuCheck(Ztc::admitHubFrame(&harness.state, harness.cf.maxFrame));
  ZuCheck(!Ztc::admitHubFrame(&harness.state, sizeof(Ztc::Hdr)));
  ZuCheck(harness.state.reqPendingFrames == harness.cf.reqFrames &&
    harness.state.reqPendingBytes == harness.cf.maxFrame);

  harness.state.reqPendingFrames = 0;
  harness.state.reqPendingBytes = harness.cf.reqBytes - sizeof(Ztc::Hdr) + 1;
  ZuCheck(!Ztc::admitHubFrame(&harness.state, sizeof(Ztc::Hdr)));
  auto initial = harness.state.reconn.initial();
  auto next = harness.state.reconn.backoff(initial);
  ZuCheck(initial.sec() == harness.cf.reconnMin && next >= initial &&
    next.sec() <= harness.cf.reconnMax);
}

static void config()
{
  ZuTestScope(config);
  Ztc::AgentCf cf;
  ZuCheck(cf.maxFrame == Ztc::AppCf::DefltMaxFrame);
  ZuCheck(cf.telSize >= cf.maxFrame + Zm::CacheLineSize);
  ZuCheck(Ztc::validHubHost("localhost") &&
    Ztc::validHubHost("hub.example.test") &&
    !Ztc::validHubHost("127.0.0.1") &&
    !Ztc::validHubHost("-hub.example") &&
    !Ztc::validHubHost("hub..example") &&
    !Ztc::validHubHost("hub.example-"));

  Ztc::Agent invalid;
  auto invalidCf = cf;
  invalidCf.telFrames = 0;
  ZuCheck(!invalid.init(invalidCf, env()));

  invalidCf = cf;
  invalidCf.telBytes = cf.maxFrame - 1;
  ZuCheck(!invalid.init(invalidCf, env()));

  invalidCf = cf;
  invalidCf.telFrames = UINT_MAX;
  ZuCheck(!invalid.init(invalidCf, env()));

  auto invalidEnv = env();
  invalidEnv.token = {};
  ZuCheck(!invalid.init(cf, ZuMv(invalidEnv)));

  invalidEnv = env();
  invalidEnv.enrollURL = "http://localhost:443/v1/enroll";
  ZuCheck(!invalid.init(cf, ZuMv(invalidEnv)));

  invalidEnv = env();
  invalidEnv.enrollURL = "https://localhost:443/v1/enroll#fragment";
  ZuCheck(!invalid.init(cf, ZuMv(invalidEnv)));

  Ztc::Agent valid;
  ZuCheck(valid.init(cf, env()));
  valid.final();

  cf.maxFrame = Ztc::AppCf::DefltMaxFrame + 1;
  cf.telBytes = cf.reqBytes = cf.telSize = cf.maxFrame + Zm::CacheLineSize;
  Ztc::Agent larger;
  ZuCheck(larger.init(cf, env()));
  larger.final();
}

static void appRegistry()
{
  ZuTestScope(pid_exclusion);
  Zi::Name ring = ZiTestResidue::uniqueName("telemetry");
  Zi::Name pidDir = ZiTestResidue::uniqueName("registry");
  ZiTestResidue::addShm(ring);
  Zi::Path dir = ZiFile::append(ZiFile::tmpDir(), pidDir);
  ZuCheck(ZiFile::mkdir(dir) == Zi::OK);

  Ztc::AgentCf cf;
  cf.telTimeout = 1;
  Ztc::Agent agent;
  ZuCheck(agent.init(cf, env(ring, pidDir)));
  agent.start(Ztc::Agent::CtrlFn{});
  Zi::Path pidName;
  pidName << pidDir << "/ztcagent.pid";
  ZuCheck(ZiStat{ZiFile::append(ZiFile::tmpDir(), pidName)}.exists());

  Ztc::Ring writer{ZiRingParams{ring, 0}};
  ZuCheck(writer.open(Ztc::Ring::Write) == Zu::OK);
  writer.close();

  Ztc::Agent excluded;
  ZuCheck(excluded.init(cf, env(ring, pidDir)));
  ZuCheck(!excluded.start());
  excluded.final();

  ZuCheck(agent.stop());
  agent.final();
  ZuCheck(!ZiStat{ZiFile::append(ZiFile::tmpDir(), pidName)}.exists());
  ZuCheck(ZiFile::rmdir(dir) == Zi::OK);
}

static void startupDiscovery()
{
  ZuTestScope(app_registry);
  Zi::Name ring = ZiTestResidue::uniqueName("scan-telemetry");
  Zi::Name pidDir = ZiTestResidue::uniqueName("scan-registry");
  Zi::Name appID = ZiTestResidue::uniqueName("publisher");
  ZiTestResidue::addShm(ring);
  ZiTestResidue::addShm(appID);
  Zi::Path dir = ZiFile::append(ZiFile::tmpDir(), pidDir);
  ZuCheck(ZiFile::mkdir(dir) == Zi::OK);

  Ztc::AgentCf agentCf;
  Ztc::Ring creator{ZiRingParams{ring, agentCf.telSize}.timeout(1)};
  ZuCheck(creator.open(Ztc::Ring::Write) == Zu::OK);
  ZuCheck(creator.reset() == Zu::OK);
  creator.close();
  Ztc::Ring reader{ZiRingParams{ring, agentCf.telSize}.timeout(1)};
  ZuCheck(reader.open(Ztc::Ring::Read) == Zu::OK);
  ZuCheck(reader.attach() == Zu::OK);

  setEnv("ZTC_RING", ring);
  setEnv("ZTC_DIR", pidDir);
  Ztc::AppCf appCf;
  appCf.id = appID;
  Ztc::App app;
  ZuCheck(app.init(appCf));
  ZuCheck(app.start());

  Ztc::Agent agent;
  ZuCheck(agent.init(agentCf, env(ring, pidDir)));
  agent.start(Ztc::Agent::CtrlFn{});
  Zi::Path agentPID;
  agentPID << pidDir << "/ztcagent.pid";
  ZuCheck(ZiStat{ZiFile::append(ZiFile::tmpDir(), agentPID)}.exists());
  bool found = false;
  for (unsigned i = 0; i < 8 && !found; ++i) {
    auto ptr = reader.shift();
    if (!ptr) continue;
    unsigned size = Ztc::ringSize(ptr);
    auto message = Ztc::msg(static_cast<const Ztc::Hdr *>(ptr));
    auto telemetry = message &&
	message->body_type() == Ztc::fbs::Body::Telemetry ?
	message->body_as_Telemetry() : nullptr;
    found = telemetry && telemetry->seqNo() &&
	Zfb::Load::str(telemetry->id()) == appID &&
	telemetry->value_type() == Ztc::fbs::TelemetryBody::AppTelemetry;
    reader.shift2(size);
  }
  ZuCheck(found);

  ZuCheck(agent.stop());
  agent.final();
  ZuCheck(app.stop());
  app.final();
  reader.detach();
  reader.close();
  ZuCheck(ZiFile::rmdir(dir) == Zi::OK);
}

static bool readSnapshot(Ztc::Ring &reader, const ZuID &id)
{
  for (unsigned i = 0; i < 8; ++i) {
    auto ptr = reader.shift();
    if (!ptr) continue;
    unsigned size = Ztc::ringSize(ptr);
    auto message = Ztc::msg(static_cast<const Ztc::Hdr *>(ptr));
    auto telemetry = message &&
	message->body_type() == Ztc::fbs::Body::Telemetry ?
	message->body_as_Telemetry() : nullptr;
    bool found = telemetry && telemetry->seqNo() &&
	Zfb::Load::str(telemetry->id()) == id &&
	telemetry->value_type() == Ztc::fbs::TelemetryBody::AppTelemetry;
    reader.shift2(size);
    if (found) return true;
  }
  return false;
}

static void ephemeralRestart()
{
  ZuTestScope(ephemeral_restart);
  Zi::Name ring = ZiTestResidue::uniqueName("restart-telemetry");
  Zi::Name pidDir = ZiTestResidue::uniqueName("restart-registry");
  Zi::Name appID = ZiTestResidue::uniqueName("restart-publisher");
  ZiTestResidue::addShm(ring);
  ZiTestResidue::addShm(appID);
  Zi::Path dir = ZiFile::append(ZiFile::tmpDir(), pidDir);
  ZuCheck(ZiFile::mkdir(dir) == Zi::OK);

  Ztc::AgentCf agentCf;
  Ztc::Ring creator{ZiRingParams{ring, agentCf.telSize}.timeout(1)};
  ZuCheck(creator.open(Ztc::Ring::Write) == Zu::OK);
  ZuCheck(creator.reset() == Zu::OK);
  creator.close();
  Ztc::Ring reader{ZiRingParams{ring, agentCf.telSize}.timeout(1)};
  ZuCheck(reader.open(Ztc::Ring::Read) == Zu::OK &&
    reader.attach() == Zu::OK);

  setEnv("ZTC_RING", ring);
  setEnv("ZTC_DIR", pidDir);
  Ztc::AppCf appCf;
  appCf.id = appID;
  Ztc::App app;
  ZuCheck(app.init(appCf) && app.start());

  Ztc::Agent first;
  ZuCheck(first.init(agentCf, env(ring, pidDir)));
  first.start(Ztc::Agent::CtrlFn{});
  ZuCheck(readSnapshot(reader, appID));
  ZuCheck(first.stop());
  first.final();

  Ztc::Agent second;
  ZuCheck(second.init(agentCf, env(ring, pidDir)));
  second.start(Ztc::Agent::CtrlFn{});
  ZuCheck(readSnapshot(reader, appID));
  ZuCheck(second.stop());
  second.final();

  Zi::Path pidName = ZiFile::append(pidDir, Zi::Path{"ztcagent.pid"});
  ZuCheck(!ZiStat{ZiFile::append(ZiFile::tmpDir(), pidName)}.exists());
  ZuCheck(app.stop());
  app.final();
  reader.detach();
  reader.close();
  ZuCheck(ZiFile::rmdir(dir) == Zi::OK);
}

static void configSources()
{
  ZuTestScope(config_sources);
  Zi::Name ring = ZiTestResidue::uniqueName("size-mismatch");
  Zi::Name pidDir = ZiTestResidue::uniqueName("size-registry");
  ZiTestResidue::addShm(ring);
  Zi::Path dir = ZiFile::append(ZiFile::tmpDir(), pidDir);
  ZuCheck(ZiFile::mkdir(dir) == Zi::OK);

  Ztc::AgentCf cf;
  Ztc::Ring creator{ZiRingParams{ring, cf.telSize << 1U}};
  ZuCheck(creator.open(Ztc::Ring::Write) == Zu::OK);
  creator.close();

  Ztc::Agent agent;
  ZuCheck(agent.init(cf, env(ring, pidDir)));
  ZuCheck(!agent.start());
  agent.final();

  Zi::Path pidName = ZiFile::append(pidDir, Zi::Path{"ztcagent.pid"});
  ZuCheck(!ZiStat{ZiFile::append(ZiFile::tmpDir(), pidName)}.exists());
  ZuCheck(ZiFile::rmdir(dir) == Zi::OK);
}

int main()
{
  ZiTestResidue::init("ZtcAgentTest");
  ZiLog::init("ZtcAgentTest");
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  ZuTestMain();
  ZuTestCall(config);
  ZuTestCall(initialAppSnapshot);
  ZuTestCall(requestSelection);
  ZuTestCall(fanoutBatching);
  ZuTestCall(periodicLateApp);
  ZuTestCall(routingCleanup);
  ZuTestCall(sequenceRouting);
  ZuTestCall(invalidInput);
  ZuTestCall(enrollmentRetry);
  ZuTestCall(enrollmentReconnect);
  ZuTestCall(tlsAuthRejection);
  ZuTestCall(hubOverload);
  ZuTestCall(appRegistry);
  ZuTestCall(startupDiscovery);
  ZuTestCall(ephemeralRestart);
  ZuTestCall(configSources);
  ZiLog::stop();
}
