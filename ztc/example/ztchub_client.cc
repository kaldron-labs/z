//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Minimal independent ztchub protocol client skeleton.

#include <stdlib.h>

#include <iostream>

#include <zlib/Zfb.hh>
#include <zlib/ZfbStruct.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmTrap.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZfCLI.hh>
#include <zlib/ZfURI.hh>
#include <zlib/ZhttpURL.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiIOBuf.hh>
#include <zlib/ZumURI.hh>
#include <zlib/ZtcAppTypes.hh>
#include <zlib/ZtcFB.hh>
#include <zlib/ZtcMsg.hh>
#include <zlib/Zws.hh>

#include "../../zum/example/native.hh"

namespace ZtcHubClient_ {

using Ztc::Protocol;
static ZmSemaphore *signalDone = nullptr;
static void interrupted() { if (signalDone) signalDone->post(); }

struct Options {
  ZtString<> config;
  bool noBrowser = false;
  ZtString<> deviceID;
  ZtString<> wssURL;
  ZtString<> caPath;
  bool help = false;
};

ZfStruct(, (Options, CLI),
  (((config), (CLI::Long<"config">, Required)), (String)),
  (((noBrowser), (CLI::Long<"no-browser">)), (Bool)),
  (((deviceID), (CLI::Long<"device-id">)), (String)),
  (((wssURL), (CLI::Long<"wss">)), (String)),
  (((caPath), (CLI::Long<"ca">)), (String)),
  (((help), (CLI::Flag<'h'>, CLI::Long<"help">)), (Bool)));

using Frame = ZmRef<ZiIOBuf>;
using FrameBuf = ZiIOBufAlloc<1024, 1U << 20, "Ztc.Example.Frame">;

static Frame requestFrame(ZuCSpan deviceID, uint64_t subID, bool subscribe)
{
  Zfb::IOBuilder builder{Frame{new FrameBuf}};
  auto device = builder.CreateString(deviceID.data(), deviceID.length());
  auto request = ZfbStruct::save(builder, Ztc::Request{
    .seqNo = 0, .interval = subscribe ? 1000U : 0U,
    .group = uint8_t(Ztc::fbs::Group::App), .subscribe = subscribe});
  builder.Finish(Ztc::saveMsg(
    builder, Ztc::fbs::Body::Request, request.Union(), subID, device));
  return builder.buf();
}

struct App {
  using Client = Zws::Client<App, Zhttp::H1TLS>;
  using Link = Client::Link;

  ZmSemaphore *done = nullptr;
  ZmSemaphore down;
  ZtString<> deviceID;
  uint64_t subID = 1;
  Frame frame;
  bool failed = false;
  bool subscribed = false;
  bool received = false;

  template <typename Link_>
  void connected(Link_ &link, const Zhttp::ConnectedInfo &)
  {
    link.txStream([frame = ZuMv(frame)](auto &tx) {
      tx << ZuBSpan{frame->data(), frame->length};
      tx.flush();
    }, Zws::Opcode::Binary);
    subscribed = true;
  }

  template <typename Link_>
  int messageStart(Link_ &link, Zws::Opcode::T opcode)
  {
    if (opcode != Zws::Opcode::Binary) {
      link.close(Zws::CloseCode::Unsupported);
      failed = true;
      return -1;
    }
    if (!frame) frame = new FrameBuf;
    frame->length = 0;
    return 1;
  }

  template <typename Link_, typename Rx>
  int process(Link_ &link, Rx &rx)
  {
    return Zhttp::bodyEach(rx, [this, &link](ZuSpan<uint8_t> span) {
      if (span.length() > (1U << 20) - frame->length) {
        link.close(Zws::CloseCode::TooLarge);
        failed = true;
        return false;
      }
      auto size = frame->length + span.length();
      frame->append(span);
      return frame->length == size;
    }) ? 1 : -1;
  }

  template <typename Link_>
  int messageEnd(Link_ &link)
  {
    auto msg = Ztc::msg(ZuBSpan{frame->data(), frame->length});
    if (!msg || !msg->deviceId() || !msg->agentGen() ||
        msg->subId() != subID || Zfb::Load::str(msg->deviceId()) != deviceID) {
      link.close(Zws::CloseCode::InvalidData);
      failed = true;
      return -1;
    }
    switch (msg->body_type()) {
      case Ztc::fbs::Body::Telemetry: {
        received = true;
        std::cout << "telemetry device=" <<
          msg->deviceId()->string_view() <<
          " generation=" << msg->agentGen() << '\n' << std::flush;
        done->post();
      } break;
      case Ztc::fbs::Body::Ack:
        if (msg->body_as_Ack()->status() == Ztc::fbs::AckStatus::OK) break;
        [[fallthrough]];
      case Ztc::fbs::Body::EOS:
      case Ztc::fbs::Body::Error:
        failed = !received;
        done->post();
        break;
      default:
        link.close(Zws::CloseCode::Protocol);
        failed = true;
        return -1;
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
    if (!received) failed = true;
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

} // ZtcHubClient_

static int session(const ZtcHubClient_::Options &options, ZuCSpan token)
{
  using namespace ZtcHubClient_;
  Zws::URI uri;
  ZtString<> authorization{"Bearer "};
  authorization << token;
  if (!Zws::URI::parse(uri, options.wssURL).ok() || !uri.secure() ||
      !uri.host || !uri.port || !uri.target ||
      !token) return 1;

  ZmSemaphore done;
  signalDone = &done;
  ZmTrap::sigintFn(interrupted);
  ZmTrap::trap();
  ZiMultiplex mx{mxParams()};
  if (!mx.start()) return 1;
  App app{&done, {}, options.deviceID};
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
  app.frame = requestFrame(options.deviceID, app.subID, true);
  if (!client.init(Zhttp::HubConfig{&mx, "3", "4"}, tls, ws) ||
      !client.start()) {
    client.final();
    mx.stop();
    signalDone = nullptr;
    ZmTrap::sigintFn(nullptr);
    return 1;
  }
  using Link = App::Client::Link;
  ZmRef<Link> link = new Link{&client, uri, Protocol, authorization};
  link->connect();
  done.wait();
  if (app.subscribed && link) {
    client.rxRun([link, deviceID = app.deviceID, subID = app.subID]() mutable {
      auto unsubscribe = requestFrame(deviceID, subID, false);
      link->txStream([unsubscribe = ZuMv(unsubscribe)](auto &tx) mutable {
        tx << ZuBSpan{unsubscribe->data(), unsubscribe->length};
        tx.flush();
      }, Zws::Opcode::Binary);
      link->close();
    });
  }
  (void)app.down.timedwait(Zm::now(6));
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

int main(int argc, char **argv)
{
  ZtcHubClient_::Options options;
  try {
    argc = ZfCLI::load(options, argc, argv);
    if (options.help) {
      std::cout << "Usage: ztchub_client --config=OAUTH_CONFIG "
        "--device-id=ID --wss=URL [--no-browser]\n";
      return 0;
    }
    ZumNative::Config config;
    if (argc != 1 || !options.deviceID || !options.wssURL ||
        !ZumNative::loadConfig(options.config, config)) return 1;
    if (!options.caPath) options.caPath = config.caPath;
    return ZumNative::run(config, options.noBrowser,
      [&options](ZiMultiplex &, ZumNative::Clients &, ZuCSpan token) {
        return session(options, token) == 0;
      }) ? 0 : 1;
  } catch (const ZeException &e) {
    std::cerr << e << '\n';
    return 1;
  }
}
