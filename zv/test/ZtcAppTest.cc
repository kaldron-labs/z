//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmSemaphore.hh>

#include <zlib/Zfb.hh>

#include <zlib/ZiLog.hh>

#include <zlib/ZtcApp.hh>
#include <zlib/ZtcFilter.hh>
#include <zlib/ZtcMsg.hh>

#include "ZiTestResidue.hh"

#include "ZtcTestClient.hh"

using namespace ZuTestUtil;

#ifndef _WIN32

using ZtcTestClient::readFrame;
using ZtcTestClient::writeAll;

struct SnapshotResult {
  Ztc::fbs::AckStatus	status = Ztc::fbs::AckStatus::Invalid;
  uint32_t		interval = 0;
  uint32_t		frames = 0;
  uint32_t		count = 0;
  Ztc::fbs::RAG		rag = Ztc::fbs::RAG::Off;
  bool			valid = false;
  bool			app = false;
};

bool sendRequest(
    int fd, uint64_t seqNo, Ztc::fbs::Group group, ZuCSpan filter,
    uint32_t interval, bool subscribe)
{
  auto requestBuf = ZtcTestClient::request(
    seqNo, group, filter, interval, subscribe);
  return requestBuf && writeAll(fd, requestBuf->cspan());
}

SnapshotResult readAck(int fd, uint64_t seqNo)
{
  SnapshotResult result;
  auto ackBuf = readFrame(fd);
  auto ackMsg = ackBuf ? Ztc::msg(ackBuf->ptr<Ztc::Hdr>()) : nullptr;
  auto ack = ackMsg &&
      ackMsg->body_type() == Ztc::fbs::Body::Ack ?
    ackMsg->body_as_Ack() : nullptr;
  if (!ack || ack->seqNo() != seqNo) return result;
  result.status = ack->status();
  result.interval = ack->interval();
  result.valid = true;
  return result;
}

bool readError(int fd)
{
  auto frame = readFrame(fd);
  auto msg = frame ? Ztc::msg(frame->ptr<Ztc::Hdr>()) : nullptr;
  return msg && msg->body_type() == Ztc::fbs::Body::Error &&
    msg->body_as_Error();
}

SnapshotResult receiveSnapshot(int fd, uint64_t seqNo)
{
  SnapshotResult result;
  for (;;) {
    auto frame = readFrame(fd);
    auto msg = frame ? Ztc::msg(frame->ptr<Ztc::Hdr>()) : nullptr;
    if (!msg) return result;
    if (msg->body_type() == Ztc::fbs::Body::Telemetry) {
      auto telemetry = msg->body_as_Telemetry();
      if (!telemetry || telemetry->seqNo() != seqNo) return result;
      if (telemetry->value_type() ==
	  Ztc::fbs::TelemetryBody::AppTelemetry) {
	auto app = telemetry->value_as_AppTelemetry();
	if (!app) return result;
	result.rag = app->rag();
	result.app = true;
      }
      ++result.frames;
      continue;
    }
    if (msg->body_type() != Ztc::fbs::Body::SnapshotComplete)
      return result;
    auto complete = msg->body_as_SnapshotComplete();
    if (!complete || complete->seqNo() != seqNo) return result;
    result.count = complete->count();
    result.valid = result.frames == result.count;
    return result;
  }
}

SnapshotResult snapshot(
    int fd, uint64_t seqNo, Ztc::fbs::Group group, ZuCSpan filter)
{
  SnapshotResult result;
  if (!sendRequest(fd, seqNo, group, filter, 0, true)) return result;
  result = readAck(fd, seqNo);
  if (!result.valid || result.status != Ztc::fbs::AckStatus::OK)
    return result;
  auto snapshot_ = receiveSnapshot(fd, seqNo);
  snapshot_.status = result.status;
  snapshot_.interval = result.interval;
  return snapshot_;
}

void appSnapshot()
{
  ZuTestScope(appSnapshot);

  Ztc::App app;
  Ztc::AppCf cf;
  cf.minInterval = 50;
  cf.maxInterval = 1000;
  cf.maxSubs = 1;
  {
    Ztc::App invalid;
    Ztc::AppCf invalidCf = cf;
    invalidCf.mx.nThreads = 3;
    ZuCheck(!invalid.init(invalidCf));
  }
  {
    Ztc::App invalid;
    Ztc::AppCf invalidCf = cf;
    invalidCf.workerRole = invalidCf.mx.rxThread;
    ZuCheck(!invalid.init(invalidCf));
  }
  ZuCheck(app.init(cf));
  bool appHeaps = false;
  Ztc::HeapMgr::capture(
    [](Ztc::Heap *heap) {
      ZuCSpan id = heap->telKey().p<0>();
      return id == "Ztc.App.State" || id == "Ztc.App.Client" ||
	id == "Ztc.App.ClientIdx" || id == "Ztc.App.Pending" ||
	id == "Ztc.App.PendingIdx" || id == "Ztc.App.Ingress" ||
	id == "Ztc.App.AlertSink" || id == "Ztc.App.AlertEvent";
    },
    [&appHeaps](const auto &captures) {
      bool state = false, client = false, clientIdx = false;
      bool pending = false, pendingIdx = false;
      bool ingress = false, alertSink = false, alertEvent = false;
      unsigned n = captures.length();
      for (unsigned i = 0; i < n; ++i) {
	ZuCSpan id = captures[i].id;
	if (id == "Ztc.App.State") state = true;
	else if (id == "Ztc.App.Client") client = true;
	else if (id == "Ztc.App.ClientIdx") clientIdx = true;
	else if (id == "Ztc.App.Pending") pending = true;
	else if (id == "Ztc.App.PendingIdx") pendingIdx = true;
	else if (id == "Ztc.App.Ingress") ingress = true;
	else if (id == "Ztc.App.AlertSink") alertSink = true;
	else if (id == "Ztc.App.AlertEvent") alertEvent = true;
      }
      appHeaps = state && client && clientIdx && pending && pendingIdx &&
	ingress && alertSink && alertEvent;
    });
  ZuCheck(appHeaps);
  Ztc::App denied;
  ZuCheck(!denied.init(cf));
  ZuCheck(app.start());
  ZuCheck(app.start());
  ZuCheck(app.localIP() == ZiIP{"127.0.0.1"});
  ZuCheck(app.localPort());

  int fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  ZuCheck(fd >= 0);
  ZiSockAddr addr{app.localIP(), app.localPort()};
  ZuCheck(!::connect(fd, addr.sa(), addr.len()));

  constexpr uint64_t SeqNo = UINT64_C(0xf123456789abcdef);
  Zfb::IOBuilder fbb{
    Ztc::frameBuf(ZmRef<ZiIOBuf>{new ZiIOBufAlloc<1024,
      Ztc::AppCf::DefltMaxFrame, "Ztc.App.TestTx">{}})};
  auto request = Ztc::fbs::CreateRequestDirect(
    fbb, SeqNo, Ztc::fbs::Group::App, "*", 0, true);
  fbb.Finish(Ztc::fbs::CreateMsg(
    fbb, Ztc::fbs::Body::Request, request.Union()));
  auto requestBuf = Ztc::saveHdr(fbb);
  ZuCheck(requestBuf);
  ZuCheck(writeAll(fd, requestBuf->cspan()));

  auto ackBuf = readFrame(fd);
  ZuCheck(ackBuf);
  auto ackMsg = Ztc::msg(ackBuf->ptr<Ztc::Hdr>());
  ZuCheck(ackMsg && ackMsg->body_type() == Ztc::fbs::Body::Ack);
  auto ack = ackMsg ? ackMsg->body_as_Ack() : nullptr;
  ZuCheck(ack && ack->seqNo() == SeqNo);
  ZuCheck(ack && ack->status() == Ztc::fbs::AckStatus::OK);

  auto telBuf = readFrame(fd);
  ZuCheck(telBuf);
  auto telMsg = Ztc::msg(telBuf->ptr<Ztc::Hdr>());
  ZuCheck(telMsg && telMsg->body_type() == Ztc::fbs::Body::Telemetry);
  auto tel = telMsg ? telMsg->body_as_Telemetry() : nullptr;
  ZuCheck(tel && tel->seqNo() == SeqNo);
  ZuCheck(tel &&
    tel->value_type() == Ztc::fbs::TelemetryBody::AppTelemetry);
  auto data = tel ? tel->value_as_AppTelemetry() : nullptr;
  ZuCheck(data && Zfb::Load::str(data->id()) == "ztc");
  ZuCheck(data && data->state() == ZmEngineState::Running);

  auto doneBuf = readFrame(fd);
  ZuCheck(doneBuf);
  auto doneMsg = Ztc::msg(doneBuf->ptr<Ztc::Hdr>());
  ZuCheck(doneMsg &&
    doneMsg->body_type() == Ztc::fbs::Body::SnapshotComplete);
  auto done = doneMsg ? doneMsg->body_as_SnapshotComplete() : nullptr;
  ZuCheck(done && done->seqNo() == SeqNo);
  ZuCheck(done && done->count() == 1);

  constexpr uint64_t HeapSeqNo = SeqNo + 1;
  Zfb::IOBuilder heapFbb{
    Ztc::frameBuf(ZmRef<ZiIOBuf>{new ZiIOBufAlloc<1024,
      Ztc::AppCf::DefltMaxFrame, "Ztc.App.TestTx">{}})};
  auto heapRequest = Ztc::fbs::CreateRequestDirect(
    heapFbb, HeapSeqNo, Ztc::fbs::Group::Heap, "*", 0, true);
  heapFbb.Finish(Ztc::fbs::CreateMsg(
    heapFbb, Ztc::fbs::Body::Request, heapRequest.Union()));
  auto heapRequestBuf = Ztc::saveHdr(heapFbb);
  ZuCheck(heapRequestBuf && writeAll(fd, heapRequestBuf->cspan()));

  auto heapAckBuf = readFrame(fd);
  auto heapAckMsg =
    heapAckBuf ? Ztc::msg(heapAckBuf->ptr<Ztc::Hdr>()) : nullptr;
  auto heapAck = heapAckMsg &&
      heapAckMsg->body_type() == Ztc::fbs::Body::Ack ?
    heapAckMsg->body_as_Ack() : nullptr;
  ZuCheck(heapAck && heapAck->seqNo() == HeapSeqNo &&
    heapAck->status() == Ztc::fbs::AckStatus::OK);

  uint32_t heapCount = 0;
  uint32_t heapComplete = 0;
  bool heapFramesOK = true;
  for (;;) {
    auto frame = readFrame(fd);
    if (!frame) {
      heapFramesOK = false;
      break;
    }
    auto msg = Ztc::msg(frame->ptr<Ztc::Hdr>());
    if (!msg) {
      heapFramesOK = false;
      break;
    }
    if (msg->body_type() == Ztc::fbs::Body::Telemetry) {
      auto tel_ = msg->body_as_Telemetry();
      if (!tel_ || tel_->seqNo() != HeapSeqNo ||
	  tel_->value_type() != Ztc::fbs::TelemetryBody::HeapTelemetry)
	heapFramesOK = false;
      ++heapCount;
      continue;
    }
    if (msg->body_type() != Ztc::fbs::Body::SnapshotComplete)
      heapFramesOK = false;
    auto complete = msg->body_as_SnapshotComplete();
    if (!complete || complete->seqNo() != HeapSeqNo)
      heapFramesOK = false;
    heapComplete = complete ? complete->count() : 0;
    break;
  }
  ZuCheck(heapFramesOK);
  ZuCheck(heapCount && heapCount == heapComplete);

  unsigned directMxQueues = 0;
  ZuID queueOwner;
  ZuID queueID;
  Ztc::QueueType::T queueType = Ztc::QueueType::Thread;
  Ztc::MxMgr::all({[
    &directMxQueues, &queueOwner, &queueID, &queueType, &cf
  ](Ztc::Mx *mx_) {
    if (mx_->telKey() != cf.id) return;
    directMxQueues = mx_->allQueues(
      {[
	&directMxQueues, &queueOwner, &queueID, &queueType
      ](Ztc::Queue *queue_) {
	++directMxQueues;
	if (queueOwner) return;
	auto key = queue_->telKey();
	queueOwner = key.p<0>();
	queueID = key.p<1>();
	queueType = key.p<2>();
      }});
  }});
  ZuCheck(directMxQueues);
  unsigned directHubs = 0;
  ZuID hubID;
  Ztc::LinkType::T hubType = Ztc::LinkType::TCP;
  Ztc::HubMgr::all({[
    &directHubs, &hubID, &hubType
  ](Ztc::Hub *hub_) {
    ++directHubs;
    if (hubID) return;
    auto key = hub_->telKey();
    hubType = key.p<0>();
    hubID = key.p<1>();
  }});
  ZuCheck(directHubs);

  auto hash = snapshot(
    fd, SeqNo + 100, Ztc::fbs::Group::Hash, "*");
  ZuCheck(hash.valid && hash.status == Ztc::fbs::AckStatus::OK &&
    hash.count);
  auto thread = snapshot(
    fd, SeqNo + 101, Ztc::fbs::Group::Thread, "*");
  ZuCheck(thread.valid && thread.status == Ztc::fbs::AckStatus::OK &&
    thread.count);
  auto mx = snapshot(
    fd, SeqNo + 102, Ztc::fbs::Group::Mx, "*");
  ZuCheck(mx.valid && mx.status == Ztc::fbs::AckStatus::OK && mx.count);
  auto queue = snapshot(
    fd, SeqNo + 103, Ztc::fbs::Group::Queue, "*:*:*");
  ZuCheck(queue.valid && queue.status == Ztc::fbs::AckStatus::OK &&
    queue.count);
  auto hub = snapshot(
    fd, SeqNo + 104, Ztc::fbs::Group::Hub, "*:*");
  ZuCheck(hub.valid && hub.status == Ztc::fbs::AckStatus::OK && hub.count);
  auto db = snapshot(
    fd, SeqNo + 105, Ztc::fbs::Group::DB, "*");
  ZuCheck(db.valid && db.status == Ztc::fbs::AckStatus::OK);
  auto alert = snapshot(
    fd, SeqNo + 106, Ztc::fbs::Group::Alert, "*");
  ZuCheck(alert.valid && alert.status == Ztc::fbs::AckStatus::OK &&
    !alert.count);

  ZtString<> queueFilter;
  queueFilter << Ztc::QueueType::name(queueType) << ':';
  Ztc::Filter_::Codec::print(queueFilter, queueOwner);
  queueFilter << ':';
  Ztc::Filter_::Codec::print(queueFilter, queueID);
  auto exactQueue = snapshot(
    fd, SeqNo + 107, Ztc::fbs::Group::Queue, queueFilter);
  ZuCheck(exactQueue.valid &&
    exactQueue.status == Ztc::fbs::AckStatus::OK && exactQueue.count == 1);

  ZtString<> shortQueueFilter;
  shortQueueFilter << Ztc::QueueType::name(queueType) << ':';
  Ztc::Filter_::Codec::print(shortQueueFilter, queueID);
  auto shortQueue = snapshot(
    fd, SeqNo + 108, Ztc::fbs::Group::Queue, shortQueueFilter);
  ZuCheck(shortQueue.valid &&
    shortQueue.status == Ztc::fbs::AckStatus::OK && shortQueue.count);

  ZtString<> hubFilter;
  hubFilter << Ztc::LinkType::name(hubType) << ':';
  Ztc::Filter_::Codec::print(hubFilter, hubID);
  auto exactHub = snapshot(
    fd, SeqNo + 109, Ztc::fbs::Group::Hub, hubFilter);
  ZuCheck(exactHub.valid &&
    exactHub.status == Ztc::fbs::AckStatus::OK && exactHub.count);

  ZtString<> hubPrefixFilter;
  hubPrefixFilter << Ztc::LinkType::name(hubType) << ':';
  Ztc::Filter_::Codec::print(
    hubPrefixFilter, ZuCSpan{hubID}.trunc(3));
  hubPrefixFilter << '*';
  auto prefixHub = snapshot(
    fd, SeqNo + 110, Ztc::fbs::Group::Hub, hubPrefixFilter);
  ZuCheck(prefixHub.valid &&
    prefixHub.status == Ztc::fbs::AckStatus::OK && prefixHub.count);

  auto exact = snapshot(
    fd, SeqNo + 2, Ztc::fbs::Group::App, "ztc");
  ZuCheck(exact.valid &&
    exact.status == Ztc::fbs::AckStatus::OK && exact.count == 1);
  auto prefix = snapshot(
    fd, SeqNo + 3, Ztc::fbs::Group::App, "zt*");
  ZuCheck(prefix.valid &&
    prefix.status == Ztc::fbs::AckStatus::OK && prefix.count == 1);
  auto none = snapshot(
    fd, SeqNo + 4, Ztc::fbs::Group::App, "other");
  ZuCheck(none.valid &&
    none.status == Ztc::fbs::AckStatus::OK && !none.count);
  auto invalid = snapshot(
    fd, SeqNo + 5, Ztc::fbs::Group::App, "a:b");
  ZuCheck(invalid.valid &&
    invalid.status == Ztc::fbs::AckStatus::Invalid);

  Ztc::Hdr badHdr{uint32_t(4)};
  uint32_t badBody = 0;
  ZuCheck(writeAll(fd, {
    reinterpret_cast<const uint8_t *>(&badHdr), sizeof(badHdr)}));
  ZuCheck(writeAll(fd, {
    reinterpret_cast<const uint8_t *>(&badBody), sizeof(badBody)}));
  ZuCheck(readError(fd));
  auto afterBad = snapshot(
    fd, SeqNo + 6, Ztc::fbs::Group::App, "ztc");
  ZuCheck(afterBad.valid && afterBad.count == 1);

  Zfb::IOBuilder wrongFbb{
    Ztc::frameBuf(ZmRef<ZiIOBuf>{new ZiIOBufAlloc<1024,
      Ztc::AppCf::DefltMaxFrame, "Ztc.App.TestTx">{}})};
  auto wrongAck = Ztc::fbs::CreateAck(
    wrongFbb, SeqNo + 7, Ztc::fbs::AckStatus::OK);
  wrongFbb.Finish(Ztc::fbs::CreateMsg(
    wrongFbb, Ztc::fbs::Body::Ack, wrongAck.Union()));
  auto wrongBuf = Ztc::saveHdr(wrongFbb);
  ZuCheck(wrongBuf && writeAll(fd, wrongBuf->cspan()));
  ZuCheck(readError(fd));

  constexpr uint64_t FragmentSeqNo = SeqNo + 8;
  Zfb::IOBuilder fragmentFbb{
    Ztc::frameBuf(ZmRef<ZiIOBuf>{new ZiIOBufAlloc<1024,
      Ztc::AppCf::DefltMaxFrame, "Ztc.App.TestTx">{}})};
  auto fragmentRequest = Ztc::fbs::CreateRequestDirect(
    fragmentFbb, FragmentSeqNo, Ztc::fbs::Group::App, "ztc", 0, true);
  fragmentFbb.Finish(Ztc::fbs::CreateMsg(
    fragmentFbb, Ztc::fbs::Body::Request, fragmentRequest.Union()));
  auto fragmentBuf = Ztc::saveHdr(fragmentFbb);
  ZuCheck(fragmentBuf && writeAll(fd,
    {fragmentBuf->data(), sizeof(Ztc::Hdr)}));
  ZuCheck(fragmentBuf && writeAll(fd,
    {fragmentBuf->data() + sizeof(Ztc::Hdr),
      fragmentBuf->length - sizeof(Ztc::Hdr)}));
  auto fragmentAck = readAck(fd, FragmentSeqNo);
  auto fragmentResult = receiveSnapshot(fd, FragmentSeqNo);
  ZuCheck(fragmentAck.valid &&
    fragmentAck.status == Ztc::fbs::AckStatus::OK &&
    fragmentResult.valid && fragmentResult.count == 1);

  constexpr uint64_t CoalesceSeqNo = FragmentSeqNo + 1;
  ZuCheck(sendRequest(
    fd, CoalesceSeqNo, Ztc::fbs::Group::App, "ztc", 0, true));
  ZuCheck(sendRequest(
    fd, CoalesceSeqNo + 1, Ztc::fbs::Group::App, "ztc", 0, true));
  auto coalesceAck1 = readAck(fd, CoalesceSeqNo);
  auto coalesceResult1 = receiveSnapshot(fd, CoalesceSeqNo);
  auto coalesceAck2 = readAck(fd, CoalesceSeqNo + 1);
  auto coalesceResult2 = receiveSnapshot(fd, CoalesceSeqNo + 1);
  ZuCheck(coalesceAck1.valid && coalesceResult1.valid &&
    coalesceResult1.count == 1 &&
    coalesceAck2.valid && coalesceResult2.valid &&
    coalesceResult2.count == 1);

  constexpr uint64_t SubSeqNo = CoalesceSeqNo + 2;
  ZuCheck(sendRequest(
    fd, SubSeqNo, Ztc::fbs::Group::App, "", 1000, true));
  auto subAck = readAck(fd, SubSeqNo);
  ZuCheck(subAck.valid && subAck.status == Ztc::fbs::AckStatus::OK &&
    subAck.interval == 1000);
  auto subSnapshot = receiveSnapshot(fd, SubSeqNo);
  ZuCheck(subSnapshot.valid && subSnapshot.count == 1);

  ZuCheck(sendRequest(
    fd, SubSeqNo + 1, Ztc::fbs::Group::App, "*", 500, true));
  auto updateAck = readAck(fd, SubSeqNo + 1);
  ZuCheck(updateAck.valid &&
    updateAck.status == Ztc::fbs::AckStatus::OK &&
    updateAck.interval == 500);
  auto updateSnapshot = receiveSnapshot(fd, SubSeqNo + 1);
  ZuCheck(updateSnapshot.valid && updateSnapshot.count == 1);

  ZuCheck(sendRequest(
    fd, SubSeqNo + 2, Ztc::fbs::Group::App, "ztc", 500, true));
  auto limitAck = readAck(fd, SubSeqNo + 2);
  ZuCheck(limitAck.valid &&
    limitAck.status == Ztc::fbs::AckStatus::Failed);

  ZuCheck(sendRequest(
    fd, SubSeqNo + 3, Ztc::fbs::Group::App, "", 0, false));
  auto unsubAck = readAck(fd, SubSeqNo + 3);
  ZuCheck(unsubAck.valid &&
    unsubAck.status == Ztc::fbs::AckStatus::OK && !unsubAck.interval);
  ZuCheck(sendRequest(
    fd, SubSeqNo + 4, Ztc::fbs::Group::App, "*", 0, false));
  auto unsubMissAck = readAck(fd, SubSeqNo + 4);
  ZuCheck(unsubMissAck.valid &&
    unsubMissAck.status == Ztc::fbs::AckStatus::OK);

  ZuCheck(sendRequest(
    fd, SubSeqNo + 5, Ztc::fbs::Group::App, "*", 1001, true));
  auto maxAck = readAck(fd, SubSeqNo + 5);
  ZuCheck(maxAck.valid && maxAck.status == Ztc::fbs::AckStatus::Invalid);

  ZuCheck(sendRequest(
    fd, SubSeqNo + 6, Ztc::fbs::Group::App, "ztc", 1, true));
  auto clampAck = readAck(fd, SubSeqNo + 6);
  ZuCheck(clampAck.valid &&
    clampAck.status == Ztc::fbs::AckStatus::OK &&
    clampAck.interval == 50);
  auto immediate = receiveSnapshot(fd, SubSeqNo + 6);
  ZuCheck(immediate.valid && immediate.count == 1);
  app.rag(Ztc::RAG::Green);
  auto periodic = receiveSnapshot(fd, SubSeqNo + 6);
  ZuCheck(periodic.valid && periodic.count == 1 && periodic.app &&
    periodic.rag == Ztc::fbs::RAG::Green);
  ZuCheck(sendRequest(
    fd, SubSeqNo + 7, Ztc::fbs::Group::App, "ztc", 0, false));
  auto timerUnsubAck = readAck(fd, SubSeqNo + 7);
  ZuCheck(timerUnsubAck.valid &&
    timerUnsubAck.status == Ztc::fbs::AckStatus::OK);

  ZmAtomic<unsigned> stopRejected = 0;
  ZmSemaphore stopDone;
  app.rag({[&app, &stopRejected, &stopDone](Ztc::RAG::T) {
    stopRejected = !app.stop();
    stopDone.post();
  }});
  stopDone.wait();
  ZuCheck(stopRejected.load_());

  Ztc::Hdr oversize{uint32_t(cf.maxFrame)};
  ZuCheck(writeAll(fd, {
    reinterpret_cast<const uint8_t *>(&oversize), sizeof(oversize)}));
  ZuCheck(!::shutdown(fd, SHUT_WR));
  uint8_t closedByte = 0;
  ZuCheck(::recv(fd, &closedByte, 1, 0) <= 0);

  ::close(fd);
  ZuCheck(app.stop());
  ZuCheck(app.stop());
  app.final();
  app.final();
  ZuCheck(app.init(cf));
  app.final();
  Ztc::App later;
  ZuCheck(later.init(cf));
  later.final();
}

void appIPv6()
{
  ZuTestScopeRT(appIPv6);

  Ztc::App app;
  Ztc::AppCf cf;
  cf.ip = ZiIP{"::1"};
  if (!app.init(cf)) {
    ZuCheckRT(true);
    return;
  }
  if (!app.start()) {
    app.final();
    ZuCheckRT(true);
    return;
  }
  int fd = ::socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
  if (fd < 0) {
    app.final();
    ZuCheckRT(true);
    return;
  }
  ZiSockAddr addr{app.localIP(), app.localPort()};
  if (::connect(fd, addr.sa(), addr.len())) {
    ::close(fd);
    app.final();
    ZuCheckRT(true);
    return;
  }
  auto result = snapshot(
    fd, UINT64_C(0x123456789abcdef0), Ztc::fbs::Group::App, "*");
  ZuCheckRT(result.valid && result.count == 1 && result.app);
  ::close(fd);
  ZuCheckRT(app.stop());
  app.final();
}

void appBoundedSnapshot()
{
  ZuTestScope(appBoundedSnapshot);

  Ztc::App app;
  Ztc::AppCf cf;
  cf.maxPending = 1;
  ZuCheck(app.init(cf));
  ZuCheck(app.start());
  int fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  ZuCheck(fd >= 0);
  ZiSockAddr addr{app.localIP(), app.localPort()};
  ZuCheck(!::connect(fd, addr.sa(), addr.len()));
  auto result = snapshot(
    fd, UINT64_C(0x223456789abcdef0), Ztc::fbs::Group::Heap, "*");
  ZuCheck(result.valid && result.count > 1);
  ::close(fd);
  ZuCheck(app.stop());
  app.final();
}

#else

void appSnapshot()
{
  ZuTestScope(appSnapshot);
  ZuCheck(true);
}

void appIPv6()
{
  ZuTestScopeRT(appIPv6);
  ZuCheckRT(true);
}

void appBoundedSnapshot()
{
  ZuTestScope(appBoundedSnapshot);
  ZuCheck(true);
}

#endif

int main()
{
  ZiTestResidue::init("ZtcAppTest");
  ZiLog::init("ZtcAppTest");
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  ZuTestMain();
  ZuTestCall(appSnapshot);
  ZuTestCall(appIPv6);
  ZuTestCall(appBoundedSnapshot);
  ZiLog::stop();
}
