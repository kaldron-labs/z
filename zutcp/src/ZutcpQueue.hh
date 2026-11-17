//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Telemetry on the actual transport queue owners. Values are advisory cold reads.

#ifndef ZutcpQueue_HH
#define ZutcpQueue_HH

#ifndef ZutcpLib_HH
#include <zlib/ZutcpLib.hh>
#endif

#include <zlib/ZuID.hh>
#include <zlib/ZuString.hh>

#include <zlib/ZtcQueue.hh>

namespace Zutcp {

template <ZuString ID, int Type> class QueueTelemetry : public Ztc::Queue {
public:
  void queueOwner(const ZuID &id) { m_owner = &id; }
  void queueName(const ZuID &id) { m_id = &id; }
  TelKey telKey() const override {
    return {m_owner ? ZuCSpan{*m_owner} : ZuCSpan{},
      m_id ? ZuCSpan{*m_id} : ID.cspan(), Type};
  }
protected:
  void queueTelemetry(Ztc::QueueTelemetry &data) const {
    data = {};
    if (m_owner) data.ownerID = *m_owner;
    data.id = m_id ? ZuCSpan{*m_id} : ID.cspan();
    data.type = Type;
  }
private:
  const ZuID *m_id = nullptr;
  const ZuID *m_owner = nullptr; // containing owner outlives its embedded queues
};

// Embedded owners only: node allocation continues to use the original queue's
// named heaps. Growable containers have no fixed capacity/full condition.
template <typename Base, ZuString ID, int Type, bool Stats = false, bool Bytes = true>
class Queue : public Base, public QueueTelemetry<ID, Type> {
public:
  using Base::Base;
  void telemetry(Ztc::QueueTelemetry &data) const override {
    this->queueTelemetry(data);
    data.count = Base::count_();
    if constexpr (Stats) {
      Base::stats(data.inCount, data.inBytes, data.outCount, data.outBytes);
      if constexpr (!Bytes) data.inBytes = data.outBytes = 0;
    }
  }
};

} // namespace Zutcp

#endif /* ZutcpQueue_HH */
