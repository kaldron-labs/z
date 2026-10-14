//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>

#include <zlib/ZuDateTime.hh>
#include <zlib/ZuTestUtil.hh>


#include <zlib/ZiFile.hh>
#include <zlib/ZiLog.hh>

#include <zlib/ZtcApp.hh>
#include <zlib/ZtcAlert.hh>
#include <zlib/ZtcMsg.hh>

#include "ZiTestResidue.hh"

#include "ZtcTestClient.hh"

using namespace ZuTestUtil;

static Zi::Path g_residue;

struct FakeRecover {
  using Offsets = ZtArray<uint64_t,
    ZtArrayHeapID<"Ztc.Alert.FakeOffsets">>;
  using Lengths = ZtArray<unsigned,
    ZtArrayHeapID<"Ztc.Alert.FakeLengths">>;

  Zi::Offset dataSize_ = 0;
  Zi::Offset indexSize_ = 0;
  Offsets offsets;
  Lengths lengths;
  int corruptSeqNo = -1;
  int failIndexSeqNo = -1;
  int failFrameSeqNo = -1;
  bool failDataTruncate = false;
  bool failIndexTruncate = false;
  Zi::Offset dataTruncated = -1;
  Zi::Offset indexTruncated = -1;

  Zi::Offset dataSize() const { return dataSize_; }
  Zi::Offset indexSize() const { return indexSize_; }
  bool index(uint64_t seqNo, uint64_t &offset) const {
    if (seqNo == uint64_t(failIndexSeqNo) || seqNo >= offsets.length())
      return false;
    offset = offsets[unsigned(seqNo)];
    return true;
  }
  int frame(
      uint64_t seqNo, uint64_t offset, unsigned &length) const {
    if (seqNo == uint64_t(failFrameSeqNo))
      return Ztc::Alert_::LoadResult::IOError;
    if (seqNo == uint64_t(corruptSeqNo))
      return Ztc::Alert_::LoadResult::Corrupt;
    if (seqNo >= lengths.length())
      return Ztc::Alert_::LoadResult::Missing;
    length = lengths[unsigned(seqNo)];
    if (offset > uint64_t(dataSize_) ||
	length > uint64_t(dataSize_) - offset)
      return Ztc::Alert_::LoadResult::Incomplete;
    return Ztc::Alert_::LoadResult::OK;
  }
  bool truncateData(Zi::Offset offset) {
    dataTruncated = offset;
    return !failDataTruncate;
  }
  bool truncateIndex(Zi::Offset offset) {
    indexTruncated = offset;
    return !failIndexTruncate;
  }
};

FakeRecover fakeRecover(Zi::Offset dataSize, Zi::Offset indexSize)
{
  FakeRecover io;
  io.dataSize_ = dataSize;
  io.indexSize_ = indexSize;
  io.offsets.push(0);
  io.offsets.push(8);
  io.lengths.push(8);
  io.lengths.push(8);
  return io;
}

void recoveryCore()
{
  ZuTestScope(recoveryCore);

  bool firstPartialDataOK = true;
  for (Zi::Offset size = 1; size < 8; ++size) {
    auto io = fakeRecover(size, 8);
    uint64_t count = 0;
    Zi::Offset end = 0;
    if (!Ztc::Alert_::recover(io, count, end) || count || end ||
	io.dataTruncated != 0 || io.indexTruncated != 0)
      firstPartialDataOK = false;
  }
  ZuCheck(firstPartialDataOK);

  bool partialDataOK = true;
  for (Zi::Offset size = 9; size < 16; ++size) {
    auto io = fakeRecover(size, 16);
    uint64_t count = 0;
    Zi::Offset end = 0;
    if (!Ztc::Alert_::recover(io, count, end) || count != 1 || end != 8 ||
	io.dataTruncated != 8 || io.indexTruncated != 8)
      partialDataOK = false;
  }
  ZuCheck(partialDataOK);
  auto missingData = fakeRecover(8, 16);
  uint64_t missingCount = 0;
  Zi::Offset missingEnd = 0;
  ZuCheck(Ztc::Alert_::recover(missingData, missingCount, missingEnd) &&
    missingCount == 1 && missingEnd == 8 &&
    missingData.dataTruncated < 0 && missingData.indexTruncated == 8);

  bool firstPartialIndexOK = true;
  for (Zi::Offset size = 1; size < 8; ++size) {
    auto io = fakeRecover(8, size);
    uint64_t count = 0;
    Zi::Offset end = 0;
    if (!Ztc::Alert_::recover(io, count, end) || count || end ||
	io.dataTruncated != 0 || io.indexTruncated != 0)
      firstPartialIndexOK = false;
  }
  ZuCheck(firstPartialIndexOK);

  bool partialIndexOK = true;
  for (Zi::Offset size = 9; size < 16; ++size) {
    auto io = fakeRecover(16, size);
    uint64_t count = 0;
    Zi::Offset end = 0;
    if (!Ztc::Alert_::recover(io, count, end) || count != 1 || end != 8 ||
	io.dataTruncated != 8 || io.indexTruncated != 8)
      partialIndexOK = false;
  }
  ZuCheck(partialIndexOK);

  auto corrupt = fakeRecover(16, 16);
  corrupt.corruptSeqNo = 0;
  uint64_t count = 0;
  Zi::Offset end = 0;
  ZuCheck(!Ztc::Alert_::recover(corrupt, count, end));
  ZuCheck(corrupt.dataTruncated < 0 && corrupt.indexTruncated < 0);

  auto indexRead = fakeRecover(16, 16);
  indexRead.failIndexSeqNo = 0;
  ZuCheck(!Ztc::Alert_::recover(indexRead, count, end));
  auto frameRead = fakeRecover(16, 16);
  frameRead.failFrameSeqNo = 0;
  ZuCheck(!Ztc::Alert_::recover(frameRead, count, end));
  auto dataTruncate = fakeRecover(9, 8);
  dataTruncate.failDataTruncate = true;
  ZuCheck(!Ztc::Alert_::recover(dataTruncate, count, end));
  auto indexTruncate = fakeRecover(8, 9);
  indexTruncate.failIndexTruncate = true;
  ZuCheck(!Ztc::Alert_::recover(indexTruncate, count, end));
  ZuCheck(Ztc::Alert_::addDays(20240228, 1) == 20240229);
  ZuCheck(Ztc::Alert_::addDays(20240228, 2) == 20240301);
  ZuCheck(Ztc::Alert_::addDays(20240301, -1) == 20240229);
  ZuCheck(Ztc::Alert_::addDays(20231231, 1) == 20240101);
}

bool alertWriteAll(int fd, ZuBSpan data)
{
  return ZtcTestClient::writeAll(fd, data);
}

ZmRef<ZiIOBuf> alertReadFrame(int fd)
{
  return ZtcTestClient::readFrame(fd);
}

int alertConnect(const Ztc::App &app)
{
  return ZtcTestClient::connect(app, true);
}

bool alertSubscribe(
    int fd, uint64_t seqNo, uint32_t interval = 100,
    uint32_t date = 0, uint64_t alertSeqNo = 0)
{
  auto frame = ZtcTestClient::request(seqNo, Ztc::Group::Alert, "*", interval, true,
    date, alertSeqNo);
  if (!frame || !alertWriteAll(fd, frame->cspan())) return false;

  auto ackFrame = alertReadFrame(fd);
  auto ackMsg = ackFrame ? Ztc::msg(ackFrame->ptr<Ztc::Hdr>()) : Ztc::MsgView{};
  auto ack = ackMsg && Ztc::msgBody(ackMsg).tag() == Ztc::Body::Ack ?
    Ztc::msgBody(ackMsg).get<Ztc::Body::Ack>() : ZiFB::View<Ztc::Ack>{};
  if (!ack || ack.get<Ztc::ZfField(Ack, seqNo)>() != seqNo ||
      ack.get<Ztc::ZfField(Ack, status)>() != Ztc::AckStatus::OK) return false;

  auto doneFrame = alertReadFrame(fd);
  auto doneMsg = doneFrame ? Ztc::msg(doneFrame->ptr<Ztc::Hdr>()) : Ztc::MsgView{};
  auto done = doneMsg &&
      Ztc::msgBody(doneMsg).tag() == Ztc::Body::EOS ?
    Ztc::msgBody(doneMsg).get<Ztc::Body::EOS>() : ZiFB::View<Ztc::EOS>{};
  return done && done.get<Ztc::ZfField(EOS, seqNo)>() == seqNo;
}

Ztc::AckStatus::T alertRequestStatus(
    int fd, uint64_t seqNo, uint32_t date, uint64_t alertSeqNo)
{
  auto frame = ZtcTestClient::request(seqNo, Ztc::Group::Alert, "*", 0, true,
    date, alertSeqNo);
  if (!frame || !alertWriteAll(fd, frame->cspan()))
    return Ztc::AckStatus::Failed;
  auto ackFrame = alertReadFrame(fd);
  auto msg = ackFrame ? Ztc::msg(ackFrame->ptr<Ztc::Hdr>()) : Ztc::MsgView{};
  auto ack = msg && Ztc::msgBody(msg).tag() == Ztc::Body::Ack ?
    Ztc::msgBody(msg).get<Ztc::Body::Ack>() : ZiFB::View<Ztc::Ack>{};
  return ack && ack.get<Ztc::ZfField(Ack, seqNo)>() == seqNo ?
    ack.get<Ztc::ZfField(Ack, status)>() : Ztc::AckStatus::Failed;
}

ZiFB::View<Ztc::AlertTelemetry> alertReceive(
    int fd, uint64_t seqNo, ZmRef<ZiIOBuf> &frame)
{
  frame = alertReadFrame(fd);
  auto msg = frame ? Ztc::msg(frame->ptr<Ztc::Hdr>()) : Ztc::MsgView{};
  auto telemetry = msg && Ztc::msgBody(msg).tag() == Ztc::Body::Telemetry ?
    Ztc::msgBody(msg).get<Ztc::Body::Telemetry>() : ZiFB::View<Ztc::Telemetry>{};
  if (!telemetry || telemetry.get<Ztc::ZfField(Telemetry, seqNo)>() != seqNo ||
      telemetry.get<Ztc::ZfField(Telemetry, value)>().tag() !=
	Ztc::TelemetryBody::AlertTelemetry) return {};
  return telemetry.get<Ztc::ZfField(Telemetry, value)>()
    .get<Ztc::TelemetryBody::AlertTelemetry>();
}

using AlertKeys = ZtArray<ZuTuple<uint32_t, uint64_t>,
  ZtArrayHeapID<"Ztc.Alert.TestKeys">>;

bool replayMany(int fd, uint64_t seqNo, const AlertKeys &keys)
{
  auto requestFrame = ZtcTestClient::request(seqNo, Ztc::Group::Alert, "*", 0, true);
  if (!requestFrame || !alertWriteAll(fd, requestFrame->cspan())) return false;

  auto ackFrame = alertReadFrame(fd);
  auto ackMsg = ackFrame ? Ztc::msg(ackFrame->ptr<Ztc::Hdr>()) : Ztc::MsgView{};
  auto ack = ackMsg && Ztc::msgBody(ackMsg).tag() == Ztc::Body::Ack ?
    Ztc::msgBody(ackMsg).get<Ztc::Body::Ack>() : ZiFB::View<Ztc::Ack>{};
  if (!ack || ack.get<Ztc::ZfField(Ack, seqNo)>() != seqNo ||
      ack.get<Ztc::ZfField(Ack, status)>() != Ztc::AckStatus::OK) return false;

  unsigned n = keys.length();
  for (unsigned i = 0; i < n; ++i) {
    ZmRef<ZiIOBuf> frame;
    auto alert = alertReceive(fd, seqNo, frame);
    if (!alert || alert.get<Ztc::ZfField(AlertTelemetry, date)>() != keys[i].p<0>() ||
	alert.get<Ztc::ZfField(AlertTelemetry, seqNo)>() != keys[i].p<1>()) return false;
  }
  auto doneFrame = alertReadFrame(fd);
  auto doneMsg = doneFrame ? Ztc::msg(doneFrame->ptr<Ztc::Hdr>()) : Ztc::MsgView{};
  auto done = doneMsg &&
      Ztc::msgBody(doneMsg).tag() == Ztc::Body::EOS ?
    Ztc::msgBody(doneMsg).get<Ztc::Body::EOS>() : ZiFB::View<Ztc::EOS>{};
  return done && done.get<Ztc::ZfField(EOS, seqNo)>() == seqNo;
}

bool appDegraded(int fd, uint64_t seqNo)
{
  auto requestFrame = ZtcTestClient::request(seqNo, Ztc::Group::App, "*", 0, true);
  if (!requestFrame || !alertWriteAll(fd, requestFrame->cspan())) return false;
  auto ack = alertReadFrame(fd);
  auto telemetryFrame = alertReadFrame(fd);
  auto done = alertReadFrame(fd);
  if (!ack || !done) return false;
  auto msg = telemetryFrame ?
    Ztc::msg(telemetryFrame->ptr<Ztc::Hdr>()) : Ztc::MsgView{};
  auto telemetry = msg && Ztc::msgBody(msg).tag() == Ztc::Body::Telemetry ?
    Ztc::msgBody(msg).get<Ztc::Body::Telemetry>() : ZiFB::View<Ztc::Telemetry>{};
  auto app = telemetry && telemetry.get<Ztc::ZfField(Telemetry, seqNo)>() == seqNo &&
      telemetry.get<Ztc::ZfField(Telemetry, value)>().tag() == Ztc::TelemetryBody::AppTelemetry ?
    telemetry.get<Ztc::ZfField(Telemetry, value)>()
      .get<Ztc::TelemetryBody::AppTelemetry>() : ZiFB::View<Ztc::AppTelemetry>{};
  return app && app.get<Ztc::ZfField(AppTelemetry, degraded)>();
}

void liveAlerts()
{
  ZuTestScope(liveAlerts);

  ZuTime time = Zm::now();
  uint32_t date = uint32_t(ZuDateTime{time}.yyyymmdd());
  Zi::Path prefix = ZiFile::append(g_residue, "live");
  Zi::Path dataPath{prefix};
  dataPath << '.' << date << ".data";
  Zi::Path indexPath{prefix};
  indexPath << '.' << date << ".index";

  Ztc::App app;
  Ztc::AppCf cf;
  cf.maxAlertMsg = 5;
  cf.alertTail = 2;
  cf.alertPrefix = prefix;
  ZuCheck(app.init(cf));
  ZuCheck(app.start());

  int fd1 = alertConnect(app);
  ZuCheck(fd1 >= 0);
  constexpr uint64_t SeqNo1 = uint64_t(0x123456789abcdef0);
  ZuCheck(alertSubscribe(fd1, SeqNo1));

  auto sink = app.alertSink();
  ZuCheck(sink);
  ZeLogBuf message;
  message << "123456789";
  time.nsec() = 123456789;
  ZeEventInfo info{
    time, Zm::ThreadID(uint64_t(0x12345678)), Ze::Warning,
    "file", 1, "function", "component"};
  sink->post(message, info);

  ZmRef<ZiIOBuf> frame1;
  auto alert1 = alertReceive(fd1, SeqNo1, frame1);
  ZuCheck(alert1);
  ZuCheck(alert1 && alert1.get<Ztc::ZfField(AlertTelemetry, message)>() == "12345");
  ZuCheck(alert1 && alert1.get<Ztc::ZfField(AlertTelemetry, seqNo)>() == 0);
  ZuCheck(alert1 &&
    alert1.get<Ztc::ZfField(AlertTelemetry, date)>() == ZuDateTime{time}.yyyymmdd());
  ZuCheck(alert1 && alert1.get<Ztc::ZfField(AlertTelemetry, tid)>() == uint64_t(0x12345678));
  ZuCheck(alert1 && alert1.get<Ztc::ZfField(AlertTelemetry, severity)>() == Ze::Warning);

  ZtcTestClient::close(fd1);
  ZuCheck(app.stop());
  app.final();

  message.length(0);
  message << "after final";
  sink->post(message, info);

  Zi::Offset recoveredOffset = ZiStat{dataPath}.size();
  {
    ZiFile data{dataPath, ZiFile::Append | ZiFile::GC};
    ZiFile index{indexPath, ZiFile::Append | ZiFile::GC};
    ZuCheck(data && index);
    const char tail[] = "partial-data";
    ZuLittleEndian<uint64_t> offset{uint64_t(data.size())};
    ZuCheck(data.write(tail, sizeof(tail)) == Zi::OK);
    ZuCheck(index.write(&offset, 3) == Zi::OK);
  }

  Ztc::App recovered;
  ZuCheck(recovered.init(cf));
  ZuCheck(recovered.start());
  int fd = alertConnect(recovered);
  ZuCheck(fd >= 0);
  constexpr uint64_t RecoveredSeqNo = SeqNo1 + 1;
  ZuCheck(alertSubscribe(fd, RecoveredSeqNo, 100, date, 0));
  auto recoveredSink = recovered.alertSink();
  message.length(0);
  message << "again";
  recoveredSink->post(message, info);
  ZmRef<ZiIOBuf> recoveredFrame;
  auto recoveredAlert =
    alertReceive(fd, RecoveredSeqNo, recoveredFrame);
  ZuCheck(recoveredAlert && recoveredAlert.get<Ztc::ZfField(AlertTelemetry, seqNo)>() == 1);
  ZtcTestClient::close(fd);
  ZuCheck(recovered.stop());
  recovered.final();
  ZuCheck(ZiStat{indexPath}.size() == 2 * Zi::Offset(sizeof(uint64_t)));
  {
    ZiFile index{indexPath, ZiFile::ReadOnly | ZiFile::GC};
    ZuLittleEndian<uint64_t> offset;
    ZuCheck(index &&
      index.pread(sizeof(uint64_t), &offset, sizeof(offset)) ==
	int(sizeof(offset)) &&
      uint64_t(offset) == uint64_t(recoveredOffset));
  }
  ZuCheck(recoveredFrame &&
    ZiStat{dataPath}.size() == recoveredOffset + recoveredFrame->length);

  Ztc::App replay;
  ZuCheck(replay.init(cf));
  ZuCheck(replay.start());
  int replayFD = alertConnect(replay);
  ZuCheck(replayFD >= 0);
  constexpr uint64_t ReplaySeqNo = RecoveredSeqNo + 1;
  auto requestFrame = ZtcTestClient::request(ReplaySeqNo, Ztc::Group::Alert, "*", 0, true,
    date, 0);
  ZuCheck(requestFrame && alertWriteAll(replayFD, requestFrame->cspan()));
  auto replayAck = alertReadFrame(replayFD);
  auto replayAckMsg =
    replayAck ? Ztc::msg(replayAck->ptr<Ztc::Hdr>()) : Ztc::MsgView{};
  auto ack = replayAckMsg &&
      Ztc::msgBody(replayAckMsg).tag() == Ztc::Body::Ack ?
    Ztc::msgBody(replayAckMsg).get<Ztc::Body::Ack>() : ZiFB::View<Ztc::Ack>{};
  ZuCheck(ack && ack.get<Ztc::ZfField(Ack, seqNo)>() == ReplaySeqNo &&
    ack.get<Ztc::ZfField(Ack, status)>() == Ztc::AckStatus::OK);
  ZmRef<ZiIOBuf> replayFrame;
  auto replayAlert = alertReceive(replayFD, ReplaySeqNo, replayFrame);
  ZuCheck(replayAlert && replayAlert.get<Ztc::ZfField(AlertTelemetry, date)>() == date &&
    replayAlert.get<Ztc::ZfField(AlertTelemetry, seqNo)>() == 1);
  auto replayDone = alertReadFrame(replayFD);
  auto replayDoneMsg =
    replayDone ? Ztc::msg(replayDone->ptr<Ztc::Hdr>()) : Ztc::MsgView{};
  auto complete = replayDoneMsg &&
      Ztc::msgBody(replayDoneMsg).tag() == Ztc::Body::EOS ?
    Ztc::msgBody(replayDoneMsg).get<Ztc::Body::EOS>() : ZiFB::View<Ztc::EOS>{};
  ZuCheck(complete && complete.get<Ztc::ZfField(EOS, seqNo)>() == ReplaySeqNo);
  ZtcTestClient::close(replayFD);
  ZuCheck(replay.stop());
  replay.final();

}

void crossDateReplay()
{
  ZuTestScope(crossDateReplay);

  ZuDateTime today{Zm::now()};
  ZuDateTime yesterday{today};
  --yesterday.julian();
  uint32_t todayID = uint32_t(today.yyyymmdd());
  uint32_t yesterdayID = uint32_t(yesterday.yyyymmdd());
  Ztc::AppCf cf;
  uint32_t expiredID = Ztc::Alert_::addDays(
    todayID, -int(cf.alertRetention) - 10);
  Zi::Path prefix = ZiFile::append(g_residue, "cross");
  Zi::Path todayData{prefix};
  Zi::Path todayIndex{prefix};
  Zi::Path yesterdayData{prefix};
  Zi::Path yesterdayIndex{prefix};
  Zi::Path expiredData{prefix};
  Zi::Path expiredIndex{prefix};
  todayData << '.' << todayID << ".data";
  todayIndex << '.' << todayID << ".index";
  yesterdayData << '.' << yesterdayID << ".data";
  yesterdayIndex << '.' << yesterdayID << ".index";
  expiredData << '.' << expiredID << ".data";
  expiredIndex << '.' << expiredID << ".index";
  {
    ZiFile expiredDataFile{
      expiredData, ZiFile::Create | ZiFile::GC};
    ZiFile expiredIndexFile{
      expiredIndex, ZiFile::Create | ZiFile::GC};
    ZuCheck(expiredDataFile && expiredIndexFile);
  }

  Ztc::App app;
  cf.alertPrefix = prefix;
  cf.maxPending = 1;
  ZuCheck(app.init(cf));
  ZuCheck(app.start());
  ZuCheck(!ZiStat{expiredData}.exists() &&
    !ZiStat{expiredIndex}.exists());
  int fd = alertConnect(app);
  ZuCheck(fd >= 0);
  constexpr uint64_t LiveSeqNo = uint64_t(0x3344556677889900);
  ZuCheck(alertSubscribe(fd, LiveSeqNo));
  auto sink = app.alertSink();
  ZeLogBuf message;
  message << "cross date";
  ZeEventInfo yesterdayInfo{
    yesterday.as_time(), Zm::ThreadID{11}, Ze::Error,
    "file", 1, "function", "component"};
  sink->post(message, yesterdayInfo);
  ZmRef<ZiIOBuf> firstFrame;
  auto first = alertReceive(fd, LiveSeqNo, firstFrame);
  ZuCheck(first && first.get<Ztc::ZfField(AlertTelemetry, date)>() == yesterdayID &&
    !first.get<Ztc::ZfField(AlertTelemetry, seqNo)>());
  sink->post(message, yesterdayInfo);
  ZmRef<ZiIOBuf> firstTailFrame;
  auto firstTail = alertReceive(fd, LiveSeqNo, firstTailFrame);
  ZuCheck(firstTail && firstTail.get<Ztc::ZfField(AlertTelemetry, date)>() == yesterdayID &&
    firstTail.get<Ztc::ZfField(AlertTelemetry, seqNo)>() == 1);
  ZeEventInfo todayInfo{
    today.as_time(), Zm::ThreadID{12}, Ze::Warning,
    "file", 2, "function", "component"};
  sink->post(message, todayInfo);
  ZmRef<ZiIOBuf> secondFrame;
  auto second = alertReceive(fd, LiveSeqNo, secondFrame);
  ZuCheck(second && second.get<Ztc::ZfField(AlertTelemetry, date)>() == todayID &&
    !second.get<Ztc::ZfField(AlertTelemetry, seqNo)>());
  ZtcTestClient::close(fd);
  ZuCheck(app.stop());
  app.final();

  Ztc::App replay;
  ZuCheck(replay.init(cf));
  ZuCheck(replay.start());
  int replayFD = alertConnect(replay);
  ZuCheck(replayFD >= 0);
  AlertKeys keys;
  keys.push(ZuFwdTuple(yesterdayID, uint64_t{0}));
  keys.push(ZuFwdTuple(yesterdayID, uint64_t{1}));
  keys.push(ZuFwdTuple(todayID, uint64_t{0}));
  ZuCheck(replayMany(replayFD, LiveSeqNo + 1, keys));
  ZuCheck(alertRequestStatus(
    replayFD, LiveSeqNo + 2,
    Ztc::Alert_::addDays(yesterdayID, -int(cf.alertRetention)), 0) ==
    Ztc::AckStatus::Invalid);
  ZtcTestClient::close(replayFD);
  ZuCheck(replay.stop());
  replay.final();

  {
    ZiFile index{yesterdayIndex, ZiFile::GC};
    ZuLittleEndian<uint64_t> badOffset{1};
    ZuCheck(index &&
      index.pwrite(0, &badOffset, sizeof(badOffset)) == Zi::OK);
  }
  Zi::Offset corruptSize = ZiStat{yesterdayIndex}.size();
  Ztc::App corrupt;
  ZuCheck(corrupt.init(cf));
  ZuCheck(corrupt.start());
  int corruptFD = alertConnect(corrupt);
  ZuCheck(corruptFD >= 0);
  ZuCheck(appDegraded(corruptFD, LiveSeqNo + 2));
  ZuCheck(ZiStat{yesterdayIndex}.size() == corruptSize);
  ZtcTestClient::close(corruptFD);
  ZuCheck(corrupt.stop());
  corrupt.final();

  {
    ZiFile index{yesterdayIndex, ZiFile::GC};
    ZuLittleEndian<uint64_t> offset{0};
    ZuCheck(index &&
      index.pwrite(0, &offset, sizeof(offset)) == Zi::OK);
  }
  Zi::Offset identitySize = ZiStat{todayData}.size();
  bool identityMutated = false;
  {
    ZiFile data{todayData, ZiFile::GC};
    ZmRef<ZiIOBuf> frame = new ZiIOBufAlloc<1024,
      Ztc::AppCf::DefltMaxFrame, "Ztc.Alert.TestRx">{};
    if (data && identitySize > 0 &&
	identitySize <= Ztc::AppCf::DefltMaxFrame &&
	frame->alloc(unsigned(identitySize))) {
      frame->length = unsigned(identitySize);
      if (data.pread(0, frame->data(), frame->length) ==
	  int(frame->length)) {
	auto root = Ztc::msg(frame->ptr<Ztc::Hdr>());
	if (root && Ztc::msgBody(root).tag() == Ztc::Body::Telemetry) {
	  auto telemetry = Ztc::msgBody(root).get<Ztc::Body::Telemetry>();
	  auto value = telemetry.get<Ztc::ZfField(Telemetry, value)>();
	  if (value.tag() == Ztc::TelemetryBody::AlertTelemetry) {
	    auto alert = value.get<Ztc::TelemetryBody::AlertTelemetry>();
	    if (auto slot = alert.field<Ztc::ZfField(AlertTelemetry, seqNo)>()) {
	      ZiFB::write<uint64_t>(const_cast<uint8_t *>(slot), 1);
	      identityMutated =
		data.pwrite(0, frame->data(), frame->length) == Zi::OK;
	    }
	  }
	}
      }
    }
  }
  ZuCheck(identityMutated);
  Ztc::App identity;
  ZuCheck(identity.init(cf));
  ZuCheck(identity.start());
  int identityFD = alertConnect(identity);
  ZuCheck(identityFD >= 0);
  ZuCheck(appDegraded(identityFD, LiveSeqNo + 4));
  ZuCheck(ZiStat{todayData}.size() == identitySize);
  ZtcTestClient::close(identityFD);
  ZuCheck(identity.stop());
  identity.final();

}

int main()
{
  ZiTestResidue::init("ZtcAlertTest");
  Zi::Name telName = ZiTestResidue::uniqueName("telemetry");
  Zt::setenv("ZTC_RING", telName.data());
  g_residue = ZiTestResidue::dir("alerts");
  ZiLog::init("ZtcAlertTest");
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  ZuTestMain();
  ZuTestCall(recoveryCore);
  ZuTestCall(liveAlerts);
  ZuTestCall(crossDateReplay);
  ZtcTestClient::final();
  ZiLog::stop();
}
