//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZgrpcEndpoint_HH
#define ZgrpcEndpoint_HH

#ifndef ZgrpcLib_HH
#include <zlib/ZgrpcLib.hh>
#endif

#include <zlib/ZmBlock.hh>
#include <zlib/ZmPolymorph.hh>
#include <zlib/ZgrpcMulti.hh>

namespace Zgrpc {

ZgrpcAPI Code::T httpCode(const Zhttp::Result &);

class ClientConfig : public Zhttp::Config {
public:
  const Limits &limits() const { return m_limits; }
  ClientConfig &limits(Limits value) { m_limits = value; return *this; }
  Zhttp::Config &http() { return *this; }
  const Zhttp::Config &http() const { return *this; }
  template <typename L> ClientConfig &http(L &&l) {
    l(static_cast<Zhttp::Config &>(*this)); return *this;
  }
private:
  Limits m_limits;
};
class ServerConfig : public Zhttp::ServerConfig {
public:
  const Limits &limits() const { return m_limits; }
  ServerConfig &limits(Limits value) { m_limits = value; return *this; }
  Zhttp::ServerConfig &http() { return *this; }
  const Zhttp::ServerConfig &http() const { return *this; }
  template <typename L> ServerConfig &http(L &&l) {
    l(static_cast<Zhttp::ServerConfig &>(*this)); return *this;
  }
private:
  Limits m_limits;
};

struct CallOptions {
  Metadata metadata{};
  int64_t timeout = -1; // nanoseconds; negative means no deadline
};

struct Failure {
  Code::T code = Code::UNAVAILABLE;
  Zhttp::Result http;
  MetaError::T metadata = MetaError::OK;
};
template <typename T> struct Result {
  Status status;
  Decoded<T> message;
  Metadata metadata{};
  Metadata trailers{};
  Zhttp::Result http;
};
struct Context {
  Zhttp::Session peer;
  const ZiIP *ip = nullptr;
  uint16_t port = 0;
  ZuCSpan authority;
  const Metadata *metadata = nullptr;
  int64_t deadline = -1; // absolute local clock nanoseconds; negative means none
};

ZgrpcAPI int64_t deadlineNow();
ZgrpcAPI int64_t expiry(int64_t duration);
ZgrpcAPI int64_t remaining(int64_t expires);

// Intrusive registry linkage and terminal selection are exclusively Rx-owned.
// The concrete work supplies cancellation dispatch and owns its directional
// state. Virtual destruction is the framework's ordinary erased callback base.
struct WorkData : ZmPolymorph {
  void (*cancelFn)(WorkData *, Code::T) = nullptr;
  bool terminal = false;
  bool registered = false;
};
using WorkList = ZmList<WorkData,
  ZmListNode<ZmPolymorph, ZmListHeapID<"">>>;
using WorkEntry = WorkList::Node;

// Shared bounded admission and event-driven endpoint drain. Retained public
// tokens never hold this object or the endpoint; a reserved ingress slot keeps
// its posted admission work in the shutdown accounting until release on Rx.
class ZgrpcAPI EndpointCalls {
public:
  using DoneFn = ZmFn<void(bool), ZmFnHeapID<"Zgrpc.Endpoint.Done">>;
  using StartFn = ZmFn<void(DoneFn), ZmFnHeapID<"Zgrpc.Endpoint.Start">>;
  using StopFn = ZmFn<void(DoneFn), ZmFnHeapID<"Zgrpc.Endpoint.Stop">>;

  bool init(const Zhttp::HubConfig &, Limits, StartFn, StopFn);
  bool start() { return ZmBlock<bool>{}([this](auto done) { start(DoneFn{ZuMv(done)}); }); }
  void start(DoneFn);
  bool up() const { return m_gate.load_() & Up; }
  bool reserve();
  void insert(ZmRef<WorkEntry>);
  void release(WorkEntry *);
  void release();
  void stop(DoneFn);
  void final();
  const Limits &limits() const { return m_limits; }
  const CallRoute &rx() const { return m_rx; }
  const CallRoute &tx() const { return m_tx; }
  unsigned pending() const { return unsigned(m_gate.load_() & CountMask); }
  bool rxInvoked() const { return m_rx.invoked(); }

private:
  void start_0(DoneFn);
  void start_1(DoneFn, bool);
  void stop_0(DoneFn);
  void stop_1();
  void stop_2();
  void stop_3();

  CallRoute m_rx, m_tx;
  Limits m_limits;
  static constexpr uint64_t Up = uint64_t(1) << 63;
  static constexpr uint64_t Starting = uint64_t(1) << 62;
  static constexpr uint64_t Stopping = uint64_t(1) << 61;
  static constexpr uint64_t CountMask = (uint64_t(1) << 32) - 1;
  static constexpr uint64_t StopPost = CountMask + 1;
  static constexpr uint64_t PostMask = (Stopping - 1) & ~CountMask;
  // Low 32 bits count calls; bits 32..60 count posted stop entries. Startup
  // retains Starting through its Rx acknowledgement, even when stop wins.
  ZmAtomic<uint64_t> m_gate = 0;
  WorkList m_work;
  ZmRef<WorkEntry> m_cancelNext;
  StartFn m_start;
  StopFn m_stop;
  ZtArray<DoneFn, ZtArrayHeapID<"Zgrpc.Endpoint.Waiters">> m_waiters;
  bool m_stopping = false;
  bool m_httpStopped = false;
  bool m_stopOK = true;
  bool m_httpStarted = false;
  bool m_stopSent = false;
};

} // Zgrpc

#endif /* ZgrpcEndpoint_HH */
