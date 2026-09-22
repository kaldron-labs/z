//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// test-only telemetry public-wire ring client primitives

#ifndef ZtcTestClient_HH
#define ZtcTestClient_HH

#include <stdlib.h>
#include <string.h>

#include <zlib/ZuDerive.hh>

#include <zlib/ZtArray.hh>

#include <zlib/Zfb.hh>

#include <zlib/ZiProgram.hh>
#include <zlib/ZtcApp.hh>
#include <zlib/ZtcFB.hh>
#include <zlib/ZtcMsg.hh>
#include <zlib/ZtcRing.hh>

#include "ZiTestResidue.hh"

namespace ZtcTestClient {

using Ring = Ztc::Ring;

using Frame = ZiIOBufAlloc<1024,
  Ztc::AppCf::DefltMaxFrame, "Ztc.TestClient.Frame">;

inline ZmRef<ZiIOBuf> request(
    uint64_t seqNo, Ztc::fbs::Group group, ZuCSpan filter,
    uint32_t interval, bool subscribe,
    uint32_t alertDate = 0, uint64_t alertSeqNo = 0,
    ZuCSpan id = {})
{
  Zfb::IOBuilder fbb{Ztc::frameBuf(ZmRef<ZiIOBuf>{new Frame})};
  Ztc::Request data;
  data.filter = filter;
  data.id = id;
  data.seqNo = seqNo;
  data.alertSeqNo = alertSeqNo;
  data.interval = interval;
  data.alertDate = alertDate;
  data.group = uint8_t(group);
  data.subscribe = subscribe;
  auto request_ = ZfbStruct::save(fbb, data);
  fbb.Finish(Ztc::saveMsg(
    fbb, Ztc::fbs::Body::Request, request_.Union()));
  return Ztc::saveHdr(fbb);
}

class Client {
public:
  enum { TelSize = 1U<<22, ReadAttempts = 5 };

  bool connect(ZuCSpan id = ZiProgram::name())
  {
    if (!m_telReady) {
      const char *name = ::getenv("ZTC_RING");
      m_telName = name ? name : "ztc";
      ZiTestResidue::addShm(m_telName);
      m_telRing.init(ZiRingParams{m_telName, TelSize}.timeout(1));
      if (m_telRing.open(Ring::Write) != Zu::OK ||
	  m_telRing.reset() != Zu::OK)
	return false;
      m_telRing.close();
      if (m_telRing.open(Ring::Read) != Zu::OK ||
	  m_telRing.attach() != Zu::OK)
	return false;
      m_telReady = true;
    }
    m_reqRing.close();
    m_reqRing.init(ZiRingParams{id, 0}.timeout(1));
    if (m_reqRing.open(Ring::Write) != Zu::OK) return false;
    bool registered = false;
    for (const auto &name : m_reqNames)
      if (name == id) registered = true;
    if (!registered) {
      ZiTestResidue::addShm(Zi::Name{id});
      m_reqNames.push(Zi::Name{id});
    }
    m_resetPending = true;
    return true;
  }

  bool send(ZuBSpan data)
  {
    if (m_reqRing.closed()) return false;
    if (m_resetPending) {
      auto reset = request(
	ZuCmp<uint64_t>::null(), Ztc::fbs::Group::App, {}, 0, false);
      if (!reset || !sendRaw(reset->cspan())) return false;
      m_resetPending = false;
    }
    return sendRaw(data);
  }

  ZmRef<ZiIOBuf> read(bool skipStartup = true)
  {
    for (unsigned attempt = 0; attempt < ReadAttempts; ++attempt) {
      const void *ptr = m_telRing.shift();
      if (!ptr) {
	if (m_telRing.readStatus() < 0) return {};
	continue;
      }
      unsigned size = Ztc::ringSize(ptr);
      ZmRef<ZiIOBuf> frame = new Frame;
      if (size <= Ztc::AppCf::DefltMaxFrame && frame->alloc(size)) {
	memcpy(frame->data(), ptr, size);
	frame->length = size;
      } else
	frame = {};
      m_telRing.shift2(size);
      if (!frame) return {};
      auto msg = Ztc::msg(frame->ptr<Ztc::Hdr>());
      auto tel = msg && msg->body_type() == Ztc::fbs::Body::Telemetry ?
	msg->body_as_Telemetry() : nullptr;
      if (skipStartup && tel && !tel->seqNo() &&
	  (tel->value_type() == Ztc::fbs::TelemetryBody::AppTelemetry ||
	   tel->value_type() == Ztc::fbs::TelemetryBody::Shutdown))
	continue;
      return frame;
    }
    return {};
  }

  void close()
  {
    m_reqRing.close();
  }

  void final()
  {
    close();
    if (!m_telRing.closed()) {
      if (m_telRing.rdrID() >= 0) m_telRing.detach();
      m_telRing.close();
    }
    m_telReady = false;
  }

private:
  using Names = ZtArray<Zi::Name,
    ZtArrayHeapID<"Ztc.TestClient.Names">>;

  bool sendRaw(ZuBSpan data)
  {
    void *ptr = m_reqRing.tryPush(data.length());
    if (!ptr) return false;
    memcpy(ptr, data.data(), data.length());
    m_reqRing.push2(ptr, data.length());
    return true;
  }

  Zi::Name	m_telName;
  Ring		m_telRing;
  Ring		m_reqRing;
  Names		m_reqNames;
  bool		m_telReady = false;
  bool		m_resetPending = true;
};

inline Client &client()
{
  static Client client_;
  return client_;
}

inline bool writeAll(int, ZuBSpan data)
{
  return client().send(data);
}

inline ZmRef<ZiIOBuf> readFrame(int, bool skipStartup = true)
{
  return client().read(skipStartup);
}

inline int connect(const Ztc::App &, bool = false)
{
  return client().connect() ? 1 : -1;
}

inline void close(int)
{
  client().close();
}

inline void final()
{
  client().final();
}

} // ZtcTestClient

#endif /* ZtcTestClient_HH */
