//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZtString.hh>

#include <zlib/Zfb.hh>

#include <zlib/ZiFile.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>

#include <zlib/ZtcApp.hh>
#include <zlib/ZtcMsg.hh>

#include "ZiTestResidue.hh"
#include "ZtcTestClient.hh"

using namespace ZuTestUtil;

struct SnapshotResult {
  Ztc::fbs::AckStatus	status = Ztc::fbs::AckStatus::Invalid;
  uint32_t		interval = 0;
  unsigned		frames = 0;
  Ztc::fbs::RAG		rag = Ztc::fbs::RAG::Off;
  uint32_t		ztcver = 0;
  bool			valid = false;
  bool			app = false;
};

static bool sendRequest(
    uint64_t seqNo, Ztc::fbs::Group group, ZuCSpan filter,
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
  auto msg = frame ? Ztc::msg(frame->ptr<Ztc::Hdr>()) : nullptr;
  auto ack_ = msg && msg->body_type() == Ztc::fbs::Body::Ack ?
    msg->body_as_Ack() : nullptr;
  if (!ack_ || ack_->seqNo() != seqNo ||
      Zfb::Load::str(ack_->id()) != ZiProgram::name())
    return result;
  result.status = ack_->status();
  result.interval = ack_->interval();
  result.valid = true;
  return result;
}

static SnapshotResult receive(uint64_t seqNo)
{
  SnapshotResult result;
  for (;;) {
    auto frame = ZtcTestClient::readFrame(1, seqNo != 0);
    auto msg = frame ? Ztc::msg(frame->ptr<Ztc::Hdr>()) : nullptr;
    if (!msg) return result;
    if (msg->body_type() == Ztc::fbs::Body::Telemetry) {
      auto tel = msg->body_as_Telemetry();
      if (!tel || tel->seqNo() != seqNo ||
	  Zfb::Load::str(tel->id()) != ZiProgram::name())
	return result;
      if (tel->value_type() == Ztc::fbs::TelemetryBody::AppTelemetry) {
	auto app = tel->value_as_AppTelemetry();
	if (!app) return result;
	result.app = true;
	result.rag = app->rag();
	result.ztcver = app->ztcver();
      }
      ++result.frames;
      continue;
    }
    auto eos = msg->body_type() == Ztc::fbs::Body::EOS ?
      msg->body_as_EOS() : nullptr;
    result.valid = eos && eos->seqNo() == seqNo &&
      Zfb::Load::str(eos->id()) == ZiProgram::name();
    return result;
  }
}

static SnapshotResult snapshot(
    uint64_t seqNo, Ztc::fbs::Group group, ZuCSpan filter)
{
  SnapshotResult result;
  if (!sendRequest(seqNo, group, filter, 0, true)) return result;
  result = ack(seqNo);
  if (!result.valid || result.status != Ztc::fbs::AckStatus::OK)
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
  Zi::Name pidDir = ZiTestResidue::uniqueName("registry");
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
  cf.maxSubs = 1;
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

  auto zero = snapshot(0, Ztc::fbs::Group::App, "*");
  ZuCheck(zero.valid && zero.app);
  auto gap = snapshot(2, Ztc::fbs::Group::App, "*");
  ZuCheck(gap.valid && gap.app);
  ZuCheck(sendRequest(1, Ztc::fbs::Group::App, "*", 0, true));
  ZuCheck(!ZtcTestClient::readFrame(1));
  ZuCheck(sendRequest(2, Ztc::fbs::Group::App, "*", 0, true));
  ZuCheck(!ZtcTestClient::readFrame(1));
  ZuCheck(sendRequest(3, Ztc::fbs::Group::App, "*", 500, true));
  auto admissionAck = ack(3);
  auto admissionSnapshot = receive(3);
  ZuCheck(admissionAck.valid && admissionAck.interval == 500 &&
    admissionSnapshot.valid);
  ZuCheck(sendRequest(3, Ztc::fbs::Group::App, "*", 0, false));
  auto unsubscribeAck = ack(3);
  ZuCheck(unsubscribeAck.valid &&
    unsubscribeAck.status == Ztc::fbs::AckStatus::OK);

  constexpr uint64_t SeqNo = UINT64_C(0x123456789abcdef0);
  auto appResult = snapshot(SeqNo, Ztc::fbs::Group::App, "*");
  ZuCheck(appResult.valid && appResult.app && appResult.frames == 1 &&
    appResult.ztcver == Z_VERSION);

  auto heaps = snapshot(SeqNo + 1, Ztc::fbs::Group::Heap, "*");
  ZuCheck(heaps.valid && heaps.frames);
  auto hashes = snapshot(SeqNo + 2, Ztc::fbs::Group::Hash, "*");
  ZuCheck(hashes.valid && hashes.frames);
  auto threads = snapshot(SeqNo + 3, Ztc::fbs::Group::Thread, "*");
  ZuCheck(threads.valid && threads.frames);
  auto mxs = snapshot(SeqNo + 4, Ztc::fbs::Group::Mx, "*");
  ZuCheck(mxs.valid && mxs.frames == 1);
  auto queues = snapshot(SeqNo + 5, Ztc::fbs::Group::Queue, "*:*:*");
  ZuCheck(queues.valid && queues.frames);
  auto hubs = snapshot(SeqNo + 6, Ztc::fbs::Group::Hub, "*:*");
  ZuCheck(hubs.valid);
  auto dbs = snapshot(SeqNo + 7, Ztc::fbs::Group::DB, "*");
  ZuCheck(dbs.valid);
  auto none = snapshot(SeqNo + 8, Ztc::fbs::Group::App, "other");
  ZuCheck(none.valid && !none.frames);

  ZuCheck(sendRequest(
    SeqNo + 9, Ztc::fbs::Group::App, "*", 0, true, cf.id));
  auto targetedAck = ack(SeqNo + 9);
  auto targeted = receive(SeqNo + 9);
  ZuCheck(targetedAck.valid &&
    targetedAck.status == Ztc::fbs::AckStatus::OK &&
    targeted.valid && targeted.app && targeted.frames == 1);

  ZuCheck(sendRequest(
    SeqNo + 10, Ztc::fbs::Group::App, "*", 0, true, "other"));
  ZuCheck(!ZtcTestClient::readFrame(1));

  ZuCheck(sendRequest(
    SeqNo + 11, Ztc::fbs::Group::App, "*", 0, true));
  auto wildcardAck = ack(SeqNo + 11);
  auto wildcard = receive(SeqNo + 11);
  ZuCheck(wildcardAck.valid &&
    wildcardAck.status == Ztc::fbs::AckStatus::OK &&
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
    Ztc::msg(errorFrame->ptr<Ztc::Hdr>()) : nullptr;
  auto error = errorMsg && errorMsg->body_type() == Ztc::fbs::Body::Error ?
    errorMsg->body_as_Error() : nullptr;
  ZuCheck(error);
  ZuCheck(!error || error->seqNo() == ZuCmp<uint64_t>::null(),
    ZuTestUtil::log("unexpected error sequence ",
      error ? error->seqNo() : uint64_t{}));
  ZuCheck(!error || Zfb::Load::str(error->id()) == cf.id);

  ZuCheck(sendRequest(SeqNo + 12, Ztc::fbs::Group::App, "*", 500, true));
  auto subAck = ack(SeqNo + 12);
  auto subSnapshot = receive(SeqNo + 12);
  ZuCheck(subAck.valid && subAck.interval == 500 && subSnapshot.valid);
  auto reset = ZtcTestClient::request(
    ZuCmp<uint64_t>::null(), Ztc::fbs::Group::App, {}, 0, false);
  ZuCheck(reset && ZtcTestClient::writeAll(1, reset->cspan()));
  ZuCheck(sendRequest(
    SeqNo + 13, Ztc::fbs::Group::App, "other", 500, true));
  auto resetAck = ack(SeqNo + 13);
  ZuCheck(resetAck.valid && resetAck.status == Ztc::fbs::AckStatus::OK);
  auto resetSnapshot = receive(SeqNo + 13);
  ZuCheck(resetSnapshot.valid && !resetSnapshot.frames);
  reset = ZtcTestClient::request(
    ZuCmp<uint64_t>::null(), Ztc::fbs::Group::App, {}, 0, false);
  ZuCheck(reset && ZtcTestClient::writeAll(1, reset->cspan()));

  ZuCheck(app.stop());
  auto shutdownFrame = ZtcTestClient::readFrame(1, false);
  auto shutdownMsg = shutdownFrame ?
    Ztc::msg(shutdownFrame->ptr<Ztc::Hdr>()) : nullptr;
  auto shutdown = shutdownMsg &&
    shutdownMsg->body_type() == Ztc::fbs::Body::Telemetry ?
    shutdownMsg->body_as_Telemetry() : nullptr;
  ZuCheck(shutdown && !shutdown->seqNo() &&
    shutdown->value_type() == Ztc::fbs::TelemetryBody::Shutdown &&
    shutdown->value_as_Shutdown());
  ZuCheck(!ZiStat{pidPath}.exists());
  Zt::setenv("ZTC_RING", ZiTestResidue::uniqueName("ignored"));
  Zt::setenv("ZTC_DIR", ZiTestResidue::uniqueName("ignored"));
  ZuCheck(app.start());
  ZuCheck(ZiStat{pidPath}.exists());
  ZuCheck(ZtcTestClient::client().connect(cf.id));
  auto restarted = snapshot(SeqNo + 14, Ztc::fbs::Group::App, "*");
  ZuCheck(restarted.valid && restarted.app);
  ZuCheck(app.stop());
  app.final();
  ZuCheck(mx.stop());
  ZuCheck(!ZiStat{pidPath}.exists());
  ZuCheck(ZiFile::rmdir(ZiFile::append(ZiFile::tmpDir(), pidDir)) == Zi::OK);
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
