//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Production-wire fixture for the ztchub multi-process test.

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifdef __linux__
#include <sys/socket.h>
#endif

#include <iostream>

#include <zlib/Zfb.hh>
#include <zlib/ZfbStruct.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZmTrap.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZfCLI.hh>
#include <zlib/ZfURI.hh>
#include <zlib/ZhttpURL.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZtcAppTypes.hh>
#include <zlib/ZtcApp.hh>
#include <zlib/ZtcFB.hh>
#include <zlib/ztchub_daemon.hh>
#include <zlib/ZtcMsg.hh>
#include <zlib/ZtcRing.hh>
#include <zlib/ZumURI.hh>
#include <zlib/Zws.hh>

#include "ZtcControlled.hh"

namespace ZtcHubWireTest_ {

struct Object { unsigned value = 0; };
struct BaseObject : public ZmHeap<"ZDash.BaseHeap", Object>, public Object { };
struct LateObject : public ZmHeap<"ZDash.LiveHeap", Object>, public Object { };

static ZmSemaphore *signalDone = nullptr;
static void interrupted() { if (signalDone) signalDone->post(); }

struct Options {
  ZtString<> mode;
  ZtString<> issuerURL;
  ZtString<> deviceID;
  ZtString<> wssURL;
  ZtString<> caPath;
  ZtString<> cookie;
  ZtString<> origin;
  ZtString<> group = "App";
  Ztc::fbs::Group groupID = Ztc::fbs::Group::App;
  uint32_t telemetry = 1;
  uint32_t expect = 1;
  uint32_t stallMS = 0;
  uint32_t payload = 0;
  uint32_t controlBurst = 1;
  uint32_t snapshots = 1;
  uint32_t expectPublishers = 0;
  bool inventory = false;
  bool oneShot = false;
  bool waitShutdown = false;
  bool controlled = false;
  bool changes = false;
  bool sourceErrors = false;
  bool help = false;
};

ZfStruct(, (Options, CLI),
  (((mode), (CLI::Long<"mode">)),				String),
  (((issuerURL), (CLI::Long<"issuer">)),			String),
  (((deviceID), (CLI::Long<"device-id">)),			String),
  (((wssURL), (CLI::Long<"wss">)),				String),
  (((caPath), (CLI::Long<"ca">)),				String),
  (((cookie), (CLI::Long<"cookie">)),				String),
  (((origin), (CLI::Long<"origin">)),				String),
  (((group), (CLI::Long<"group">)),				String),
  (((telemetry), (CLI::Long<"telemetry">)),			UInt32),
  (((expect), (CLI::Long<"expect">)),				UInt32),
  (((stallMS), (CLI::Long<"stall-ms">)),			UInt32),
  (((payload), (CLI::Long<"payload">)),				UInt32),
  (((controlBurst), (CLI::Long<"control-burst">)),		UInt32),
  (((snapshots), (CLI::Long<"snapshots">)),			UInt32),
  (((expectPublishers), (CLI::Long<"expect-publishers">)),	UInt32),
  (((inventory), (CLI::Long<"inventory">)),			Bool),
  (((oneShot), (CLI::Long<"one-shot">)),			Bool),
  (((waitShutdown), (CLI::Long<"wait-shutdown">)),		Bool),
  (((controlled), (CLI::Long<"controlled">)),			Bool),
  (((changes), (CLI::Long<"changes">)),				Bool),
  (((sourceErrors), (CLI::Long<"source-errors">)),		Bool),
  (((help), (CLI::Flag<'h'>, CLI::Long<"help">)),		Bool));

using Frame = ZtArray<uint8_t,
  ZtArrayHeapID<"Ztc.Hub.Wire.Frame">>;

static bool bearer(ZtString<> &value)
{
  const char *token = ::getenv("ZTC_ACCESS_TOKEN");
  if (!token || !*token) return false;
  value = "Bearer ";
  value << token;
  return true;
}

static Frame finish(Zfb::Builder &builder)
{
  Frame frame;
  frame.length(unsigned(builder.GetSize()));
  memcpy(frame.data(), builder.GetBufferPointer(), frame.length());
  return frame;
}

static Frame subscribe(ZuCSpan deviceID, uint32_t interval,
    Ztc::fbs::Group group)
{
  Zfb::Builder builder;
  auto device = builder.CreateString(deviceID.data(), deviceID.length());
  auto filter = builder.CreateString("*");
  auto request = Ztc::fbs::CreateRequest(builder, 0,
    group, filter, interval, true);
  builder.Finish(Ztc::saveMsg(
    builder, Ztc::fbs::Body::Request, request.Union(), 1, device));
  return finish(builder);
}

static Frame ack(uint64_t seqNo)
{
  Zfb::Builder builder;
  auto value = ZfbStruct::save(builder, Ztc::Ack{ZuID{"wire"}, seqNo, 1000});
  builder.Finish(Ztc::saveMsg(
    builder, Ztc::fbs::Body::Ack, value.Union()));
  return finish(builder);
}

static Frame telemetry(uint64_t seqNo, uint32_t payload)
{
  Zfb::Builder builder;
  ZtString<> id;
  id.length(payload ? payload : 4);
  memset(id.data(), 'x', id.length());
  if (!payload) memcpy(id.data(), "wire", 4);
  auto idOffset = builder.CreateString(id.data(), id.length());
  Ztc::AppTelemetry value;
  value.version = "wire";
  value.role = "wire";
  auto data = ZfbStruct::save(builder, value);
  auto tel = Ztc::saveTelemetry(
    builder, idOffset, seqNo, Ztc::fbs::TelemetryBody::AppTelemetry,
    data.Union());
  builder.Finish(Ztc::saveMsg(
    builder, Ztc::fbs::Body::Telemetry, tel.Union()));
  return finish(builder);
}

// Inject an empty unsolicited snapshot completion on the public telemetry ring.
static bool emptySnapshot(const Ztc::AppCf &cf)
{
  using MsgFrame = ZiIOBufAlloc<1024, 1U<<20, "Ztc.Wire.Snapshot">;
  Zfb::IOBuilder builder{Ztc::frameBuf(ZmRef<ZiIOBuf>{new MsgFrame})};
  auto eos = ZfbStruct::save(builder, Ztc::EOS{cf.id, 0});
  builder.Finish(Ztc::saveMsg(builder, Ztc::fbs::Body::EOS, eos.Union()));
  auto frame = Ztc::saveHdr(builder);
  Ztc::Ring ring;
  auto name = ::getenv("ZTC_RING");
  ring.init(ZiRingParams{name ? name : "ztc", 0}.initial(cf.telRingSize).timeout(1));
  if (ring.open(Ztc::Ring::Write) != Zu::OK) return false;
  auto slot = ring.push(frame->length);
  bool ok = bool(slot);
  if (slot) {
    memcpy(slot, frame->data(), frame->length);
    ring.push2(slot, frame->length);
  }
  ring.close();
  return ok;
}

static Frame unsubscribe(ZuCSpan deviceID, uint64_t subID, bool inventory)
{
  Zfb::Builder builder;
  auto device = builder.CreateString(deviceID.data(), deviceID.length());
  auto filter = inventory ? builder.CreateString("*") :
    flatbuffers::Offset<flatbuffers::String>{};
  auto value = Ztc::fbs::CreateRequest(builder, 0,
    inventory ? Ztc::fbs::Group::App : Ztc::fbs::Group::Heap,
    filter, 0, false);
  builder.Finish(Ztc::saveMsg(
    builder, Ztc::fbs::Body::Request, value.Union(), subID, device));
  return finish(builder);
}

static void sendFrame(auto &link, Frame frame)
{
  link.txStream([frame = ZuMv(frame)](auto &tx) mutable {
    tx << ZuBSpan{frame.data(), frame.length()};
    tx.flush();
  }, Zws::Opcode::Binary);
}

struct App {
  using Client = Zws::Client<App, Zhttp::H1TLS>;
  using Link = Client::Link;

  Options *options;
  ZmSemaphore *done;
  ZmSemaphore down;
  Frame frame;
  ZtString<> authorization;
  bool agent = false;
  bool failed = false;
  bool subscribed = false;
  bool completed = false;
  bool received = false;
  unsigned telemetryCount = 0;
  unsigned snapshotCount = 0;
  uint64_t baseAllocs = 0;
  bool baseSeen = false;
  bool baseUpdated = false;
  bool lateSeen = false;
  ZtArray<ZtString<>> inventoryDevices;
  ZtArray<ZuID, ZtArrayHeapID<"Ztc.Wire.Publishers">> publishers;
  uint64_t requestSeqNo = 0;
  unsigned requestCount = 0;

  template <typename Link_>
  void connected(Link_ &link, const Zhttp::ConnectedInfo &)
  {
    if (agent)
      std::cout << "agent ready\n" << std::flush;
    if (!agent) {
#ifdef __linux__
      if (options->stallMS) {
        // Keep the overflow case at the socket boundary.  A delayed Rx
        // callback alone still lets the kernel absorb several megabytes.
        int receiveBuffer = 4096;
        (void)::setsockopt(link.cxn()->info().socket, SOL_SOCKET, SO_RCVBUF,
          &receiveBuffer, sizeof(receiveBuffer));
      }
#endif
      ZuCSpan deviceID{options->deviceID};
      if (options->inventory) deviceID = {};
      sendFrame(link, subscribe(deviceID, options->oneShot ? 0 : 1000,
	options->groupID));
      subscribed = true;
    }
  }

  template <typename Link_>
  int messageStart(Link_ &link, Zws::Opcode::T opcode)
  {
    if (!agent && options->stallMS) {
      ZmSemaphore stalled;
      auto delay = ZuTime{int64_t(options->stallMS / 1000),
        int32_t(options->stallMS % 1000) * 1000000};
      options->stallMS = 0;
      (void)stalled.timedwait(Zm::now() + delay);
    }
    if (opcode != Zws::Opcode::Binary) {
      failed = true;
      link.close(Zws::CloseCode::Unsupported);
      return -1;
    }
    frame.length(0);
    return 1;
  }

  template <typename Link_, typename Rx>
  int process(Link_ &link, Rx &rx)
  {
    return Zhttp::bodyEach(rx, [this, &link](ZuSpan<uint8_t> span) {
      if (span.length() > (1U << 20) - frame.length()) {
        failed = true;
        link.close(Zws::CloseCode::TooLarge);
        return false;
      }
      frame << span;
      return true;
    }) ? 1 : -1;
  }

  template <typename Link_>
  int messageEnd(Link_ &link)
  {
    auto msg = Ztc::msg(ZuBSpan{frame.data(), frame.length()});
    if (!msg) {
      failed = true;
      link.close(Zws::CloseCode::InvalidData);
      return -1;
    }
    if (agent) {
      if (!Ztc::Hubd::validAgentMessage(msg) ||
          msg->body_type() != Ztc::fbs::Body::Request) {
        failed = true;
        link.close(Zws::CloseCode::Protocol);
        return -1;
      }
      auto request = msg->body_as_Request();
      requestSeqNo = request->seqNo();
      if (!requestSeqNo) {
        failed = true;
        link.close(Zws::CloseCode::Protocol);
        return -1;
      }
      if (!request->subscribe()) {
        std::cout << "agent unsubscribe " << requestSeqNo << '\n' <<
          std::flush;
        return 1;
      }
      ++requestCount;
      unsigned controlBurst = requestCount == 2 ? options->controlBurst : 1;
      for (unsigned i = 0; i < controlBurst; ++i)
        sendFrame(link, ack(requestSeqNo));
      for (unsigned i = 0; i < options->telemetry; ++i)
        sendFrame(link, telemetry(requestSeqNo, options->payload));
      std::cout << "agent request " << requestSeqNo << " telemetry " <<
        options->telemetry << '\n' << std::flush;
      return 1;
    }
    auto hubMsg = Ztc::fbs::GetMsg(frame.data());
    if (!hubMsg || !hubMsg->body() || hubMsg->subId() != 1) {
      failed = true;
      link.close(Zws::CloseCode::Protocol);
      return -1;
    }
    received = true;
    switch (hubMsg->body_type()) {
      case Ztc::fbs::Body::Ack:
        std::cout << "front ack\n" << std::flush;
        break;
      case Ztc::fbs::Body::Telemetry:
        if (hubMsg->body_as_Telemetry()->value_type() ==
            Ztc::fbs::TelemetryBody::Shutdown) {
          if (!options->waitShutdown || !hubMsg->deviceId() ||
              !hubMsg->agentGen() || !hubMsg->body_as_Telemetry()->id()) {
            failed = true;
            link.close(Zws::CloseCode::Protocol);
            return -1;
          }
          std::cout << "front shutdown\n" << std::flush;
          completed = true;
          break;
        }
        if (options->expectPublishers) {
	  ZuID id{Zfb::Load::str(hubMsg->body_as_Telemetry()->id())};
	  bool found = false;
	  for (const auto &prior: publishers)
	    if (prior == id) { found = true; break; }
	  if (!found) publishers.push(id);
	}
        if (options->groupID == Ztc::fbs::Group::Heap &&
	    hubMsg->body_as_Telemetry()->value_type() !=
	      Ztc::fbs::TelemetryBody::HeapTelemetry) {
	  failed = true;
	  link.close(Zws::CloseCode::Protocol);
	  return -1;
	}
	if (options->changes) {
	  auto data = hubMsg->body_as_Telemetry()->value_as_HeapTelemetry();
	  auto id = Zfb::Load::str(data->id());
	  if (id == "ZDash.BaseHeap") {
	    uint64_t n = data->cacheAllocs() + data->heapAllocs();
	    if (!baseSeen) { baseSeen = true; baseAllocs = n; }
	    else if (n > baseAllocs) baseUpdated = true;
	  } else if (id == "ZDash.LiveHeap") {
	    if (!snapshotCount) {
	      failed = true;
	      link.close(Zws::CloseCode::Protocol);
	      return -1;
	    }
	    lateSeen = true;
	  }
	}
        if (options->inventory) {
          auto telemetry = hubMsg->body_as_Telemetry();
          if (!hubMsg->deviceId() || !hubMsg->deviceId()->size() ||
              !hubMsg->agentGen() || !telemetry || !telemetry->id() ||
              !telemetry->id()->size() || telemetry->seqNo() ||
              telemetry->value_type() !=
                Ztc::fbs::TelemetryBody::AppTelemetry) {
            failed = true;
            link.close(Zws::CloseCode::Protocol);
            return -1;
          }
          ZuCSpan deviceID = Zfb::Load::str(hubMsg->deviceId());
          for (auto &id: inventoryDevices)
            if (id == deviceID) {
              failed = true;
              link.close(Zws::CloseCode::Protocol);
              return -1;
            }
          inventoryDevices.push(deviceID);
        }
        ++telemetryCount;
        std::cout << "front telemetry " << telemetryCount << '\n' <<
          std::flush;
        if (!options->oneShot && !options->waitShutdown && options->snapshots == 1 &&
            telemetryCount >= options->expect) completed = true;
        break;
      case Ztc::fbs::Body::EOS:
        if (options->expectPublishers &&
	    publishers.length() != options->expectPublishers) {
	  failed = true;
	  link.close(Zws::CloseCode::Protocol);
	  return -1;
	}
        if (options->oneShot &&
            (options->inventory ? telemetryCount != options->expect :
	      telemetryCount < options->expect)) {
          failed = true;
          link.close(Zws::CloseCode::Protocol);
          return -1;
        }
        std::cout << "front eos\n" << std::flush;
        ++snapshotCount;
        std::cout << "front snapshot " << snapshotCount << '\n' << std::flush;
        completed = !options->waitShutdown &&
          (options->oneShot || snapshotCount >= options->snapshots);
	if (completed && options->changes &&
	    (!baseSeen || !baseUpdated || !lateSeen)) {
	  failed = true;
	  link.close(Zws::CloseCode::Protocol);
	  return -1;
	}
	if (completed && options->changes)
	  std::cout << "front new heap and updated heap\n" << std::flush;
        break;
      case Ztc::fbs::Body::Error:
        std::cout << "front error " <<
          hubMsg->body_as_Error()->code() << '\n' << std::flush;
        completed = !options->sourceErrors;
        break;
      default:
        failed = true;
        link.close(Zws::CloseCode::Protocol);
        return -1;
    }
    if (completed) {
      if (subscribed) {
        ZuCSpan deviceID{options->deviceID};
        if (options->inventory) deviceID = {};
        sendFrame(link, unsubscribe(deviceID, 1, options->inventory));
      }
      link.close();
    }
    return 1;
  }

  template <typename Link_>
  void connectFailed(Link_ &, bool)
  {
    failed = true;
    done->post();
  }

  template <typename Link_>
  void disconnected(Link_ &, bool)
  {
    if (!agent)
      std::cerr << "front disconnected received=" << received <<
        " completed=" << completed << " failed=" << failed << '\n';
    if (!received) failed = true;
    if (!agent && !completed) failed = true;
    down.post();
    done->post();
  }
};

static ZiMxParams mxParams()
{
  return ZiMxParams().scheduler([](auto &s) {
    s.nThreads(4)
      .thread(1, [](auto &t) { t.isolated(1); })
      .thread(2, [](auto &t) { t.isolated(1); })
      .thread(3, [](auto &t) { t.isolated(1); })
      .thread(4, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2);
}

} // ZtcHubWireTest_

int main(int argc, char **argv)
{
  using namespace ZtcHubWireTest_;
  Options options;
  try {
    argc = ZfCLI::load(options, argc, argv);
  } catch (const ZeException &e) {
    std::cerr << e << '\n';
    return 1;
  }
  if (options.help) {
    std::cout << "Usage: ztchubwiretest --mode=front|agent --issuer=URL "
      "--device-id=ID --wss=URL [--ca=PATH] [--cookie=COOKIE "
      "--origin=ORIGIN] [--telemetry=N] [--expect=N] [--stall-ms=N] "
      "[--payload=N] [--control-burst=N] [--inventory]\n";
    return 0;
  }
  if (options.mode == "publisher") {
    ZmSemaphore done;
    signalDone = &done;
    ZmTrap::sigintFn(interrupted);
    ZmTrap::trap();
    Ztc::AppCf cf;
    cf.id = options.deviceID;
    cf.alertPrefix = options.deviceID;
    ZtcControlled::Sources sources{options.controlled};
    Ztc::App app;
    if (!app.init(cf) || !app.start()) return 1;
    if (options.controlled)
      ZmHeapMgr::init("ZDash.BaseHeap", ZmSelf()->partition(), 0,
	ZmHeapConfig{.cacheSize = 1});
    BaseObject *base = options.controlled ? new BaseObject : nullptr;
    BaseObject *extra = nullptr;
    LateObject *late = nullptr;
    std::cout << "publisher ready\n" << std::flush;
    if (options.controlled) {
      int c;
      while ((c = ::getchar()) != EOF && c != 'q') {
	if (c == 'e') {
	  if (!emptySnapshot(cf)) return 1;
	  std::cout << "publisher snapshot boundary\n" << std::flush;
	  continue;
	}
	if (c != 'u' || late) continue;
	extra = new BaseObject;
	late = new LateObject;
	std::cout << "publisher changed\n" << std::flush;
      }
    } else done.wait();
    delete late;
    delete extra;
    delete base;
    app.stop();
    app.final();
    signalDone = nullptr;
    return 0;
  }
  if (argc != 1 || !options.mode || !options.issuerURL || !options.wssURL ||
      (options.mode != "front" && options.mode != "agent") ||
      (!options.inventory && !options.deviceID) ||
      (options.inventory && options.mode != "front") ||
      (options.inventory && options.group != "App"))
    return 1;
  bool groupOK = false;
  for (auto group: Ztc::fbs::EnumValuesGroup()) {
    if (options.group != Ztc::fbs::EnumNameGroup(group)) continue;
    options.groupID = group;
    groupOK = true;
    break;
  }
  if (!groupOK || !options.snapshots ||
      (options.expectPublishers && !options.oneShot) || (options.changes &&
      (options.groupID != Ztc::fbs::Group::Heap || options.snapshots < 2)))
    return 1;

  Zhttp::URL issuerURL{options.issuerURL};
  Zws::URI uri;
  Zum::AppIssuerPath issuerPath;
  ZtString<> issuerSource{issuerURL.url().path};
  ZtString<> authorization;
  bool hasBearer = bearer(authorization);
  if (!hasBearer && !(options.cookie && options.origin)) return 1;
  if (!issuerURL.ok() && issuerURL.url().scheme != Zhttp::Scheme::http &&
      issuerURL.url().scheme != Zhttp::Scheme::https) return 1;
  if (!issuerURL.ok() ||
      !issuerURL.url().host || issuerURL.url().hasQuery ||
      issuerURL.url().hasFragment || !issuerURL.url().path ||
      !ZfURI::loadPath(issuerPath, issuerSource) ||
      issuerPath.oauth2 != "oauth2" || !issuerPath.appID ||
      !Zws::URI::parse(uri, options.wssURL).ok() || !uri.secure() ||
      !uri.host || !uri.port || !uri.target ||
      (options.cookie && !options.origin)) return 1;

  ZmSemaphore done;
  signalDone = &done;
  ZmTrap::sigintFn(interrupted);
  ZmTrap::trap();
  ZiMultiplex mx{mxParams()};
  if (!mx.start()) return 1;
  App app{&options, &done, {}, {}, authorization,
    options.mode == "agent"};
  App::Client client{&app};
  Zws::Config ws;
  ws.maxMessage = 1U << 20;
  ws.maxQueuedInput = 1U << 22;
  ws.handshakeTimeout = 10;
  ws.closeTimeout = 5;
  ws.pingInterval = 30;
  ws.pongTimeout = 60;
  Zhttp::H2Config tls;
  if (options.caPath) tls.caPath(options.caPath);
  if (!client.init(Zhttp::HubConfig{&mx, "3", "4"}, tls, ws) ||
      !client.start()) {
    client.final();
    mx.stop();
    return 1;
  }
  using Link = App::Client::Link;
  ZmRef<Link> link = new Link{&client, uri,
    Ztc::Protocol,
    authorization, options.cookie, options.origin};
  link->connect();
  done.wait();
  if (link && app.completed && !app.failed) {
    client.rxRun([link = ZuMv(link)]() mutable { link->close(); });
  }
  (void)app.down.timedwait(Zm::now(2));
  ZmSemaphore stopped;
  client.stop([&stopped](bool) { stopped.post(); });
  stopped.wait();
  link = nullptr;
  client.final();
  mx.stop();
  signalDone = nullptr;
  ZmTrap::sigintFn(nullptr);
  return app.failed ? 1 : 0;
}
