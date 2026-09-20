//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZDashProtocol_HH
#define ZDashProtocol_HH

#include <zlib/ZtcFB.hh>
#include <zlib/ZtcMsg.hh>

namespace ZDash {

enum { FrameMax = 1U << 20, QueuedInputMax = 1U << 22 };
using Frame = ZmRef<ZiIOBuf>;
using FrameBuf = ZiIOBufAlloc<1024, FrameMax, "ZDash.Frame">;

struct Subscription {
  ZtString<>	deviceID;
  ZuID		publisherID;
  Ztc::RequestFilter filter{"*"};
  uint64_t	id = 1;
  unsigned	interval = 1000;
  Ztc::fbs::Group group = Ztc::fbs::Group::App;
};

inline Frame requestFrame(const Subscription &sub, bool subscribe)
{
  Zfb::IOBuilder builder{Frame{new FrameBuf}};
  auto device = sub.deviceID ?
    builder.CreateString(sub.deviceID.data(), sub.deviceID.length()) :
    Zfb::Offset<flatbuffers::String>{};
  auto request = ZfbStruct::save(builder, Ztc::Request{
    .filter = sub.filter, .id = sub.publisherID, .seqNo = 0,
    .interval = subscribe ? sub.interval : 0,
    .group = uint8_t(sub.group), .subscribe = subscribe});
  builder.Finish(Ztc::saveMsg(builder, Ztc::fbs::Body::Request,
    request.Union(), sub.id, device));
  return builder.buf();
}

inline bool accepts(const Subscription &sub, const Ztc::fbs::Msg *msg)
{
  if (!Ztc::validMsg(msg) || msg->subId() != sub.id) return false;
  auto device = Zfb::Load::str(msg->deviceId());
  if (sub.deviceID && (device != sub.deviceID || !msg->agentGen()))
    return false;
  switch (msg->body_type()) {
    case Ztc::fbs::Body::Ack:
    case Ztc::fbs::Body::Error:
      return true;
    case Ztc::fbs::Body::EOS:
      // Snapshot completion may be per source or for the entire inventory.
      return (device && msg->agentGen()) ||
	(!sub.deviceID && !device && !msg->agentGen() &&
	  Zfb::Load::str(msg->body_as_EOS()->id()) == "inventory");
    case Ztc::fbs::Body::Telemetry: {
      if (!device || !msg->agentGen()) return false;
      auto tel = msg->body_as_Telemetry();
      if (sub.publisherID && Zfb::Load::str(tel->id()) != sub.publisherID)
	return false;
      using namespace Ztc::fbs;
      switch (tel->value_type()) {
	case TelemetryBody::AppTelemetry: return sub.group == Group::App;
	case TelemetryBody::HeapTelemetry: return sub.group == Group::Heap;
	case TelemetryBody::HashTelemetry: return sub.group == Group::Hash;
	case TelemetryBody::ThreadTelemetry: return sub.group == Group::Thread;
	case TelemetryBody::MxTelemetry:
	case TelemetryBody::CxnTelemetry: return sub.group == Group::Mx;
	case TelemetryBody::QueueTelemetry: return sub.group == Group::Queue;
	case TelemetryBody::HubTelemetry:
	case TelemetryBody::LinkTelemetry:
	case TelemetryBody::PoolTelemetry: return sub.group == Group::Hub;
	case TelemetryBody::DBTelemetry:
	case TelemetryBody::DBHostTelemetry:
	case TelemetryBody::DBTableTelemetry: return sub.group == Group::DB;
	case TelemetryBody::AlertTelemetry: return sub.group == Group::Alert;
	case TelemetryBody::Shutdown: return true;
	default: return false;
      }
    }
    default: return false;
  }
}

} // ZDash

#endif /* ZDashProtocol_HH */
