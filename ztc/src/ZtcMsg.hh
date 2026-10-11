//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// telemetry service framing and message verification

#ifndef ZtcMsg_HH
#define ZtcMsg_HH

#ifndef ZtcLib_HH
#include <zlib/ZtcLib.hh>
#endif

#include <limits.h>
#include <new>
#include <stdint.h>

#include <zlib/ZuByteSwap.hh>
#include <zlib/ZuSpan.hh>

#include <zlib/ZmRef.hh>

#include <zlib/ZtBuiltin.hh>

#include <zlib/ZtcFB.hh>
#include <zlib/ZtcDB.hh>

namespace Ztc {

namespace HubError {
  using T = int8_t;
  enum {
    NoAgent = 1,
    DuplicateSub,
    BadReq,
    AgentGone,
    Overflow,
    Unauthorized,
    SnapshotFailed
  };
}

inline constexpr auto Protocol = "ztc"_z;

struct Shutdown { };
ZfStruct(ZtcAPI, Shutdown);
ZiFB::Table<"Ztc.fbs.Shutdown"> ZiFBType(Shutdown *);

namespace TelemetryBody {
  enum {
    None, HeapTelemetry, HashTelemetry, ThreadTelemetry, CxnTelemetry,
    MxTelemetry, QueueTelemetry, HubTelemetry, LinkTelemetry, PoolTelemetry,
    DBTelemetry, DBHostTelemetry, DBTableTelemetry, AppTelemetry, Shutdown,
    AlertTelemetry
  };
}
ZuDerive(TelemetryValue, (ZuUnion<void,
  HeapTelemetry, HashTelemetry, ThreadTelemetry, CxnTelemetry,
  MxTelemetry, QueueTelemetry, HubTelemetry, LinkTelemetry, PoolTelemetry,
  DBTelemetry, DBHostTelemetry, DBTableTelemetry, AppTelemetry, Shutdown,
  AlertTelemetry>));
ZiFB::Union<"Ztc.fbs.TelemetryBody",
  ZiFB::Alt<"HeapTelemetry", HeapTelemetry>,
  ZiFB::Alt<"HashTelemetry", HashTelemetry>,
  ZiFB::Alt<"ThreadTelemetry", ThreadTelemetry>,
  ZiFB::Alt<"CxnTelemetry", CxnTelemetry>,
  ZiFB::Alt<"MxTelemetry", MxTelemetry>,
  ZiFB::Alt<"QueueTelemetry", QueueTelemetry>,
  ZiFB::Alt<"HubTelemetry", HubTelemetry>,
  ZiFB::Alt<"LinkTelemetry", LinkTelemetry>,
  ZiFB::Alt<"PoolTelemetry", PoolTelemetry>,
  ZiFB::Alt<"DBTelemetry", DBTelemetry>,
  ZiFB::Alt<"DBHostTelemetry", DBHostTelemetry>,
  ZiFB::Alt<"DBTableTelemetry", DBTableTelemetry>,
  ZiFB::Alt<"AppTelemetry", AppTelemetry>,
  ZiFB::Alt<"Shutdown", Shutdown>,
  ZiFB::Alt<"AlertTelemetry", AlertTelemetry>> ZiFBType(TelemetryValue *);
struct Telemetry {
  ZuCSpan id;
  uint64_t seqNo;
  TelemetryValue value;
};
ZfStruct(ZtcAPI, Telemetry,
  (id,,				String),
  (seqNo,	(Deflt<0>),	UInt64),
  (value,,			UDT));
ZiFB::Table<"Ztc.fbs.Telemetry"> ZiFBType(Telemetry *);

namespace Body { enum { None, Request, Ack, Telemetry, EOS, Error }; }
ZuDerive(BodyValue, (ZuUnion<void, Request, Ack, Telemetry, EOS, Error>));
ZiFB::Union<"Ztc.fbs.Body",
  ZiFB::Alt<"Request", Request>, ZiFB::Alt<"Ack", Ack>,
  ZiFB::Alt<"Telemetry", Telemetry>, ZiFB::Alt<"EOS", EOS>,
  ZiFB::Alt<"Error", Error>> ZiFBType(BodyValue *);
struct Payload { BodyValue body; };
ZfStruct(ZtcAPI, Payload, (body,, UDT));
ZiFB::Table<"Ztc.fbs.Payload"> ZiFBType(Payload *);
struct Msg {
  ZuBSpan data;
  uint64_t subID;
  ZuCSpan deviceID;
  uint64_t agentGen;
};
ZfStruct(ZtcAPI, Msg,
  (data,,			Bytes),
  (subID,	(Deflt<0>),	UInt64),
  (deviceID,,			String),
  (agentGen,	(Deflt<0>),	UInt64));
ZiFB::Table<"Ztc.fbs.Msg"> ZiFBType(Msg *);
using MsgView = ZiFB::View<Msg>;
using TelemetryView = ZiFB::View<Telemetry>;
using Builder = ZiFB::IOBuilder<ZiFB::BufferAlign<Payload>::N>;

// Trusted access after ingress verification, or to a locally generated span.
inline auto msgBody(MsgView msg) {
  return ZiFB::view<Payload>(msg.get<ZfField(Msg, data)>())
    .get<ZfField(Payload, body)>();
}

template <typename B>
auto saveMsg(B &builder, ZiFB::Offset data, uint64_t subID = 0,
    ZuCSpan deviceID = {}, uint64_t generation = 0) {
  auto device = deviceID ? builder.string(deviceID) : 0;
  ZtBuiltin<ZiFB::Offsets, ZiFB::Meta<Msg>::N> offsets;
  offsets.length(ZiFB::Meta<Msg>::N);
  auto table = builder.startTable(offsets.data());
  ZiFB::fieldOffset<ZfField(Msg, data)>(builder, table, data);
  ZiFB::field<ZfField(Msg, subID)>(builder, table, subID);
  ZiFB::fieldOffset<ZfField(Msg, deviceID)>(builder, table, device);
  ZiFB::field<ZfField(Msg, agentGen)>(builder, table, generation);
  return builder.endTable(table);
}

template <typename B>
auto saveMsg(B &builder, ZiFB::NestedState nested, unsigned type,
    ZiFB::Offset body, uint64_t subID = 0, ZuCSpan deviceID = {},
    uint64_t generation = 0) {
  ZtBuiltin<ZiFB::Offsets, ZiFB::Meta<Payload>::N> offsets;
  offsets.length(ZiFB::Meta<Payload>::N);
  auto table = builder.startTable(offsets.data());
  ZiFB::fieldUnion<ZfField(Payload, body)>(builder, table,
    ZiFB::UnionOffset<BodyValue>{body, uint8_t(type)});
  auto root = builder.endTable(table);
  auto payload = builder.endNested(nested, root);
  return saveMsg(builder, payload, subID, deviceID, generation);
}

template <bool ForceSeq = false, typename B>
auto saveTelemetry(B &builder, ZiFB::Offset id, uint64_t seqNo,
    unsigned type, ZiFB::Offset value) {
  auto table = builder.startTable();
  ZiFB::fieldOffset<ZfField(Telemetry, id)>(builder, table, id);
  using Seq = ZfField(Telemetry, seqNo);
  builder.template field<uint64_t>(table, ZiFB::Meta<Telemetry>::id<Seq>(),
    seqNo, ZiFB::defaultValue<Seq>(), ForceSeq);
  ZiFB::fieldUnion<ZfField(Telemetry, value)>(builder, table,
    ZiFB::UnionOffset<TelemetryValue>{value, uint8_t(type)});
  return builder.endTable(table);
}

#pragma pack(push, 4)
struct Hdr {
  ZuLittleEndian<uint32_t>	length;

  const uint8_t *data() const {
    return reinterpret_cast<const uint8_t *>(this) + sizeof(Hdr);
  }
};
#pragma pack(pop)

ZuAssert(sizeof(Hdr) == 4);

inline ZmRef<ZiIOBuf> frameBuf(ZmRef<ZiIOBuf> buf) {
  ZuAssert(sizeof(Hdr) <= Builder::Align);
  buf->skip = Builder::Align;
  return buf;
}

template <typename Builder, typename Owner>
inline auto saveHdr(Builder &fbb, Owner *owner) {
  auto length = fbb.size();
  auto buf = fbb.buf();
  if (ZuUnlikely(
	!buf || buf->skip < sizeof(Hdr)))
    return decltype(buf){};
  buf->owner = owner;
  auto ptr = buf->prepend(sizeof(Hdr));
  new (ptr) Hdr{uint32_t(length)};
  return buf;
}

template <typename Builder>
inline auto saveHdr(Builder &fbb) {
  return saveHdr(fbb, static_cast<void *>(nullptr));
}

template <typename Buf>
inline int loadHdr(const Buf *buf, uint32_t maxFrame) {
  if (ZuUnlikely(buf->length < sizeof(Hdr))) return INT_MAX;
  auto hdr = buf->template ptr<Hdr>();
  uint64_t body = uint32_t(hdr->length);
  uint64_t total = body + sizeof(Hdr);
  if (ZuUnlikely(
	total > maxFrame || total > unsigned(INT_MAX) ||
	maxFrame < sizeof(Hdr)))
    return -1;
  return int(total);
}

template <typename Buf, typename L>
inline int verifyHdr(ZmRef<Buf> buf, uint32_t maxFrame, L &&l) {
  int total = loadHdr(buf.ptr(), maxFrame);
  if (ZuUnlikely(total < 0)) return -1;
  if (ZuUnlikely(total == INT_MAX || unsigned(total) > buf->length))
    return INT_MAX;
  auto hdr = buf->template ptr<Hdr>();
  int n = ZuFwd<L>(l)(hdr, ZuMv(buf));
  return n < 0 ? n : total;
}

inline ZuBSpan msgData(const Hdr *hdr) {
  if (ZuUnlikely(!hdr)) return {};
  return {
    reinterpret_cast<const uint8_t *>(hdr),
    unsigned(sizeof(Hdr) + uint32_t(hdr->length))};
}

inline bool validTelemetry(TelemetryView telemetry) {
  if (!telemetry || !telemetry.get<ZfField(Telemetry, id)>()) return false;
  auto value = telemetry.get<ZfField(Telemetry, value)>();
  return value.data() && value.tag() >= TelemetryBody::HeapTelemetry &&
    value.tag() <= TelemetryBody::AlertTelemetry;
}

inline bool validRequest(ZiFB::View<Request> request) {
  if (!request) return false;
  auto group = request.get<ZfField(Request, group)>();
  return group >= Group::Heap && group <= Group::Alert;
}

inline bool validMsg(MsgView msg) {
  if (!msg) return false;
  auto body = msgBody(msg);
  if (!body.data()) return false;
  switch (body.tag()) {
    case Body::Request: return validRequest(body.get<Body::Request>());
    case Body::Ack: {
      auto value = body.get<Body::Ack>();
      auto status = value.get<ZfField(Ack, status)>();
      return value.get<ZfField(Ack, id)>() &&
	status >= AckStatus::OK && status <= AckStatus::Failed;
    }
    case Body::EOS: return !!body.get<Body::EOS>().get<ZfField(EOS, id)>();
    case Body::Error: return !!body.get<Body::Error>().get<ZfField(Error, id)>();
    case Body::Telemetry: return validTelemetry(body.get<Body::Telemetry>());
    default: return false;
  }
}

inline MsgView msg(ZuBSpan data) {
  if (!ZiFB::verify<Msg>(data)) return {};
  auto root = ZiFB::view<Msg>(data);
  if (!ZiFB::verify<Payload>(root.get<ZfField(Msg, data)>())) return {};
  return validMsg(root) ? root : MsgView{};
}
inline MsgView msg(const Hdr *hdr) {
  return hdr ? msg(ZuBSpan{hdr->data(), uint32_t(hdr->length)}) : MsgView{};
}
inline MsgView msg_(const Hdr *hdr) {
  return ZiFB::view<Msg>({hdr->data(), uint32_t(hdr->length)});
}

} // Ztc

#endif /* ZtcMsg_HH */
