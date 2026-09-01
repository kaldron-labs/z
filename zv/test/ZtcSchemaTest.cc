//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <limits.h>
#include <string.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/Zfb.hh>
#include <zlib/ZfbStruct.hh>

#include <zlib/ZtcDB.hh>
#include <zlib/ZtcFB.hh>
#include <zlib/ZtcMsg.hh>

using namespace ZuTestUtil;

void app()
{
  ZuTestScope(app);
  Ztc::AppTelemetry data;
  data.version = "10.0.0";
  data.role = "telemetry";
  data.startTime = INT64_C(0x123456789abcdef);
  data.state = ZmEngineState::Running;
  data.degraded = true;
  data.rag = Ztc::RAG::Amber;

  Zfb::Builder fbb;
  fbb.Finish(ZfbStruct::save(fbb, data));
  auto fbo = ZfbStruct::verify<Ztc::AppTelemetry>(
    {fbb.GetBufferPointer(), unsigned(fbb.GetSize())});
  ZuCheck(fbo);
  ZuCheck(Zfb::Load::str(fbo->version()) == data.version);
  ZuCheck(Zfb::Load::str(fbo->role()) == data.role);
  ZuCheck(fbo->startTime() == data.startTime);
  ZuCheck(fbo->state() == data.state);
  ZuCheck(fbo->degraded() == data.degraded);
  ZuCheck(fbo->rag() == Ztc::fbs::RAG::Amber);

  auto loaded = ZfbStruct::ctor<Ztc::AppTelemetry>(fbo);
  ZuCheck(loaded.version == data.version);
  ZuCheck(loaded.role == data.role);
  ZuCheck(loaded.startTime == data.startTime);
  ZuCheck(loaded.state == data.state);
  ZuCheck(loaded.degraded == data.degraded);
  ZuCheck(loaded.rag == data.rag);
}

void alert()
{
  ZuTestScope(alert);
  Ztc::AlertTelemetry data;
  data.message = "alert message";
  data.time = ZuTime{INT64_C(0x123456789), 987654321};
  data.seqNo = UINT64_C(0xfedcba9876543210);
  data.tid = UINT64_C(0xabcdef0123456789);
  data.date = UINT32_C(20991231);
  data.severity = -7;

  Zfb::Builder fbb;
  fbb.Finish(ZfbStruct::save(fbb, data));
  auto fbo = ZfbStruct::verify<Ztc::AlertTelemetry>(
    {fbb.GetBufferPointer(), unsigned(fbb.GetSize())});
  ZuCheck(fbo);
  ZuCheck(Zfb::Load::str(fbo->message()) == data.message);
  ZuCheck(fbo->time());
  ZuTime time;
  if (fbo->time())
    time = ZuTime{fbo->time()->sec(), fbo->time()->nsec()};
  ZuCheck(time == data.time);
  ZuCheck(fbo->seqNo() == data.seqNo);
  ZuCheck(fbo->tid() == data.tid);
  ZuCheck(fbo->date() == data.date);
  ZuCheck(fbo->severity() == data.severity);

  auto loaded = ZfbStruct::ctor<Ztc::AlertTelemetry>(fbo);
  ZuCheck(loaded.message == data.message);
  ZuCheck(loaded.time == data.time);
  ZuCheck(loaded.seqNo == data.seqNo);
  ZuCheck(loaded.tid == data.tid);
  ZuCheck(loaded.date == data.date);
  ZuCheck(loaded.severity == data.severity);

  Zfb::Builder msgBuilder;
  msgBuilder.ForceDefaults(true);
  auto id = msgBuilder.CreateString("app");
  auto value = ZfbStruct::save(msgBuilder, data);
  auto telemetry = Ztc::fbs::CreateTelemetry(
    msgBuilder, id, 0,
    Ztc::fbs::TelemetryBody::AlertTelemetry, value.Union());
  msgBuilder.Finish(Ztc::fbs::CreateMsg(
    msgBuilder, Ztc::fbs::Body::Telemetry, telemetry.Union()));
  auto msg = flatbuffers::GetMutableRoot<Ztc::fbs::Msg>(
    msgBuilder.GetBufferPointer());
  auto mutableTelemetry = msg->mutable_body_as_Telemetry();
  ZuCheck(mutableTelemetry);
  ZuCheck(mutableTelemetry && mutableTelemetry->mutate_seqNo(data.seqNo));
  ZuCheck(mutableTelemetry && mutableTelemetry->seqNo() == data.seqNo);
}

void framing()
{
  ZuTestScope(framing);
  Zfb::IOBuilder fbb{
    Ztc::frameBuf(ZmRef<ZiIOBuf>{new ZiIOBufAlloc<>})};
  auto request = Ztc::fbs::CreateRequestDirect(
    fbb, UINT64_C(0x123456789abcdef0), Ztc::fbs::Group::App, "app");
  fbb.Finish(Ztc::fbs::CreateMsg(
    fbb, Ztc::fbs::Body::Request, request.Union()));

  auto body = fbb.GetBufferPointer();
  auto bodyLength = unsigned(fbb.GetSize());
  auto buf = Ztc::saveHdr(fbb);
  ZuCheck(buf);
  ZuCheck(buf->data() + sizeof(Ztc::Hdr) == body);
  ZuCheck(buf->length == bodyLength + sizeof(Ztc::Hdr));
  ZuCheck(Ztc::loadHdr(buf.ptr(), buf->length) == int(buf->length));

  auto hdr = buf->ptr<Ztc::Hdr>();
  auto msg = Ztc::msg(hdr);
  ZuCheck(msg);
  ZuCheck(msg->body_type() == Ztc::fbs::Body::Request);
  auto loaded = msg->body_as_Request();
  ZuCheck(loaded);
  ZuCheck(loaded->seqNo() == UINT64_C(0x123456789abcdef0));
  ZuCheck(loaded->group() == Ztc::fbs::Group::App);
  ZuCheck(Zfb::Load::str(loaded->filter()) == "app");

  uint32_t length = hdr->length;
  hdr->length = UINT32_MAX;
  ZuCheck(Ztc::loadHdr(buf.ptr(), UINT32_MAX) < 0);
  hdr->length = length;

  buf->length = sizeof(Ztc::Hdr) - 1;
  ZuCheck(Ztc::loadHdr(buf.ptr(), UINT32_MAX) == INT_MAX);

  struct NoHeadroom {
    ZmRef<ZiIOBuf>	buf;
    unsigned		length;

    unsigned GetSize() const { return length; }
    ZmRef<ZiIOBuf> buf_() { return ZuMv(buf); }
  };
  struct NoHeadroomBuilder : public NoHeadroom {
    ZmRef<ZiIOBuf> buf() { return buf_(); }
  };
  ZmRef<ZiIOBuf> noHeadroomBuf = new ZiIOBufAlloc<>;
  uint8_t noHeadroomByte = 42;
  noHeadroomBuf->append(&noHeadroomByte, 1);
  auto noHeadroomBody = noHeadroomBuf->data();
  NoHeadroomBuilder noHeadroom{{noHeadroomBuf, 1}};
  ZuCheck(!Ztc::saveHdr(noHeadroom));
  ZuCheck(noHeadroomBuf->data() == noHeadroomBody &&
    noHeadroomBuf->length == 1 && noHeadroomBuf->data()[0] == 42);

  Zfb::IOBuilder invalidOuter{
    Ztc::frameBuf(ZmRef<ZiIOBuf>{new ZiIOBufAlloc<>})};
  auto invalidOuterRequest = Ztc::fbs::CreateRequestDirect(
    invalidOuter, 2, Ztc::fbs::Group::App, "*");
  invalidOuter.Finish(Ztc::fbs::CreateMsg(
    invalidOuter, Ztc::fbs::Body(127), invalidOuterRequest.Union()));
  auto invalidOuterBuf = Ztc::saveHdr(invalidOuter);
  ZuCheck(invalidOuterBuf &&
    !Ztc::msg(invalidOuterBuf->ptr<Ztc::Hdr>()));

  Zfb::IOBuilder invalidValue{
    Ztc::frameBuf(ZmRef<ZiIOBuf>{new ZiIOBufAlloc<>})};
  Ztc::AppTelemetry invalidValueData;
  auto invalidID = invalidValue.CreateString("app");
  auto invalidValueOffset = ZfbStruct::save(invalidValue, invalidValueData);
  auto invalidTelemetry = Ztc::fbs::CreateTelemetry(
    invalidValue, invalidID, 3, Ztc::fbs::TelemetryBody(127),
    invalidValueOffset.Union());
  invalidValue.Finish(Ztc::fbs::CreateMsg(
    invalidValue, Ztc::fbs::Body::Telemetry, invalidTelemetry.Union()));
  auto invalidValueBuf = Ztc::saveHdr(invalidValue);
  ZuCheck(invalidValueBuf &&
    !Ztc::msg(invalidValueBuf->ptr<Ztc::Hdr>()));
}

void goldenWire()
{
  ZuTestScope(goldenWire);

  // Exact capacity is intentional: this is the reviewed protocol capture.
  static const uint8_t capture[] = {
    0x68, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x08, 0x00, 0x0e, 0x00,
    0x07, 0x00, 0x08, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x12, 0x00, 0x28, 0x00, 0x14, 0x00,
    0x06, 0x00, 0x08, 0x00, 0x0c, 0x00, 0x07, 0x00, 0x10, 0x00, 0x1c, 0x00,
    0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x01, 0x20, 0x00, 0x00, 0x00,
    0x40, 0x30, 0x20, 0x10, 0x7b, 0x27, 0x35, 0x01, 0xf0, 0xde, 0xbc, 0x9a,
    0x78, 0x56, 0x34, 0x12, 0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
    0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x54, 0x78, 0x3a, 0x68,
    0x75, 0x62, 0x3a, 0x6c, 0x69, 0x6e, 0x6b, 0x2a, 0x00, 0x00, 0x00, 0x00
  };

  Zfb::IOBuilder fbb{
    Ztc::frameBuf(ZmRef<ZiIOBuf>{new ZiIOBufAlloc<>})};
  auto request = Ztc::fbs::CreateRequestDirect(
    fbb, UINT64_C(0x123456789abcdef0), Ztc::fbs::Group::Queue,
    "Tx:hub:link*", UINT32_C(0x10203040), true,
    UINT32_C(20260731), UINT64_C(0xfedcba9876543210));
  fbb.Finish(Ztc::fbs::CreateMsg(
    fbb, Ztc::fbs::Body::Request, request.Union()));
  auto buf = Ztc::saveHdr(fbb);
  ZuCheck(buf && buf->length == sizeof(capture));
  ZuCheck(buf && !memcmp(buf->data(), capture, sizeof(capture)));
  auto msg = Ztc::msg(reinterpret_cast<const Ztc::Hdr *>(capture));
  auto loaded = msg ? msg->body_as_Request() : nullptr;
  ZuCheck(loaded && loaded->seqNo() == UINT64_C(0x123456789abcdef0) &&
    loaded->group() == Ztc::fbs::Group::Queue &&
    Zfb::Load::str(loaded->filter()) == "Tx:hub:link*" &&
    loaded->interval() == UINT32_C(0x10203040) && loaded->subscribe() &&
    loaded->alertDate() == UINT32_C(20260731) &&
    loaded->alertSeqNo() == UINT64_C(0xfedcba9876543210));
}

template <typename T>
bool unionValue(Ztc::fbs::TelemetryBody type, const T &data)
{
  Zfb::Builder fbb;
  auto id = fbb.CreateString("app");
  auto value =
    ZfbStruct::save<ZuFacet::Core, ZfFieldFilter::All>(fbb, data);
  auto telemetry = Ztc::fbs::CreateTelemetry(
    fbb, id, UINT64_C(0xabcdef0123456789), type, value.Union());
  fbb.Finish(Ztc::fbs::CreateMsg(
    fbb, Ztc::fbs::Body::Telemetry, telemetry.Union()));
  Zfb::Verifier verifier{fbb.GetBufferPointer(), fbb.GetSize()};
  if (!verifier.VerifyBuffer<Ztc::fbs::Msg>()) return false;
  auto msg = Zfb::GetRoot<Ztc::fbs::Msg>(fbb.GetBufferPointer());
  auto loaded = msg && Ztc::validMsg(msg) ? msg->body_as_Telemetry() : nullptr;
  return loaded && Zfb::Load::str(loaded->id()) == "app" &&
    loaded->seqNo() == UINT64_C(0xabcdef0123456789) &&
    loaded->value_type() == type && loaded->value();
}

void telemetryUnions()
{
  ZuTestScope(telemetryUnions);
  ZuCheck(unionValue(Ztc::fbs::TelemetryBody::HeapTelemetry,
    Ztc::HeapTelemetry{}));
  ZuCheck(unionValue(Ztc::fbs::TelemetryBody::HashTelemetry,
    Ztc::HashTelemetry{}));
  ZuCheck(unionValue(Ztc::fbs::TelemetryBody::ThreadTelemetry,
    Ztc::ThreadTelemetry{}));
  ZuCheck(unionValue(Ztc::fbs::TelemetryBody::CxnTelemetry,
    Ztc::CxnTelemetry{}));
  ZuCheck(unionValue(Ztc::fbs::TelemetryBody::MxTelemetry,
    Ztc::MxTelemetry{}));
  ZuCheck(unionValue(Ztc::fbs::TelemetryBody::QueueTelemetry,
    Ztc::QueueTelemetry{}));
  ZuCheck(unionValue(Ztc::fbs::TelemetryBody::HubTelemetry,
    Ztc::HubTelemetry{}));
  ZuCheck(unionValue(Ztc::fbs::TelemetryBody::LinkTelemetry,
    Ztc::LinkTelemetry{}));
  ZuCheck(unionValue(Ztc::fbs::TelemetryBody::PoolTelemetry,
    Ztc::PoolTelemetry{}));
  ZuCheck(unionValue(Ztc::fbs::TelemetryBody::DBTelemetry,
    Ztc::DBTelemetry{}));
  ZuCheck(unionValue(Ztc::fbs::TelemetryBody::DBHostTelemetry,
    Ztc::DBHostTelemetry{}));
  ZuCheck(unionValue(Ztc::fbs::TelemetryBody::DBTableTelemetry,
    Ztc::DBTableTelemetry{}));
  ZuCheck(unionValue(Ztc::fbs::TelemetryBody::AppTelemetry,
    Ztc::AppTelemetry{}));
  ZuCheck(unionValue(Ztc::fbs::TelemetryBody::AlertTelemetry,
    Ztc::AlertTelemetry{}));
}

void rag()
{
  ZuTestScope(rag);

  Ztc::HeapTelemetry heap;
  ZuCheck(heap.rag() == Ztc::RAG::Off);
  heap.cacheSize = 100;
  ZuCheck(heap.rag() == Ztc::RAG::Green);
  heap.heapAllocs = 1;
  ZuCheck(heap.rag() == Ztc::RAG::Amber);
  heap.cacheAllocs = 100;
  ZuCheck(heap.rag() == Ztc::RAG::Red);
  ZuCheck(heap.allocated() == 101);

  Ztc::HashTelemetry hash;
  hash.loadFactor = 1.0;
  hash.effLoadFactor = 0.79;
  ZuCheck(hash.rag() == Ztc::RAG::Green);
  hash.effLoadFactor = 0.8;
  ZuCheck(hash.rag() == Ztc::RAG::Amber);
  hash.resized = 1;
  ZuCheck(hash.rag() == Ztc::RAG::Red);

  Ztc::ThreadTelemetry thread;
  ZuCheck(thread.rag() == Ztc::RAG::Green);
  thread.cpuUsage = 0.5;
  ZuCheck(thread.rag() == Ztc::RAG::Amber);
  thread.cpuUsage = 0.8;
  ZuCheck(thread.rag() == Ztc::RAG::Red);

  Ztc::QueueTelemetry queue;
  ZuCheck(queue.rag() == Ztc::RAG::Off);
  queue.size = 10;
  queue.count = 4;
  ZuCheck(queue.rag() == Ztc::RAG::Green);
  queue.count = 5;
  ZuCheck(queue.rag() == Ztc::RAG::Amber);
  queue.count = 8;
  ZuCheck(queue.rag() == Ztc::RAG::Red);

  Ztc::CxnTelemetry cxn;
  ZuCheck(cxn.rag() == Ztc::RAG::Red);
  cxn.rxBufSize = cxn.txBufSize = 100;
  cxn.rxBufLen = cxn.txBufLen = 49;
  ZuCheck(cxn.rag() == Ztc::RAG::Green);
  cxn.rxBufLen = 50;
  ZuCheck(cxn.rag() == Ztc::RAG::Amber);
  cxn.rxBufLen = 80;
  ZuCheck(cxn.rag() == Ztc::RAG::Red);

  Ztc::MxTelemetry mx;
  mx.state = ZmEngineState::Stopped;
  ZuCheck(mx.rag() == Ztc::RAG::Red);
  mx.state = ZmEngineState::Starting;
  ZuCheck(mx.rag() == Ztc::RAG::Amber);
  mx.state = ZmEngineState::Running;
  ZuCheck(mx.rag() == Ztc::RAG::Green);
  mx.state = -1;
  ZuCheck(mx.rag() == Ztc::RAG::Off);

  Ztc::HubTelemetry hub;
  hub.state = ZmEngineState::Stopped;
  ZuCheck(hub.rag() == Ztc::RAG::Red);
  hub.state = ZmEngineState::StartPending;
  ZuCheck(hub.rag() == Ztc::RAG::Amber);
  hub.state = ZmEngineState::Running;
  ZuCheck(hub.rag() == Ztc::RAG::Green);
  hub.state = -1;
  ZuCheck(hub.rag() == Ztc::RAG::Off);

  Ztc::LinkTelemetry link;
  link.state = Ztc::LinkState::Down;
  ZuCheck(link.rag() == Ztc::RAG::Red);
  link.state = Ztc::LinkState::Connecting;
  ZuCheck(link.rag() == Ztc::RAG::Amber);
  link.state = Ztc::LinkState::Up;
  ZuCheck(link.rag() == Ztc::RAG::Green);
  link.state = Ztc::LinkState::Disabled;
  ZuCheck(link.rag() == Ztc::RAG::Off);

  Ztc::PoolTelemetry pool;
  pool.state = Ztc::PoolState::Down;
  ZuCheck(pool.rag() == Ztc::RAG::Red);
  pool.state = Ztc::PoolState::Up;
  ZuCheck(pool.rag() == Ztc::RAG::Green);
  pool.state = -1;
  ZuCheck(pool.rag() == Ztc::RAG::Off);
}

void telemetryValues()
{
  ZuTestScope(telemetryValues);

  {
    Ztc::HeapTelemetry data;
    data.id = "heap";
    data.cacheSize = UINT64_C(0x100000001);
    data.cpuset[3] = true;
    data.cacheAllocs = UINT64_C(0x200000002);
    data.heapAllocs = UINT64_C(0x300000003);
    data.frees = UINT64_C(0x100000001);
    data.crossFrees = UINT64_C(0x400000004);
    data.size = UINT32_C(0x80000001);
    data.partition = UINT16_C(0x8001);
    data.sharded = 3;
    data.alignment = 64;
    Zfb::Builder fbb;
    fbb.Finish(
      ZfbStruct::save<ZuFacet::Core, ZfFieldFilter::All>(fbb, data));
    auto fbo = ZfbStruct::verify<Ztc::HeapTelemetry>(
      {fbb.GetBufferPointer(), unsigned(fbb.GetSize())});
    auto loaded = ZfbStruct::ctor<Ztc::HeapTelemetry>(fbo);
    ZuCheck(fbo && fbo->cacheSize() == data.cacheSize &&
      fbo->crossFrees() == data.crossFrees);
    ZuCheck(fbo && fbo->size() == data.size &&
      fbo->partition() == data.partition &&
      fbo->alignment() == data.alignment);
    ZuCheck(loaded.id == data.id && loaded.cpuset[3] &&
      loaded.cacheAllocs == data.cacheAllocs &&
      loaded.heapAllocs == data.heapAllocs && loaded.frees == data.frees);
    ZuCheck(fbo && fbo->allocated() == data.allocated() &&
      fbo->rag() == Ztc::fbs::RAG::Red);
  }
  {
    Ztc::HashTelemetry data;
    data.id = "hash";
    data.addr = uintptr_t(UINT64_C(0xabcdef0123456789));
    data.loadFactor = 1.25;
    data.effLoadFactor = 0.75;
    data.count = UINT64_C(0x100000005);
    data.nodeSize = UINT32_C(0x80000005);
    data.resized = 7;
    data.bits = 17;
    data.cBits = 9;
    data.linear = 1;
    data.shadow = 2;
    Zfb::Builder fbb;
    fbb.Finish(
      ZfbStruct::save<ZuFacet::Core, ZfFieldFilter::All>(fbb, data));
    auto fbo = ZfbStruct::verify<Ztc::HashTelemetry>(
      {fbb.GetBufferPointer(), unsigned(fbb.GetSize())});
    ZuCheck(fbo && fbo->addr() == data.addr &&
      fbo->count() == data.count && fbo->nodeSize() == data.nodeSize);
    ZuCheck(fbo && fbo->loadFactor() == data.loadFactor &&
      fbo->effLoadFactor() == data.effLoadFactor &&
      fbo->resized() == data.resized);
    ZuCheck(fbo && fbo->bits() == data.bits &&
      fbo->cBits() == data.cBits && fbo->linear() == data.linear &&
      fbo->shadow() == data.shadow && fbo->rag() == Ztc::fbs::RAG::Red);
  }
  {
    Ztc::ThreadTelemetry data;
    data.name = "thread";
    data.tid = UINT64_C(0xabcdef0123456789);
    data.stackSize = UINT64_C(0x100000006);
    data.cpuset[5] = true;
    data.cpuUsage = 0.625;
    data.allocStack = UINT64_C(0x200000006);
    data.allocHeap = UINT64_C(0x300000006);
    data.sysPriority = INT32_MIN + 7;
    data.sid = UINT16_C(0x8002);
    data.partition = UINT16_C(0x8003);
    data.priority = ZmThreadPriority::High;
    data.main = true;
    data.detached = true;
    Zfb::Builder fbb;
    fbb.Finish(
      ZfbStruct::save<ZuFacet::Core, ZfFieldFilter::All>(fbb, data));
    auto fbo = ZfbStruct::verify<Ztc::ThreadTelemetry>(
      {fbb.GetBufferPointer(), unsigned(fbb.GetSize())});
    auto loaded = ZfbStruct::ctor<Ztc::ThreadTelemetry>(fbo);
    ZuCheck(fbo && fbo->tid() == data.tid &&
      fbo->stackSize() == data.stackSize && fbo->sid() == data.sid &&
      fbo->partition() == data.partition);
    ZuCheck(fbo && fbo->cpuUsage() == data.cpuUsage &&
      fbo->allocStack() == data.allocStack &&
      fbo->allocHeap() == data.allocHeap &&
      fbo->sysPriority() == data.sysPriority);
    ZuCheck(loaded.cpuset[5] && loaded.priority == data.priority &&
      loaded.main && loaded.detached);
  }
  {
    Ztc::QueueTelemetry data;
    data.ownerID = "owner";
    data.id = "queue";
    data.inBytes = UINT64_C(0x100000011);
    data.outBytes = UINT64_C(0x200000012);
    data.inCount = UINT64_C(0x300000013);
    data.outCount = UINT64_C(0x400000014);
    data.count = UINT64_C(0x500000015);
    data.size = UINT32_C(0x80000015);
    data.full = UINT32_C(0x80000016);
    data.type = Ztc::QueueType::Tx;
    Zfb::Builder fbb;
    fbb.Finish(
      ZfbStruct::save<ZuFacet::Core, ZfFieldFilter::All>(fbb, data));
    auto fbo = ZfbStruct::verify<Ztc::QueueTelemetry>(
      {fbb.GetBufferPointer(), unsigned(fbb.GetSize())});
    ZuCheck(fbo && Zfb::Load::str(fbo->ownerID()) == data.ownerID &&
      Zfb::Load::str(fbo->id()) == data.id &&
      int(fbo->type()) == int(data.type));
    ZuCheck(fbo && fbo->inBytes() == data.inBytes &&
      fbo->outBytes() == data.outBytes && fbo->inCount() == data.inCount &&
      fbo->outCount() == data.outCount && fbo->count() == data.count);
    ZuCheck(fbo && fbo->size() == data.size && fbo->full() == data.full);
  }
  {
    Ztc::CxnTelemetry data;
    data.mxID = "mx";
    data.socket = UINT64_C(0xabcdef0123456789);
    data.rxCalls = UINT64_C(0x100000021);
    data.rxBytes = UINT64_C(0x200000022);
    data.txCalls = UINT64_C(0x300000023);
    data.txBytes = UINT64_C(0x400000024);
    data.rxBufSize = UINT32_C(0x80000021);
    data.rxBufLen = UINT32_C(0x70000022);
    data.txBufSize = UINT32_C(0x80000023);
    data.txBufLen = UINT32_C(0x70000024);
    data.mreqAddr = ZiIP{"239.1.2.3"};
    data.mreqIf = ZiIP{"10.1.2.3"};
    data.mreqIfIndex = UINT32_C(0x80000025);
    data.mif = ZiIP{"10.2.3.4"};
    data.mifIndex = UINT32_C(0x80000026);
    data.ttl = UINT32_C(0x80000027);
    data.localIP = ZiIP{"127.0.0.1"};
    data.remoteIP = ZiIP{"127.0.0.2"};
    data.localPort = UINT16_C(50001);
    data.remotePort = UINT16_C(50002);
    data.flags = 5;
    data.type = ZiCxnType::TCPOut;
    Zfb::Builder fbb;
    fbb.Finish(
      ZfbStruct::save<ZuFacet::Core, ZfFieldFilter::All>(fbb, data));
    auto fbo = ZfbStruct::verify<Ztc::CxnTelemetry>(
      {fbb.GetBufferPointer(), unsigned(fbb.GetSize())});
    auto loaded = ZfbStruct::ctor<Ztc::CxnTelemetry>(fbo);
    ZuCheck(fbo && fbo->socket() == data.socket &&
      fbo->rxCalls() == data.rxCalls && fbo->rxBytes() == data.rxBytes &&
      fbo->txCalls() == data.txCalls && fbo->txBytes() == data.txBytes);
    ZuCheck(fbo && fbo->rxBufSize() == data.rxBufSize &&
      fbo->rxBufLen() == data.rxBufLen &&
      fbo->txBufSize() == data.txBufSize &&
      fbo->txBufLen() == data.txBufLen);
    ZuCheck(loaded.mreqAddr == data.mreqAddr && loaded.mreqIf == data.mreqIf &&
      loaded.mif == data.mif && loaded.localIP == data.localIP &&
      loaded.remoteIP == data.remoteIP);
    ZuCheck(fbo && fbo->mreqIfIndex() == data.mreqIfIndex &&
      fbo->mifIndex() == data.mifIndex && fbo->ttl() == data.ttl &&
      fbo->localPort() == data.localPort &&
      fbo->remotePort() == data.remotePort && fbo->flags() == data.flags &&
      int(fbo->type()) == int(data.type));
  }
  {
    Ztc::MxTelemetry data;
    data.id = "mx";
    data.stackSize = UINT32_C(0x80000031);
    data.queueSize = UINT32_C(0x80000032);
    data.spin = UINT32_C(0x80000033);
    data.timeout = UINT32_C(0x80000034);
    data.rxBufSize = UINT32_C(0x80000035);
    data.txBufSize = UINT32_C(0x80000036);
    data.rxThread = UINT16_C(0x8004);
    data.txThread = UINT16_C(0x8005);
    data.partition = UINT16_C(0x8006);
    data.state = ZmEngineState::Running;
    data.ll = 1;
    data.priority = 7;
    data.nThreads = 9;
    Zfb::Builder fbb;
    fbb.Finish(
      ZfbStruct::save<ZuFacet::Core, ZfFieldFilter::All>(fbb, data));
    auto fbo = ZfbStruct::verify<Ztc::MxTelemetry>(
      {fbb.GetBufferPointer(), unsigned(fbb.GetSize())});
    ZuCheck(fbo && fbo->stackSize() == data.stackSize &&
      fbo->queueSize() == data.queueSize && fbo->spin() == data.spin &&
      fbo->timeout() == data.timeout);
    ZuCheck(fbo && fbo->rxBufSize() == data.rxBufSize &&
      fbo->txBufSize() == data.txBufSize &&
      fbo->rxThread() == data.rxThread && fbo->txThread() == data.txThread &&
      fbo->partition() == data.partition);
    ZuCheck(fbo && int(fbo->state()) == int(data.state) &&
      fbo->ll() == data.ll &&
      fbo->priority() == data.priority && fbo->nThreads() == data.nThreads);
  }
  {
    Ztc::HubTelemetry data;
    data.id = "hub";
    data.mxID = "mx";
    data.down = UINT16_C(0x8001);
    data.disabled = UINT16_C(0x8002);
    data.transient = UINT16_C(0x8003);
    data.up = UINT16_C(0x8004);
    data.reconn = UINT16_C(0x8005);
    data.failed = UINT16_C(0x8006);
    data.nLinks = UINT16_C(0x8007);
    data.rxThread = 201;
    data.txThread = 202;
    data.linkType = Ztc::LinkType::QUIC;
    data.state = ZmEngineState::Running;
    Zfb::Builder fbb;
    fbb.Finish(
      ZfbStruct::save<ZuFacet::Core, ZfFieldFilter::All>(fbb, data));
    auto fbo = ZfbStruct::verify<Ztc::HubTelemetry>(
      {fbb.GetBufferPointer(), unsigned(fbb.GetSize())});
    ZuCheck(fbo && Zfb::Load::str(fbo->id()) == data.id &&
      Zfb::Load::str(fbo->mxID()) == data.mxID &&
      fbo->down() == data.down && fbo->disabled() == data.disabled &&
      fbo->transient() == data.transient && fbo->up() == data.up);
    ZuCheck(fbo && fbo->reconn() == data.reconn &&
      fbo->failed() == data.failed && fbo->nLinks() == data.nLinks &&
      fbo->rxThread() == data.rxThread && fbo->txThread() == data.txThread &&
      int(fbo->linkType()) == int(data.linkType) &&
      int(fbo->state()) == int(data.state) &&
      fbo->rag() == Ztc::fbs::RAG::Green);
  }
  {
    Ztc::LinkTelemetry data;
    data.hubID = "hub";
    data.id = "link";
    data.rxCalls = UINT64_C(0x100000041);
    data.txCalls = UINT64_C(0x200000042);
    data.rxBytes = UINT64_C(0x300000043);
    data.txBytes = UINT64_C(0x400000044);
    data.reconnects = UINT32_C(0x80000045);
    data.type = Ztc::LinkType::TLS;
    data.state = Ztc::LinkState::Failed;
    Zfb::Builder fbb;
    fbb.Finish(
      ZfbStruct::save<ZuFacet::Core, ZfFieldFilter::All>(fbb, data));
    auto fbo = ZfbStruct::verify<Ztc::LinkTelemetry>(
      {fbb.GetBufferPointer(), unsigned(fbb.GetSize())});
    ZuCheck(fbo && Zfb::Load::str(fbo->hubID()) == data.hubID &&
      Zfb::Load::str(fbo->id()) == data.id &&
      fbo->rxCalls() == data.rxCalls && fbo->txCalls() == data.txCalls &&
      fbo->rxBytes() == data.rxBytes && fbo->txBytes() == data.txBytes);
    ZuCheck(fbo && fbo->reconnects() == data.reconnects &&
      int(fbo->type()) == int(data.type) &&
      int(fbo->state()) == int(data.state) &&
      fbo->rag() == Ztc::fbs::RAG::Red);
  }
  {
    Ztc::PoolTelemetry data;
    data.hubID = "hub";
    data.id = "pool";
    data.rxCalls = UINT64_C(0x100000051);
    data.rxBytes = UINT64_C(0x200000052);
    data.txCalls = UINT64_C(0x300000053);
    data.txBytes = UINT64_C(0x400000054);
    data.idle = UINT16_C(0x8008);
    data.busy = UINT16_C(0x8009);
    data.down = UINT16_C(0x800a);
    data.type = Ztc::LinkType::TCP;
    data.state = Ztc::PoolState::Failed;
    Zfb::Builder fbb;
    fbb.Finish(
      ZfbStruct::save<ZuFacet::Core, ZfFieldFilter::All>(fbb, data));
    auto fbo = ZfbStruct::verify<Ztc::PoolTelemetry>(
      {fbb.GetBufferPointer(), unsigned(fbb.GetSize())});
    ZuCheck(fbo && Zfb::Load::str(fbo->hubID()) == data.hubID &&
      Zfb::Load::str(fbo->id()) == data.id &&
      fbo->rxCalls() == data.rxCalls && fbo->rxBytes() == data.rxBytes &&
      fbo->txCalls() == data.txCalls && fbo->txBytes() == data.txBytes);
    ZuCheck(fbo && fbo->idle() == data.idle && fbo->busy() == data.busy &&
      fbo->down() == data.down && int(fbo->type()) == int(data.type) &&
      int(fbo->state()) == int(data.state) &&
      fbo->rag() == Ztc::fbs::RAG::Red);
  }
}

int main()
{
  ZuTestMain();
  ZuTestCall(app);
  ZuTestCall(alert);
  ZuTestCall(framing);
  ZuTestCall(goldenWire);
  ZuTestCall(telemetryUnions);
  ZuTestCall(rag);
  ZuTestCall(telemetryValues);
}
