//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZtString.hh>


#include <zlib/ZiFile.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>

#include <zlib/ZtcApp.hh>
#include <zlib/ZfCf.hh>
#include <zlib/ZtcMsg.hh>

#include "ZiTestResidue.hh"
#include "ZtcTestClient.hh"

using namespace ZuTestUtil;

struct SnapshotResult {
  Ztc::AckStatus::T	status = Ztc::AckStatus::Invalid;
  uint32_t		interval = 0;
  unsigned		frames = 0;
  Ztc::RAG::T		rag = Ztc::RAG::Off;
  uint32_t		ztcver = 0;
  bool			valid = false;
  bool			app = false;
};

static bool sendRequest(
    uint64_t seqNo, Ztc::Group::T group, ZuCSpan filter,
    uint32_t interval, bool subscribe,
    ZuCSpan id = {})
{
  auto frame = ZtcTestClient::request(
    seqNo, group, filter, interval, subscribe, 0, 0, id);
  return frame && ZtcTestClient::writeAll(1, frame->cspan());
}

static SnapshotResult ack(uint64_t seqNo)
{
  SnapshotResult result;
  auto frame = ZtcTestClient::readFrame(1);
  auto msg = frame ? Ztc::msg(frame->ptr<Ztc::Hdr>()) : Ztc::MsgView{};
  auto ack_ = msg && Ztc::msgBody(msg).tag() == Ztc::Body::Ack ?
    Ztc::msgBody(msg).get<Ztc::Body::Ack>() : ZiFB::View<Ztc::Ack>{};
  if (!ack_ || ack_.get<Ztc::ZfField(Ack, seqNo)>() != seqNo ||
      ack_.get<Ztc::ZfField(Ack, id)>() != ZiProgram::name())
    return result;
  result.status = ack_.get<Ztc::ZfField(Ack, status)>();
  result.interval = ack_.get<Ztc::ZfField(Ack, interval)>();
  result.valid = true;
  return result;
}

static SnapshotResult receive(uint64_t seqNo)
{
  SnapshotResult result;
  for (;;) {
    auto frame = ZtcTestClient::readFrame(1, seqNo != 0);
    auto msg = frame ? Ztc::msg(frame->ptr<Ztc::Hdr>()) : Ztc::MsgView{};
    if (!msg) return result;
    if (Ztc::msgBody(msg).tag() == Ztc::Body::Telemetry) {
      auto tel = Ztc::msgBody(msg).tag() == Ztc::Body::Telemetry ?
      Ztc::msgBody(msg).get<Ztc::Body::Telemetry>() : ZiFB::View<Ztc::Telemetry>{};
      if (!tel || tel.get<Ztc::ZfField(Telemetry, seqNo)>() != seqNo ||
	  tel.get<Ztc::ZfField(Telemetry, id)>() != ZiProgram::name())
	return result;
      if (tel.get<Ztc::ZfField(Telemetry, value)>().tag() == Ztc::TelemetryBody::AppTelemetry) {
	auto app = tel.get<Ztc::ZfField(Telemetry, value)>().tag() ==
	    Ztc::TelemetryBody::AppTelemetry ?
	  tel.get<Ztc::ZfField(Telemetry, value)>().get<Ztc::TelemetryBody::AppTelemetry>() :
	  ZiFB::View<Ztc::AppTelemetry>{};
	if (!app) return result;
	result.app = true;
	result.rag = app.get<Ztc::ZfField(AppTelemetry, rag)>();
	result.ztcver = app.get<Ztc::ZfField(AppTelemetry, ztcver)>().value();
      }
      ++result.frames;
      continue;
    }
    auto eos = Ztc::msgBody(msg).tag() == Ztc::Body::EOS ?
      Ztc::msgBody(msg).get<Ztc::Body::EOS>() : ZiFB::View<Ztc::EOS>{};
    result.valid = eos && eos.get<Ztc::ZfField(EOS, seqNo)>() == seqNo &&
      eos.get<Ztc::ZfField(EOS, id)>() == ZiProgram::name();
    return result;
  }
}

static SnapshotResult snapshot(
    uint64_t seqNo, Ztc::Group::T group, ZuCSpan filter)
{
  SnapshotResult result;
  if (!sendRequest(seqNo, group, filter, 0, true)) return result;
  result = ack(seqNo);
  if (!result.valid || result.status != Ztc::AckStatus::OK)
    return result;
  auto body = receive(seqNo);
  body.status = result.status;
  body.interval = result.interval;
  return body;
}

static void appTest()
{
  ZuTestScope(appTest);

  Ztc::AppCf cf;
  ZuCheck(cf.id == ZiProgram::name());
  auto parsed = ZfCf::scan("id: test-publisher");
  Ztc::AppCf loadedCf;
  ZfCf::handler<Ztc::AppCf>(parsed.p<1>()).update(loadedCf);
  ZuCheck(loadedCf.id == "test-publisher");
  ZuCheck(loadedCf.version == "10.0.0");
  ZuCheck(loadedCf.role == "server");
  ZuCheck(loadedCf.reqSize == Ztc::AppCf::DefltReqSize);
  {
    Ztc::App invalid;
    Ztc::AppCf invalidCf = cf;
    invalidCf.id = {};
    ZuCheck(!invalid.init(invalidCf));
  }
  {
    Ztc::App invalid;
    Ztc::AppCf invalidCf = cf;
    invalidCf.reqTimeout = 0;
    ZuCheck(!invalid.init(invalidCf));
  }
  {
    Ztc::App invalid;
    Ztc::AppCf invalidCf = cf;
    invalidCf.reqSize = invalidCf.maxFrame;
    ZuCheck(!invalid.init(invalidCf));
  }
  {
    Ztc::App invalid;
    Ztc::AppCf invalidCf = cf;
    invalidCf.scheduler.nThreads = 1;
    ZuCheck(!invalid.init(invalidCf));
  }
  {
    Ztc::App invalid;
    Ztc::AppCf invalidCf = cf;
    invalidCf.workerThread = invalidCf.timerThread;
    ZuCheck(!invalid.init(invalidCf));
  }

  Zi::Name telName = ZiTestResidue::uniqueName("telemetry");
  Zi::Path pidRoot = ZiTestResidue::tmpDir("registry");
  Zi::Path pidDir = ZiFile::leafname(pidRoot);
  Zt::setenv("ZTC_RING", telName);
  Zt::setenv("ZTC_DIR", pidDir);

  ZiMxParams params;
  params.scheduler([](auto &scheduler) {
    scheduler.id("ztc-app-test").nThreads(2)
      .thread(1, [](auto &thread) { thread.isolated(true); })
      .thread(2, [](auto &thread) { thread.isolated(true); });
  }).rxThread(1).txThread(2);
  ZiMultiplex mx{ZuMv(params)};
  ZuCheck(mx.start());

  Ztc::App app;
  cf.scheduler.nThreads = 3;
  cf.timerThread = 2;
  cf.workerThread = 3;
  cf.minInterval = 50;
  cf.maxInterval = 1000;
  cf.maxSubs = 2;
  ZuCheck(app.init(cf));
  ZuCheck(app.start());
  ZuCheck(app.start());

  ZtcTestClient::Ring absent{ZiRingParams{telName, 0}};
  ZuCheck(absent.open(ZtcTestClient::Ring::Write) != Zu::OK);

  Zi::Path pidName;
  pidName << pidDir << '/' << cf.id << ".pid";
  Zi::Path pidPath = ZiFile::append(ZiFile::tmpDir(), pidName);
  ZuCheck(ZiStat{pidPath}.exists());
  ZuCheck(ZtcTestClient::client().connect(cf.id));

  auto zero = snapshot(0, Ztc::Group::App, "*");
  ZuCheck(zero.valid && zero.app);
  auto gap = snapshot(2, Ztc::Group::App, "*");
  ZuCheck(gap.valid && gap.app);
  ZuCheck(sendRequest(1, Ztc::Group::App, "*", 0, true));
  ZuCheck(!ZtcTestClient::readFrame(1));
  ZuCheck(sendRequest(2, Ztc::Group::App, "*", 0, true));
  ZuCheck(!ZtcTestClient::readFrame(1));
  ZuCheck(sendRequest(3, Ztc::Group::App, "*", 500, true));
  auto admissionAck = ack(3);
  auto admissionSnapshot = receive(3);
  ZuCheck(admissionAck.valid && admissionAck.interval == 500 &&
    admissionSnapshot.valid);
  ZuCheck(sendRequest(3, Ztc::Group::App, "*", 0, false));
  auto unsubscribeAck = ack(3);
  ZuCheck(unsubscribeAck.valid &&
    unsubscribeAck.status == Ztc::AckStatus::OK);

  constexpr uint64_t SeqNo = uint64_t(0x123456789abcdef0);
  auto appResult = snapshot(SeqNo, Ztc::Group::App, "*");
  ZuCheck(appResult.valid && appResult.app && appResult.frames == 1 &&
    appResult.ztcver == Z_VERSION);

  auto heaps = snapshot(SeqNo + 1, Ztc::Group::Heap, "*");
  ZuCheck(heaps.valid && heaps.frames);
  auto hashes = snapshot(SeqNo + 2, Ztc::Group::Hash, "*");
  ZuCheck(hashes.valid && hashes.frames);
  auto threads = snapshot(SeqNo + 3, Ztc::Group::Thread, "*");
  ZuCheck(threads.valid && threads.frames);
  auto mxs = snapshot(SeqNo + 4, Ztc::Group::Mx, "*");
  ZuCheck(mxs.valid && mxs.frames == 1);
  auto queues = snapshot(SeqNo + 5, Ztc::Group::Queue, "*:*:*");
  ZuCheck(queues.valid && queues.frames);
  auto hubs = snapshot(SeqNo + 6, Ztc::Group::Hub, "*:*");
  ZuCheck(hubs.valid);
  auto dbs = snapshot(SeqNo + 7, Ztc::Group::DB, "*");
  ZuCheck(dbs.valid);
  auto none = snapshot(SeqNo + 8, Ztc::Group::App, "other");
  ZuCheck(none.valid && !none.frames);

  ZuCheck(sendRequest(
    SeqNo + 9, Ztc::Group::App, "*", 0, true, cf.id));
  auto targetedAck = ack(SeqNo + 9);
  auto targeted = receive(SeqNo + 9);
  ZuCheck(targetedAck.valid &&
    targetedAck.status == Ztc::AckStatus::OK &&
    targeted.valid && targeted.app && targeted.frames == 1);

  ZuCheck(sendRequest(
    SeqNo + 10, Ztc::Group::App, "*", 0, true, "other"));
  ZuCheck(!ZtcTestClient::readFrame(1));

  ZuCheck(sendRequest(
    SeqNo + 11, Ztc::Group::App, "*", 0, true));
  auto wildcardAck = ack(SeqNo + 11);
  auto wildcard = receive(SeqNo + 11);
  ZuCheck(wildcardAck.valid &&
    wildcardAck.status == Ztc::AckStatus::OK &&
    wildcard.valid && wildcard.app && wildcard.frames == 1);

  Ztc::Hdr badHdr{uint32_t(4)};
  uint32_t badBody = 0;
  ZmRef<ZiIOBuf> bad = new ZtcTestClient::Frame;
  unsigned badSize = sizeof(badHdr) + sizeof(badBody);
  ZuCheck(bad->alloc(badSize));
  memcpy(bad->data(), &badHdr, sizeof(badHdr));
  memcpy(bad->data() + sizeof(badHdr), &badBody, sizeof(badBody));
  bad->length = badSize;
  ZuCheck(ZtcTestClient::writeAll(1, bad->cspan()));
  auto errorFrame = ZtcTestClient::readFrame(1);
  auto errorMsg = errorFrame ?
    Ztc::msg(errorFrame->ptr<Ztc::Hdr>()) : Ztc::MsgView{};
  auto error = errorMsg && Ztc::msgBody(errorMsg).tag() == Ztc::Body::Error ?
    Ztc::msgBody(errorMsg).get<Ztc::Body::Error>() : ZiFB::View<Ztc::Error>{};
  ZuCheck(error);
  ZuCheck(!error || error.get<Ztc::ZfField(Error, seqNo)>() == ZuCmp<uint64_t>::null(),
    ZuTestUtil::log("unexpected error sequence ",
      error ? error.get<Ztc::ZfField(Error, seqNo)>() : uint64_t{}));
  ZuCheck(!error || error.get<Ztc::ZfField(Error, id)>() == cf.id);

  ZuCheck(sendRequest(SeqNo + 12, Ztc::Group::App, "*", 500, true));
  auto subAck = ack(SeqNo + 12);
  auto subSnapshot = receive(SeqNo + 12);
  ZuCheck(subAck.valid && subAck.interval == 500 && subSnapshot.valid);
  // Distinct consumers of the same group/filter own distinct subscriptions.
  ZuCheck(sendRequest(SeqNo + 13, Ztc::Group::App, "*", 500, true));
  auto secondAck = ack(SeqNo + 13);
  auto secondSnapshot = receive(SeqNo + 13);
  ZuCheck(secondAck.valid && secondAck.status == Ztc::AckStatus::OK &&
    secondSnapshot.valid);
  app.rag(Ztc::RAG::Red);
  // Ring waits are milliseconds; allow several 500 ms sampling intervals.
  unsigned liveAttempts = cf.maxInterval * 4;
  bool firstUpdated = false, secondUpdated = false;
  for (unsigned n = 0; n < 12 && !(firstUpdated && secondUpdated); ++n) {
    auto frame = ZtcTestClient::readFrame(1, true, liveAttempts);
    auto msg = frame ? Ztc::msg(frame->ptr<Ztc::Hdr>()) : Ztc::MsgView{};
    if (!msg) break;
    if (Ztc::msgBody(msg).tag() != Ztc::Body::Telemetry) continue;
    auto tel = Ztc::msgBody(msg).tag() == Ztc::Body::Telemetry ?
      Ztc::msgBody(msg).get<Ztc::Body::Telemetry>() : ZiFB::View<Ztc::Telemetry>{};
    auto data = tel.get<Ztc::ZfField(Telemetry, value)>().tag() ==
	    Ztc::TelemetryBody::AppTelemetry ?
	  tel.get<Ztc::ZfField(Telemetry, value)>().get<Ztc::TelemetryBody::AppTelemetry>() :
	  ZiFB::View<Ztc::AppTelemetry>{};
    if (!data || data.get<Ztc::ZfField(AppTelemetry, rag)>() != Ztc::RAG::Red) continue;
    if (tel.get<Ztc::ZfField(Telemetry, seqNo)>() == SeqNo + 12) firstUpdated = true;
    if (tel.get<Ztc::ZfField(Telemetry, seqNo)>() == SeqNo + 13) secondUpdated = true;
  }
  ZuCheck(firstUpdated && secondUpdated);
  // Cancel only the second consumer, then check another update on the first.
  ZuCheck(sendRequest(SeqNo + 13, Ztc::Group::App, "*", 0, false));
  bool cancelled = false;
  for (unsigned n = 0; n < 8 && !cancelled; ++n) {
    auto frame = ZtcTestClient::readFrame(1);
    auto msg = frame ? Ztc::msg(frame->ptr<Ztc::Hdr>()) : Ztc::MsgView{};
    if (!msg) break;
    auto value = Ztc::msgBody(msg).tag() == Ztc::Body::Ack ?
      Ztc::msgBody(msg).get<Ztc::Body::Ack>() : ZiFB::View<Ztc::Ack>{};
    cancelled = value && value.get<Ztc::ZfField(Ack, seqNo)>() == SeqNo + 13 &&
	value.get<Ztc::ZfField(Ack, status)>() == Ztc::AckStatus::OK;
  }
  ZuCheck(cancelled);
  app.rag(Ztc::RAG::Green);
  bool survivorUpdated = false;
  for (unsigned n = 0; n < 8 && !survivorUpdated; ++n) {
    auto frame = ZtcTestClient::readFrame(1, true, liveAttempts);
    auto msg = frame ? Ztc::msg(frame->ptr<Ztc::Hdr>()) : Ztc::MsgView{};
    if (!msg) break;
    auto tel = Ztc::msgBody(msg).tag() == Ztc::Body::Telemetry ?
      Ztc::msgBody(msg).get<Ztc::Body::Telemetry>() : ZiFB::View<Ztc::Telemetry>{};
    auto value = tel && tel.get<Ztc::ZfField(Telemetry, value)>().tag() ==
	Ztc::TelemetryBody::AppTelemetry ?
      tel.get<Ztc::ZfField(Telemetry, value)>().get<Ztc::TelemetryBody::AppTelemetry>() :
      ZiFB::View<Ztc::AppTelemetry>{};
    survivorUpdated = value && tel.get<Ztc::ZfField(Telemetry, seqNo)>() == SeqNo + 12 &&
	value.get<Ztc::ZfField(AppTelemetry, rag)>() == Ztc::RAG::Green;
  }
  ZuCheck(survivorUpdated);
  bool survivorEOS = false;
  for (unsigned n = 0; n < 8 && !survivorEOS; ++n) {
    auto frame = ZtcTestClient::readFrame(1, true, liveAttempts);
    auto msg = frame ? Ztc::msg(frame->ptr<Ztc::Hdr>()) : Ztc::MsgView{};
    if (!msg) break;
    auto value = Ztc::msgBody(msg).tag() == Ztc::Body::EOS ?
      Ztc::msgBody(msg).get<Ztc::Body::EOS>() : ZiFB::View<Ztc::EOS>{};
    survivorEOS = value && value.get<Ztc::ZfField(EOS, seqNo)>() == SeqNo + 12;
  }
  ZuCheck(survivorEOS);
  auto reset = ZtcTestClient::request(
    ZuCmp<uint64_t>::null(), Ztc::Group::App, {}, 0, false);
  ZuCheck(reset && ZtcTestClient::writeAll(1, reset->cspan()));
  ZuCheck(sendRequest(
    SeqNo + 14, Ztc::Group::App, "other", 500, true));
  auto resetAck = ack(SeqNo + 14);
  ZuCheck(resetAck.valid && resetAck.status == Ztc::AckStatus::OK);
  auto resetSnapshot = receive(SeqNo + 14);
  ZuCheck(resetSnapshot.valid && !resetSnapshot.frames);
  reset = ZtcTestClient::request(
    ZuCmp<uint64_t>::null(), Ztc::Group::App, {}, 0, false);
  ZuCheck(reset && ZtcTestClient::writeAll(1, reset->cspan()));

  ZuCheck(app.stop());
  auto shutdownFrame = ZtcTestClient::readFrame(1, false);
  auto shutdownMsg = shutdownFrame ?
    Ztc::msg(shutdownFrame->ptr<Ztc::Hdr>()) : Ztc::MsgView{};
  auto shutdown = shutdownMsg &&
    Ztc::msgBody(shutdownMsg).tag() == Ztc::Body::Telemetry ?
    Ztc::msgBody(shutdownMsg).get<Ztc::Body::Telemetry>() : ZiFB::View<Ztc::Telemetry>{};
  ZuCheck(shutdown && !shutdown.get<Ztc::ZfField(Telemetry, seqNo)>() &&
    shutdown.get<Ztc::ZfField(Telemetry, value)>().tag() == Ztc::TelemetryBody::Shutdown &&
    shutdown.get<Ztc::ZfField(Telemetry, value)>().get<Ztc::TelemetryBody::Shutdown>());
  ZuCheck(!ZiStat{pidPath}.exists());
  Zt::setenv("ZTC_RING", ZiTestResidue::uniqueName("ignored"));
  Zt::setenv("ZTC_DIR", ZiTestResidue::uniqueName("ignored"));
  ZuCheck(app.start());
  ZuCheck(ZiStat{pidPath}.exists());
  ZuCheck(ZtcTestClient::client().connect(cf.id));
  auto restarted = snapshot(SeqNo + 15, Ztc::Group::App, "*");
  ZuCheck(restarted.valid && restarted.app);
  ZuCheck(app.stop());
  app.final();
  ZuCheck(mx.stop());
  ZuCheck(!ZiStat{pidPath}.exists());
}

int main()
{
  ZiTestResidue::init("ZtcAppTest");
  ZiLog::init("ZtcAppTest");
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  ZuTestMain();
  ZuTestCall(appTest);
  ZtcTestClient::final();
  ZiLog::stop();
}
