//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <limits.h>
#include <string.h>

#include <zlib/ZuTestUtil.hh>


#include <zlib/ZtcDB.hh>
#include <zlib/ZtcFB.hh>
#include <zlib/ZtcMsg.hh>

using namespace ZuTestUtil;

void defaults()
{
  ZuTestScope(defaults);
  using ReqH = Zfb::As<Ztc::Request>::Handler<Ztc::Request>;
  using ErrH = Zfb::As<Ztc::Error>::Handler<Ztc::Error>;
  Ztc::Builder builder{new ZiIOBufAlloc<>()};
  auto table = builder.startTable();
  auto bytes = builder.finish(builder.endTable(table));
  auto loaded = Zfb::view<Ztc::Request>(bytes);
  ZuCheck(loaded.get<Ztc::ZfField(Request, seqNo)>() == ZuCmp<uint64_t>::null());
  builder.clear();
  table = builder.startTable();
  ReqH::field<Ztc::ZfField(Request, seqNo)>(builder, table, uint64_t(0));
  bytes = builder.finish(builder.endTable(table));
  loaded = Zfb::view<Ztc::Request>(bytes);
  ZuCheck(loaded.get<Ztc::ZfField(Request, seqNo)>() == 0);

  builder.clear();
  table = builder.startTable();
  bytes = builder.finish(builder.endTable(table));
  auto error = Zfb::view<Ztc::Error>(bytes);
  ZuCheck(error.get<Ztc::ZfField(Error, seqNo)>() == ZuCmp<uint64_t>::null());
  builder.clear();
  table = builder.startTable();
  ErrH::field<Ztc::ZfField(Error, seqNo)>(builder, table, uint64_t(0));
  bytes = builder.finish(builder.endTable(table));
  error = Zfb::view<Ztc::Error>(bytes);
  ZuCheck(error.get<Ztc::ZfField(Error, seqNo)>() == 0);

  Ztc::Error source{Ztc::ErrorMessage{"queue full"}, ZuID{"publisher"}, 7, 5};
  builder.clear();
  bytes = builder.finish(Zfb::save(builder, source));
  ZuCheck(Zfb::verify<Ztc::Error>(bytes));
  auto restored = Zfb::load<Ztc::Error>(bytes);
  ZuCheck(restored.id == source.id && restored.seqNo == source.seqNo &&
    restored.code == source.code && restored.message == source.message);

  builder.clear();
  auto empty = builder.string("");
  table = builder.startTable();
  ReqH::fieldOffset<Ztc::ZfField(Request, filter)>(builder, table, empty);
  bytes = builder.finish(builder.endTable(table));
  loaded = Zfb::view<Ztc::Request>(bytes);
  ZuCheck(loaded.has<Ztc::ZfField(Request, filter)>() &&
    !loaded.get<Ztc::ZfField(Request, filter)>() &&
    !loaded.has<Ztc::ZfField(Request, id)>());
}

void app()
{
  ZuTestScope(app);
  Ztc::AppTelemetry data;
  data.version = "10.0.0";
  data.role = "telemetry";
  data.startTime = ZuDateTime{2026, 10, 1, 12, 34, 56, 123456789};
  data.ztcver = ZuSemVer{uint32_t(12034056)};
  data.state = ZmEngineState::Running;
  data.degraded = true;
  data.rag = Ztc::RAG::Amber;

  Ztc::Builder fbb{new ZiIOBufAlloc<>()};
  auto bytes = fbb.finish(Zfb::save(fbb, data));
  ZuCheck(Zfb::verify<Ztc::AppTelemetry>(bytes));
  auto fbo = Zfb::view<Ztc::AppTelemetry>(bytes);
  ZuCheck(fbo);
  ZuCheck(fbo.get<Ztc::ZfField(AppTelemetry, version)>() == data.version);
  ZuCheck(fbo.get<Ztc::ZfField(AppTelemetry, role)>() == data.role);
  ZuCheck(fbo.has<Ztc::ZfField(AppTelemetry, startTime)>());
  ZuCheck(fbo.get<Ztc::ZfField(AppTelemetry, startTime)>().julian() == data.startTime.julian());
  ZuCheck(fbo.get<Ztc::ZfField(AppTelemetry, startTime)>().sec() == data.startTime.sec());
  ZuCheck(fbo.get<Ztc::ZfField(AppTelemetry, startTime)>().nsec() == data.startTime.nsec());
  ZuCheck(fbo.get<Ztc::ZfField(AppTelemetry, ztcver)>() == data.ztcver);
  ZuCheck(fbo.get<Ztc::ZfField(AppTelemetry, state)>() == data.state);
  ZuCheck(fbo.get<Ztc::ZfField(AppTelemetry, degraded)>() == data.degraded);
  ZuCheck(fbo.get<Ztc::ZfField(AppTelemetry, rag)>() == Ztc::RAG::Amber);

  auto loaded = Zfb::handler<Ztc::AppTelemetry>(fbo).ctor();
  ZuCheck(loaded.version == data.version);
  ZuCheck(loaded.role == data.role);
  ZuCheck(loaded.startTime == data.startTime);
  ZuCheck(loaded.ztcver == data.ztcver);
  ZuCheck(loaded.state == data.state);
  ZuCheck(loaded.degraded == data.degraded);
  ZuCheck(loaded.rag == data.rag);
}

void alert()
{
  ZuTestScope(alert);
  Ztc::AlertTelemetry data;
  data.message = "alert message";
  data.time = ZuTime{int64_t(0x123456789), 987654321};
  data.seqNo = uint64_t(0xfedcba9876543210);
  data.tid = uint64_t(0xabcdef0123456789);
  data.date = uint32_t(20991231);
  data.severity = -7;

  Ztc::Builder fbb{new ZiIOBufAlloc<>()};
  auto bytes = fbb.finish(Zfb::save(fbb, data));
  ZuCheck(Zfb::verify<Ztc::AlertTelemetry>(bytes));
  auto fbo = Zfb::view<Ztc::AlertTelemetry>(bytes);
  ZuCheck(fbo);
  ZuCheck(fbo.get<Ztc::ZfField(AlertTelemetry, message)>() == data.message);
  ZuCheck(fbo.get<Ztc::ZfField(AlertTelemetry, time)>());
  ZuTime time;
  if (fbo.get<Ztc::ZfField(AlertTelemetry, time)>())
    time = ZuTime{fbo.get<Ztc::ZfField(AlertTelemetry, time)>().sec(),
      fbo.get<Ztc::ZfField(AlertTelemetry, time)>().nsec()};
  ZuCheck(time == data.time);
  ZuCheck(fbo.get<Ztc::ZfField(AlertTelemetry, seqNo)>() == data.seqNo);
  ZuCheck(fbo.get<Ztc::ZfField(AlertTelemetry, tid)>() == data.tid);
  ZuCheck(fbo.get<Ztc::ZfField(AlertTelemetry, date)>() == data.date);
  ZuCheck(fbo.get<Ztc::ZfField(AlertTelemetry, severity)>() == data.severity);

  auto loaded = Zfb::handler<Ztc::AlertTelemetry>(fbo).ctor();
  ZuCheck(loaded.message == data.message);
  ZuCheck(loaded.time == data.time);
  ZuCheck(loaded.seqNo == data.seqNo);
  ZuCheck(loaded.tid == data.tid);
  ZuCheck(loaded.date == data.date);
  ZuCheck(loaded.severity == data.severity);

  Ztc::Builder msgBuilder{new ZiIOBufAlloc<>()};
  auto nested = msgBuilder.startNested();
  auto id = msgBuilder.string("app");
  auto value = Zfb::save(msgBuilder, data);
  auto telemetry = Ztc::saveTelemetry<true>(msgBuilder, id, 0,
    Ztc::TelemetryBody::AlertTelemetry, value);
  auto message = msgBuilder.finish(Ztc::saveMsg(msgBuilder, nested,
    Ztc::Body::Telemetry, telemetry));
  auto msg = Ztc::msg(message);
  ZuCheck(msg);
  auto tel = Ztc::msgBody(msg).get<Ztc::Body::Telemetry>();
  auto slot = tel.field<Ztc::ZfField(Telemetry, seqNo)>();
  ZuCheck(slot);
  if (slot) Zfb::write<uint64_t>(const_cast<uint8_t *>(slot), data.seqNo);
  ZuCheck(tel.get<Ztc::ZfField(Telemetry, seqNo)>() == data.seqNo);
}

void framing()
{
  ZuTestScope(framing);
  Ztc::Builder fbb{
    Ztc::frameBuf(ZmRef<ZiIOBuf>{new ZiIOBufAlloc<>})};
  auto nested = fbb.startNested();
  Ztc::Request data;
  data.filter = "app";
  data.id = "target";
  data.seqNo = uint64_t(0x123456789abcdef0);
  data.group = uint8_t(Ztc::Group::App);
  auto request = Zfb::save(fbb, data);
  fbb.finish(Ztc::saveMsg(
    fbb, nested, Ztc::Body::Request, request));

  auto body = fbb.data();
  auto bodyLength = unsigned(fbb.size());
  auto buf = Ztc::saveHdr(fbb);
  ZuCheck(buf);
  ZuCheck(buf->data() + sizeof(Ztc::Hdr) == body);
  ZuCheck(buf->length == bodyLength + sizeof(Ztc::Hdr));
  ZuCheck(Ztc::loadHdr(buf.ptr(), buf->length) == int(buf->length));

  auto hdr = buf->ptr<Ztc::Hdr>();
  auto msg = Ztc::msg(hdr);
  ZuCheck(msg);
  ZuCheck(Ztc::msgBody(msg).tag() == Ztc::Body::Request);
  auto loaded = Ztc::msgBody(msg).get<Ztc::Body::Request>();
  ZuCheck(loaded);
  ZuCheck(loaded.get<Ztc::ZfField(Request, seqNo)>() == uint64_t(0x123456789abcdef0));
  ZuCheck(loaded.get<Ztc::ZfField(Request, group)>() == Ztc::Group::App);
  ZuCheck(loaded.get<Ztc::ZfField(Request, filter)>() == "app");
  ZuCheck(loaded.get<Ztc::ZfField(Request, id)>() == "target");

  uint32_t length = hdr->length;
  hdr->length = UINT32_MAX;
  ZuCheck(Ztc::loadHdr(buf.ptr(), UINT32_MAX) < 0);
  hdr->length = length;

  buf->length = sizeof(Ztc::Hdr) - 1;
  ZuCheck(Ztc::loadHdr(buf.ptr(), UINT32_MAX) == INT_MAX);

  struct NoHeadroom {
    ZmRef<ZiIOBuf>	buf;
    unsigned		length;

    unsigned size() const { return length; }
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

  Ztc::Builder invalidOuter{Ztc::frameBuf(ZmRef<ZiIOBuf>{new ZiIOBufAlloc<>})};
  auto outerNested = invalidOuter.startNested();
  auto req = Zfb::save(invalidOuter, Ztc::Request{.seqNo = 2});
  invalidOuter.finish(Ztc::saveMsg(invalidOuter, outerNested,
    Ztc::Body::Request, req));
  auto invalidOuterBuf = Ztc::saveHdr(invalidOuter);
  auto outer = Ztc::msg_(invalidOuterBuf->ptr<Ztc::Hdr>());
  auto payload = Zfb::view<Ztc::Payload>(outer.get<Ztc::ZfField(Msg, data)>());
  auto tag = Zfb::TableView{payload.data()}.field(
    Zfb::As<Ztc::Payload>::template Handler<Ztc::Payload>::id<Ztc::ZfField(Payload, body)>() - 1);
  *const_cast<uint8_t *>(tag) = 127;
  ZuCheck(!Ztc::msg(invalidOuterBuf->ptr<Ztc::Hdr>()));

  Ztc::Builder invalidValue{Ztc::frameBuf(ZmRef<ZiIOBuf>{new ZiIOBufAlloc<>})};
  auto valueNested = invalidValue.startNested();
  auto id = invalidValue.string("app");
  auto value = Zfb::save(invalidValue, Ztc::AppTelemetry{});
  auto telemetry = Ztc::saveTelemetry(invalidValue, id, 3,
    Ztc::TelemetryBody::AppTelemetry, value);
  invalidValue.finish(Ztc::saveMsg(invalidValue, valueNested,
    Ztc::Body::Telemetry, telemetry));
  auto invalidValueBuf = Ztc::saveHdr(invalidValue);
  auto tel = Ztc::msgBody(Ztc::msg_(invalidValueBuf->ptr<Ztc::Hdr>()))
    .get<Ztc::Body::Telemetry>();
  tag = Zfb::TableView{tel.data()}.field(
    Zfb::As<Ztc::Telemetry>::template Handler<Ztc::Telemetry>::id<Ztc::ZfField(Telemetry, value)>() - 1);
  *const_cast<uint8_t *>(tag) = 127;
  ZuCheck(!Ztc::msg(invalidValueBuf->ptr<Ztc::Hdr>()));
}

void requestFields()
{
  ZuTestScope(requestFields);
  Ztc::Builder builder{Ztc::frameBuf(ZmRef<ZiIOBuf>{new ZiIOBufAlloc<>})};
  auto nested = builder.startNested();
  Ztc::Request data{
    .filter = "Tx:hub:link*", .seqNo = uint64_t(0x123456789abcdef0),
    .alertSeqNo = uint64_t(0xfedcba9876543210), .interval = 0x10203040,
    .alertDate = 20260731, .group = Ztc::Group::Queue, .subscribe = true};
  auto request = Zfb::save(builder, data);
  builder.finish(Ztc::saveMsg(builder, nested, Ztc::Body::Request, request));
  auto buf = Ztc::saveHdr(builder);
  ZuCheck(buf);
  auto msg = Ztc::msg(buf->ptr<Ztc::Hdr>());
  ZuCheck(msg);
  auto loaded = Ztc::msgBody(msg).get<Ztc::Body::Request>();
  ZuCheck(loaded.get<Ztc::ZfField(Request, seqNo)>() == data.seqNo &&
    loaded.get<Ztc::ZfField(Request, group)>() == data.group &&
    loaded.get<Ztc::ZfField(Request, filter)>() == data.filter &&
    loaded.get<Ztc::ZfField(Request, interval)>() == data.interval &&
    loaded.get<Ztc::ZfField(Request, subscribe)>() &&
    loaded.get<Ztc::ZfField(Request, alertDate)>() == data.alertDate &&
    loaded.get<Ztc::ZfField(Request, alertSeqNo)>() == data.alertSeqNo &&
    !loaded.get<Ztc::ZfField(Request, id)>());
}

template <typename T>
bool unionValue(unsigned type, const T &data)
{
  Ztc::Builder builder{new ZiIOBufAlloc<>()};
  auto nested = builder.startNested();
  auto id = builder.string("app");
  auto value = Zfb::save<ZuFacet::FB, ZfFieldFilter::All>(builder, data);
  auto telemetry = Ztc::saveTelemetry(
    builder, id, uint64_t(0xabcdef0123456789), type, value);
  auto bytes = builder.finish(Ztc::saveMsg(builder, nested,
    Ztc::Body::Telemetry, telemetry));
  auto msg = Ztc::msg(bytes);
  if (!msg) return false;
  auto loaded = Ztc::msgBody(msg).template get<Ztc::Body::Telemetry>();
  auto payload = loaded.template get<Ztc::ZfField(Telemetry, value)>();
  return loaded.template get<Ztc::ZfField(Telemetry, id)>() == "app" &&
    loaded.template get<Ztc::ZfField(Telemetry, seqNo)>() == uint64_t(0xabcdef0123456789) &&
    payload.tag() == type && payload.data();
}

void telemetryUnions()
{
  ZuTestScope(telemetryUnions);
  ZuCheck(unionValue(Ztc::TelemetryBody::HeapTelemetry,
    Ztc::HeapTelemetry{}));
  ZuCheck(unionValue(Ztc::TelemetryBody::HashTelemetry,
    Ztc::HashTelemetry{}));
  ZuCheck(unionValue(Ztc::TelemetryBody::ThreadTelemetry,
    Ztc::ThreadTelemetry{}));
  ZuCheck(unionValue(Ztc::TelemetryBody::CxnTelemetry,
    Ztc::CxnTelemetry{}));
  ZuCheck(unionValue(Ztc::TelemetryBody::MxTelemetry,
    Ztc::MxTelemetry{}));
  ZuCheck(unionValue(Ztc::TelemetryBody::QueueTelemetry,
    Ztc::QueueTelemetry{}));
  ZuCheck(unionValue(Ztc::TelemetryBody::HubTelemetry,
    Ztc::HubTelemetry{}));
  ZuCheck(unionValue(Ztc::TelemetryBody::LinkTelemetry,
    Ztc::LinkTelemetry{}));
  ZuCheck(unionValue(Ztc::TelemetryBody::PoolTelemetry,
    Ztc::PoolTelemetry{}));
  ZuCheck(unionValue(Ztc::TelemetryBody::DBTelemetry,
    Ztc::DBTelemetry{}));
  ZuCheck(unionValue(Ztc::TelemetryBody::DBHostTelemetry,
    Ztc::DBHostTelemetry{}));
  ZuCheck(unionValue(Ztc::TelemetryBody::DBTableTelemetry,
    Ztc::DBTableTelemetry{}));
  ZuCheck(unionValue(Ztc::TelemetryBody::AppTelemetry,
    Ztc::AppTelemetry{}));
  ZuCheck(unionValue(Ztc::TelemetryBody::AlertTelemetry,
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
  heap.globalHeapMax = 1;
  ZuCheck(heap.rag() == Ztc::RAG::Amber);
  heap.cacheAllocs = 100;
  ZuCheck(heap.rag() == Ztc::RAG::Red);
  ZuCheck(heap.allocated() == 101);
  heap.heapFrees = 1;
  ZuCheck(heap.allocated() == 100);
  ZuCheck(heap.rag() == Ztc::RAG::Amber);
  heap.cacheFrees = 100;
  ZuCheck(!heap.allocated());

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
    data.cacheSize = uint64_t(0x100000001);
    data.cpuset[3] = true;
    data.cacheAllocs = uint64_t(0x200000002);
    data.heapAllocs = uint64_t(0x300000003);
    data.cacheFrees = uint64_t(0x100000001);
    data.heapFrees = uint64_t(0x100000002);
    data.crossFrees = uint64_t(0x400000004);
    data.globalHeapAllocs = uint64_t(0x600000006);
    data.globalHeapFrees = uint64_t(0x700000007);
    data.globalHeapMax = uint64_t(0x500000005);
    data.size = uint32_t(0x80000001);
    data.partition = uint16_t(0x8001);
    data.sharded = 3;
    data.alignment = 64;
    Ztc::Builder fbb{new ZiIOBufAlloc<>()};
    fbb.finish(
      Zfb::save<ZuFacet::FB, ZfFieldFilter::All>(fbb, data));
    auto fbo = Zfb::verify<Ztc::HeapTelemetry>(fbb.span()) ?
      Zfb::view<Ztc::HeapTelemetry>(fbb.span()) : Zfb::View<Ztc::HeapTelemetry>{};
    auto loaded = Zfb::handler<Ztc::HeapTelemetry>(fbo).ctor();
    ZuCheck(fbo && fbo.get<Ztc::ZfField(HeapTelemetry, cacheSize)>() == data.cacheSize &&
      fbo.get<Ztc::ZfField(HeapTelemetry, crossFrees)>() == data.crossFrees);
    ZuCheck(fbo && fbo.get<Ztc::ZfField(HeapTelemetry, size)>() == data.size &&
      fbo.get<Ztc::ZfField(HeapTelemetry, partition)>() == data.partition &&
      fbo.get<Ztc::ZfField(HeapTelemetry, alignment)>() == data.alignment);
    ZuCheck(loaded.id == data.id && loaded.cpuset[3] &&
      loaded.cacheAllocs == data.cacheAllocs &&
      loaded.heapAllocs == data.heapAllocs && loaded.cacheFrees == data.cacheFrees);
    ZuCheck(fbo && fbo.get<Ztc::ZfField(HeapTelemetry, heapFrees)>() == data.heapFrees &&
      fbo.get<Ztc::ZfField(HeapTelemetry, globalHeapMax)>() == data.globalHeapMax);
    ZuCheck(loaded.heapFrees == data.heapFrees &&
      loaded.crossFrees == data.crossFrees && loaded.globalHeapMax == data.globalHeapMax);
    ZuCheck(fbo &&
      fbo.get<Ztc::ZfField(HeapTelemetry, globalHeapAllocs)>() == data.globalHeapAllocs &&
      fbo.get<Ztc::ZfField(HeapTelemetry, globalHeapFrees)>() == data.globalHeapFrees);
    ZuCheck(loaded.globalHeapAllocs == data.globalHeapAllocs &&
      loaded.globalHeapFrees == data.globalHeapFrees);
    ZuCheck(loaded.size == data.size && loaded.partition == data.partition &&
      loaded.sharded == data.sharded && loaded.alignment == data.alignment);
    ZuCheck(fbo && fbo.get<Ztc::ZfField(HeapTelemetry, allocated)>() == data.allocated() &&
      fbo.get<Ztc::ZfField(HeapTelemetry, rag)>() == Ztc::RAG::Red);
  }
  {
    Ztc::HashTelemetry data;
    data.id = "hash";
    data.addr = uintptr_t(uint64_t(0xabcdef0123456789));
    data.loadFactor = 1.25;
    data.effLoadFactor = 0.75;
    data.count = uint64_t(0x100000005);
    data.maxCount = uint64_t(0x200000009);
    data.nodeSize = uint32_t(0x80000005);
    data.resized = 7;
    data.bits = 17;
    data.cBits = 9;
    data.linear = 1;
    data.shadow = 2;
    Ztc::Builder fbb{new ZiIOBufAlloc<>()};
    fbb.finish(
      Zfb::save<ZuFacet::FB, ZfFieldFilter::All>(fbb, data));
    auto fbo = Zfb::verify<Ztc::HashTelemetry>(fbb.span()) ?
      Zfb::view<Ztc::HashTelemetry>(fbb.span()) : Zfb::View<Ztc::HashTelemetry>{};
    ZuCheck(fbo && fbo.get<Ztc::ZfField(HashTelemetry, addr)>() == data.addr &&
      fbo.get<Ztc::ZfField(HashTelemetry, count)>() == data.count &&
	fbo.get<Ztc::ZfField(HashTelemetry, nodeSize)>() == data.nodeSize);
    ZuCheck(fbo && fbo.get<Ztc::ZfField(HashTelemetry, maxCount)>() == data.maxCount);
    if (fbo) {
      auto loaded = Zfb::handler<Ztc::HashTelemetry>(fbo).ctor();
      ZuCheck(loaded.count == data.count && loaded.maxCount == data.maxCount);
      loaded.count = 0;
      loaded.effLoadFactor = 0;
      Ztc::Builder drained{new ZiIOBufAlloc<>()};
      drained.finish(
	Zfb::save<ZuFacet::FB, ZfFieldFilter::All>(drained, loaded));
      auto roundtrip = Zfb::verify<Ztc::HashTelemetry>(drained.span()) ?
      Zfb::view<Ztc::HashTelemetry>(drained.span()) : Zfb::View<Ztc::HashTelemetry>{};
      ZuCheck(roundtrip && !roundtrip.get<Ztc::ZfField(HashTelemetry, count)>() &&
	roundtrip.get<Ztc::ZfField(HashTelemetry, maxCount)>() == data.maxCount);
    }
    ZuCheck(fbo && fbo.get<Ztc::ZfField(HashTelemetry, loadFactor)>() == data.loadFactor &&
      fbo.get<Ztc::ZfField(HashTelemetry, effLoadFactor)>() == data.effLoadFactor &&
      fbo.get<Ztc::ZfField(HashTelemetry, resized)>() == data.resized);
    ZuCheck(fbo && fbo.get<Ztc::ZfField(HashTelemetry, bits)>() == data.bits &&
      fbo.get<Ztc::ZfField(HashTelemetry, cBits)>() == data.cBits &&
	fbo.get<Ztc::ZfField(HashTelemetry, linear)>() == data.linear &&
      fbo.get<Ztc::ZfField(HashTelemetry, shadow)>() == data.shadow &&
	fbo.get<Ztc::ZfField(HashTelemetry, rag)>() == Ztc::RAG::Red);
  }
  {
    Ztc::ThreadTelemetry data;
    data.name = "thread";
    data.tid = uint64_t(0xabcdef0123456789);
    data.stackSize = uint64_t(0x100000006);
    data.cpuset[5] = true;
    data.cpuUsage = 0.625;
    data.allocStack = uint64_t(0x200000006);
    data.allocHeap = uint64_t(0x300000006);
    data.sysPriority = INT32_MIN + 7;
    data.sid = uint16_t(0x8002);
    data.partition = uint16_t(0x8003);
    data.priority = ZmThreadPriority::High;
    data.main = true;
    data.detached = true;
    Ztc::Builder fbb{new ZiIOBufAlloc<>()};
    fbb.finish(
      Zfb::save<ZuFacet::FB, ZfFieldFilter::All>(fbb, data));
    auto fbo = Zfb::verify<Ztc::ThreadTelemetry>(fbb.span()) ?
      Zfb::view<Ztc::ThreadTelemetry>(fbb.span()) : Zfb::View<Ztc::ThreadTelemetry>{};
    auto loaded = Zfb::handler<Ztc::ThreadTelemetry>(fbo).ctor();
    ZuCheck(fbo && fbo.get<Ztc::ZfField(ThreadTelemetry, tid)>() == data.tid &&
      fbo.get<Ztc::ZfField(ThreadTelemetry, stackSize)>() == data.stackSize &&
	fbo.get<Ztc::ZfField(ThreadTelemetry, sid)>() == data.sid &&
      fbo.get<Ztc::ZfField(ThreadTelemetry, partition)>() == data.partition);
    ZuCheck(fbo && fbo.get<Ztc::ZfField(ThreadTelemetry, cpuUsage)>() == data.cpuUsage &&
      fbo.get<Ztc::ZfField(ThreadTelemetry, allocStack)>() == data.allocStack &&
      fbo.get<Ztc::ZfField(ThreadTelemetry, allocHeap)>() == data.allocHeap &&
      fbo.get<Ztc::ZfField(ThreadTelemetry, sysPriority)>() == data.sysPriority);
    ZuCheck(loaded.cpuset[5] && loaded.priority == data.priority &&
      loaded.main && loaded.detached);
  }
  {
    Ztc::QueueTelemetry data;
    data.ownerID = "owner";
    data.id = "queue";
    data.inBytes = uint64_t(0x100000011);
    data.outBytes = uint64_t(0x200000012);
    data.inCount = uint64_t(0x300000013);
    data.outCount = uint64_t(0x400000014);
    data.count = uint64_t(0x500000015);
    data.size = uint32_t(0x80000015);
    data.full = uint32_t(0x80000016);
    data.type = Ztc::QueueType::Tx;
    Ztc::Builder fbb{new ZiIOBufAlloc<>()};
    fbb.finish(
      Zfb::save<ZuFacet::FB, ZfFieldFilter::All>(fbb, data));
    auto fbo = Zfb::verify<Ztc::QueueTelemetry>(fbb.span()) ?
      Zfb::view<Ztc::QueueTelemetry>(fbb.span()) : Zfb::View<Ztc::QueueTelemetry>{};
    ZuCheck(fbo && fbo.get<Ztc::ZfField(QueueTelemetry, ownerID)>() == data.ownerID &&
      fbo.get<Ztc::ZfField(QueueTelemetry, id)>() == data.id &&
      int(fbo.get<Ztc::ZfField(QueueTelemetry, type)>()) == int(data.type));
    ZuCheck(fbo && fbo.get<Ztc::ZfField(QueueTelemetry, inBytes)>() == data.inBytes &&
      fbo.get<Ztc::ZfField(QueueTelemetry, outBytes)>() == data.outBytes &&
	fbo.get<Ztc::ZfField(QueueTelemetry, inCount)>() == data.inCount &&
      fbo.get<Ztc::ZfField(QueueTelemetry, outCount)>() == data.outCount &&
	fbo.get<Ztc::ZfField(QueueTelemetry, count)>() == data.count);
    ZuCheck(fbo && fbo.get<Ztc::ZfField(QueueTelemetry, size)>() == data.size &&
      fbo.get<Ztc::ZfField(QueueTelemetry, full)>() == data.full);
  }
  {
    Ztc::CxnTelemetry data;
    data.mxID = "mx";
    data.socket = uint64_t(0xabcdef0123456789);
    data.rxCalls = uint64_t(0x100000021);
    data.rxBytes = uint64_t(0x200000022);
    data.txCalls = uint64_t(0x300000023);
    data.txBytes = uint64_t(0x400000024);
    data.rxBufSize = uint32_t(0x80000021);
    data.rxBufLen = uint32_t(0x70000022);
    data.txBufSize = uint32_t(0x80000023);
    data.txBufLen = uint32_t(0x70000024);
    data.mreqAddr = ZiIP{"239.1.2.3"};
    data.mreqIf = ZiIP{"10.1.2.3"};
    data.mreqIfIndex = uint32_t(0x80000025);
    data.mif = ZiIP{"10.2.3.4"};
    data.mifIndex = uint32_t(0x80000026);
    data.ttl = uint32_t(0x80000027);
    data.localIP = ZiIP{"127.0.0.1"};
    data.remoteIP = ZiIP{"127.0.0.2"};
    data.localPort = uint16_t(50001);
    data.remotePort = uint16_t(50002);
    data.flags = 5;
    data.type = ZiCxnType::TCPOut;
    Ztc::Builder fbb{new ZiIOBufAlloc<>()};
    fbb.finish(
      Zfb::save<ZuFacet::FB, ZfFieldFilter::All>(fbb, data));
    auto fbo = Zfb::verify<Ztc::CxnTelemetry>(fbb.span()) ?
      Zfb::view<Ztc::CxnTelemetry>(fbb.span()) : Zfb::View<Ztc::CxnTelemetry>{};
    auto loaded = Zfb::handler<Ztc::CxnTelemetry>(fbo).ctor();
    ZuCheck(fbo && fbo.get<Ztc::ZfField(CxnTelemetry, socket)>() == data.socket &&
      fbo.get<Ztc::ZfField(CxnTelemetry, rxCalls)>() == data.rxCalls &&
	fbo.get<Ztc::ZfField(CxnTelemetry, rxBytes)>() == data.rxBytes &&
      fbo.get<Ztc::ZfField(CxnTelemetry, txCalls)>() == data.txCalls &&
	fbo.get<Ztc::ZfField(CxnTelemetry, txBytes)>() == data.txBytes);
    ZuCheck(fbo && fbo.get<Ztc::ZfField(CxnTelemetry, rxBufSize)>() == data.rxBufSize &&
      fbo.get<Ztc::ZfField(CxnTelemetry, rxBufLen)>() == data.rxBufLen &&
      fbo.get<Ztc::ZfField(CxnTelemetry, txBufSize)>() == data.txBufSize &&
      fbo.get<Ztc::ZfField(CxnTelemetry, txBufLen)>() == data.txBufLen);
    ZuCheck(loaded.mreqAddr == data.mreqAddr && loaded.mreqIf == data.mreqIf &&
      loaded.mif == data.mif && loaded.localIP == data.localIP &&
      loaded.remoteIP == data.remoteIP);
    ZuCheck(fbo && fbo.get<Ztc::ZfField(CxnTelemetry, mreqIfIndex)>() == data.mreqIfIndex &&
      fbo.get<Ztc::ZfField(CxnTelemetry, mifIndex)>() == data.mifIndex &&
	fbo.get<Ztc::ZfField(CxnTelemetry, ttl)>() == data.ttl &&
      fbo.get<Ztc::ZfField(CxnTelemetry, localPort)>() == data.localPort &&
      fbo.get<Ztc::ZfField(CxnTelemetry, remotePort)>() == data.remotePort &&
	fbo.get<Ztc::ZfField(CxnTelemetry, flags)>() == data.flags &&
      int(fbo.get<Ztc::ZfField(CxnTelemetry, type)>()) == int(data.type));
  }
  {
    Ztc::MxTelemetry data;
    data.id = "mx";
    data.stackSize = uint32_t(0x80000031);
    data.queueSize = uint32_t(0x80000032);
    data.spin = uint32_t(0x80000033);
    data.timeout = uint32_t(0x80000034);
    data.rxBufSize = uint32_t(0x80000035);
    data.txBufSize = uint32_t(0x80000036);
    data.rxThread = uint16_t(0x8004);
    data.txThread = uint16_t(0x8005);
    data.partition = uint16_t(0x8006);
    data.state = ZmEngineState::Running;
    data.ll = 1;
    data.priority = 7;
    data.nThreads = 9;
    Ztc::Builder fbb{new ZiIOBufAlloc<>()};
    fbb.finish(
      Zfb::save<ZuFacet::FB, ZfFieldFilter::All>(fbb, data));
    auto fbo = Zfb::verify<Ztc::MxTelemetry>(fbb.span()) ?
      Zfb::view<Ztc::MxTelemetry>(fbb.span()) : Zfb::View<Ztc::MxTelemetry>{};
    ZuCheck(fbo && fbo.get<Ztc::ZfField(MxTelemetry, stackSize)>() == data.stackSize &&
      fbo.get<Ztc::ZfField(MxTelemetry, queueSize)>() == data.queueSize &&
	fbo.get<Ztc::ZfField(MxTelemetry, spin)>() == data.spin &&
      fbo.get<Ztc::ZfField(MxTelemetry, timeout)>() == data.timeout);
    ZuCheck(fbo && fbo.get<Ztc::ZfField(MxTelemetry, rxBufSize)>() == data.rxBufSize &&
      fbo.get<Ztc::ZfField(MxTelemetry, txBufSize)>() == data.txBufSize &&
      fbo.get<Ztc::ZfField(MxTelemetry, rxThread)>() == data.rxThread &&
	fbo.get<Ztc::ZfField(MxTelemetry, txThread)>() == data.txThread &&
      fbo.get<Ztc::ZfField(MxTelemetry, partition)>() == data.partition);
    ZuCheck(fbo && int(fbo.get<Ztc::ZfField(MxTelemetry, state)>()) == int(data.state) &&
      fbo.get<Ztc::ZfField(MxTelemetry, ll)>() == data.ll &&
      fbo.get<Ztc::ZfField(MxTelemetry, priority)>() == data.priority &&
	fbo.get<Ztc::ZfField(MxTelemetry, nThreads)>() == data.nThreads);
  }
  {
    Ztc::HubTelemetry data;
    data.id = "hub";
    data.mxID = "mx";
    data.down = uint16_t(0x8001);
    data.disabled = uint16_t(0x8002);
    data.transient = uint16_t(0x8003);
    data.up = uint16_t(0x8004);
    data.reconn = uint16_t(0x8005);
    data.failed = uint16_t(0x8006);
    data.nLinks = uint16_t(0x8007);
    data.rxThread = 201;
    data.txThread = 202;
    data.linkType = Ztc::LinkType::QUIC;
    data.state = ZmEngineState::Running;
    Ztc::Builder fbb{new ZiIOBufAlloc<>()};
    fbb.finish(
      Zfb::save<ZuFacet::FB, ZfFieldFilter::All>(fbb, data));
    auto fbo = Zfb::verify<Ztc::HubTelemetry>(fbb.span()) ?
      Zfb::view<Ztc::HubTelemetry>(fbb.span()) : Zfb::View<Ztc::HubTelemetry>{};
    ZuCheck(fbo && fbo.get<Ztc::ZfField(HubTelemetry, id)>() == data.id &&
      fbo.get<Ztc::ZfField(HubTelemetry, mxID)>() == data.mxID &&
      fbo.get<Ztc::ZfField(HubTelemetry, down)>() == data.down &&
	fbo.get<Ztc::ZfField(HubTelemetry, disabled)>() == data.disabled &&
      fbo.get<Ztc::ZfField(HubTelemetry, transient)>() == data.transient &&
	fbo.get<Ztc::ZfField(HubTelemetry, up)>() == data.up);
    ZuCheck(fbo && fbo.get<Ztc::ZfField(HubTelemetry, reconn)>() == data.reconn &&
      fbo.get<Ztc::ZfField(HubTelemetry, failed)>() == data.failed &&
	fbo.get<Ztc::ZfField(HubTelemetry, nLinks)>() == data.nLinks &&
      fbo.get<Ztc::ZfField(HubTelemetry, rxThread)>() == data.rxThread &&
	fbo.get<Ztc::ZfField(HubTelemetry, txThread)>() == data.txThread &&
      int(fbo.get<Ztc::ZfField(HubTelemetry, linkType)>()) == int(data.linkType) &&
      int(fbo.get<Ztc::ZfField(HubTelemetry, state)>()) == int(data.state) &&
      fbo.get<Ztc::ZfField(HubTelemetry, rag)>() == Ztc::RAG::Green);
  }
  {
    Ztc::LinkTelemetry data;
    data.hubID = "hub";
    data.id = "link";
    data.rxCalls = uint64_t(0x100000041);
    data.txCalls = uint64_t(0x200000042);
    data.rxBytes = uint64_t(0x300000043);
    data.txBytes = uint64_t(0x400000044);
    data.reconnects = uint32_t(0x80000045);
    data.type = Ztc::LinkType::TLS;
    data.state = Ztc::LinkState::Failed;
    Ztc::Builder fbb{new ZiIOBufAlloc<>()};
    fbb.finish(
      Zfb::save<ZuFacet::FB, ZfFieldFilter::All>(fbb, data));
    auto fbo = Zfb::verify<Ztc::LinkTelemetry>(fbb.span()) ?
      Zfb::view<Ztc::LinkTelemetry>(fbb.span()) : Zfb::View<Ztc::LinkTelemetry>{};
    ZuCheck(fbo && fbo.get<Ztc::ZfField(LinkTelemetry, hubID)>() == data.hubID &&
      fbo.get<Ztc::ZfField(LinkTelemetry, id)>() == data.id &&
      fbo.get<Ztc::ZfField(LinkTelemetry, rxCalls)>() == data.rxCalls &&
	fbo.get<Ztc::ZfField(LinkTelemetry, txCalls)>() == data.txCalls &&
      fbo.get<Ztc::ZfField(LinkTelemetry, rxBytes)>() == data.rxBytes &&
	fbo.get<Ztc::ZfField(LinkTelemetry, txBytes)>() == data.txBytes);
    ZuCheck(fbo && fbo.get<Ztc::ZfField(LinkTelemetry, reconnects)>() == data.reconnects &&
      int(fbo.get<Ztc::ZfField(LinkTelemetry, type)>()) == int(data.type) &&
      int(fbo.get<Ztc::ZfField(LinkTelemetry, state)>()) == int(data.state) &&
      fbo.get<Ztc::ZfField(LinkTelemetry, rag)>() == Ztc::RAG::Red);
  }
  {
    Ztc::PoolTelemetry data;
    data.hubID = "hub";
    data.id = "pool";
    data.rxCalls = uint64_t(0x100000051);
    data.rxBytes = uint64_t(0x200000052);
    data.txCalls = uint64_t(0x300000053);
    data.txBytes = uint64_t(0x400000054);
    data.idle = uint16_t(0x8008);
    data.busy = uint16_t(0x8009);
    data.down = uint16_t(0x800a);
    data.type = Ztc::LinkType::TCP;
    data.state = Ztc::PoolState::Failed;
    Ztc::Builder fbb{new ZiIOBufAlloc<>()};
    fbb.finish(
      Zfb::save<ZuFacet::FB, ZfFieldFilter::All>(fbb, data));
    auto fbo = Zfb::verify<Ztc::PoolTelemetry>(fbb.span()) ?
      Zfb::view<Ztc::PoolTelemetry>(fbb.span()) : Zfb::View<Ztc::PoolTelemetry>{};
    ZuCheck(fbo && fbo.get<Ztc::ZfField(PoolTelemetry, hubID)>() == data.hubID &&
      fbo.get<Ztc::ZfField(PoolTelemetry, id)>() == data.id &&
      fbo.get<Ztc::ZfField(PoolTelemetry, rxCalls)>() == data.rxCalls &&
	fbo.get<Ztc::ZfField(PoolTelemetry, rxBytes)>() == data.rxBytes &&
      fbo.get<Ztc::ZfField(PoolTelemetry, txCalls)>() == data.txCalls &&
	fbo.get<Ztc::ZfField(PoolTelemetry, txBytes)>() == data.txBytes);
    ZuCheck(fbo && fbo.get<Ztc::ZfField(PoolTelemetry, idle)>() == data.idle &&
      fbo.get<Ztc::ZfField(PoolTelemetry, busy)>() == data.busy &&
      fbo.get<Ztc::ZfField(PoolTelemetry, down)>() == data.down &&
	int(fbo.get<Ztc::ZfField(PoolTelemetry, type)>()) == int(data.type) &&
      int(fbo.get<Ztc::ZfField(PoolTelemetry, state)>()) == int(data.state) &&
      fbo.get<Ztc::ZfField(PoolTelemetry, rag)>() == Ztc::RAG::Red);
  }
}

int main()
{
  ZuTestMain();
  ZuTestCall(defaults);
  ZuTestCall(app);
  ZuTestCall(alert);
  ZuTestCall(framing);
  ZuTestCall(requestFields);
  ZuTestCall(telemetryUnions);
  ZuTestCall(rag);
  ZuTestCall(telemetryValues);
}
