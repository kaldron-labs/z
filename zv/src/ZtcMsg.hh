//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// telemetry service framing and message verification

#ifndef ZtcMsg_HH
#define ZtcMsg_HH

#ifndef ZvLib_HH
#include <zlib/ZvLib.hh>
#endif

#include <limits.h>
#include <new>
#include <stdint.h>

#include <zlib/ZuByteSwap.hh>
#include <zlib/ZuSpan.hh>

#include <zlib/ZmRef.hh>

#include <zlib/Zfb.hh>

#include <zlib/ztc_msg_fbs.h>

namespace Ztc {

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
  ZuAssert(sizeof(Hdr) <= Zfb::IOBuilder::Align);
  buf->skip = Zfb::IOBuilder::Align;
  return buf;
}

template <typename Builder, typename Owner>
inline auto saveHdr(Builder &fbb, Owner *owner) {
  auto length = fbb.GetSize();
  auto buf = fbb.buf();
  if (ZuUnlikely(
	!buf || length > UINT32_MAX || buf->skip < sizeof(Hdr)))
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

inline bool validTelemetry(const fbs::Telemetry *telemetry) {
  if (ZuUnlikely(!telemetry || !telemetry->id() ||
      !telemetry->id()->size() || !telemetry->value())) return false;
  switch (telemetry->value_type()) {
    case fbs::TelemetryBody::HeapTelemetry:
    case fbs::TelemetryBody::HashTelemetry:
    case fbs::TelemetryBody::ThreadTelemetry:
    case fbs::TelemetryBody::CxnTelemetry:
    case fbs::TelemetryBody::MxTelemetry:
    case fbs::TelemetryBody::QueueTelemetry:
    case fbs::TelemetryBody::HubTelemetry:
    case fbs::TelemetryBody::LinkTelemetry:
    case fbs::TelemetryBody::PoolTelemetry:
    case fbs::TelemetryBody::DBTelemetry:
    case fbs::TelemetryBody::DBHostTelemetry:
    case fbs::TelemetryBody::DBTableTelemetry:
    case fbs::TelemetryBody::AppTelemetry:
    case fbs::TelemetryBody::AlertTelemetry:
      return true;
    default:
      return false;
  }
}

inline bool validMsg(const fbs::Msg *msg) {
  if (ZuUnlikely(!msg || !msg->body())) return false;
  switch (msg->body_type()) {
    case fbs::Body::Request:
      return true;
    case fbs::Body::Ack: {
      auto body = msg->body_as_Ack();
      return body && body->id() && body->id()->size();
    }
    case fbs::Body::EOS: {
      auto body = msg->body_as_EOS();
      return body && body->id() && body->id()->size();
    }
    case fbs::Body::Error: {
      auto body = msg->body_as_Error();
      return body && body->id() && body->id()->size();
    }
    case fbs::Body::Telemetry:
      return validTelemetry(msg->body_as_Telemetry());
    default:
      return false;
  }
}

inline const fbs::Msg *msg(const Hdr *hdr) {
  if (ZuUnlikely(!hdr)) return nullptr;
  auto data = hdr->data();
  auto length = uint32_t(hdr->length);
  if (ZuUnlikely((!Zfb::Verifier{data, length}.VerifyBuffer<fbs::Msg>())))
    return nullptr;
  auto root = Zfb::GetRoot<fbs::Msg>(data);
  return validMsg(root) ? root : nullptr;
}

inline const fbs::Msg *msg_(const Hdr *hdr) {
  return Zfb::GetRoot<fbs::Msg>(hdr->data());
}

} // Ztc

#endif /* ZtcMsg_HH */
