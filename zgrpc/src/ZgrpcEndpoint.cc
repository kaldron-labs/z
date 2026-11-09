//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZgrpcEndpoint.hh>

namespace Zgrpc {

// gRPC PROTOCOL-HTTP2 and G2-http3-protocol native error mappings.
Code::T httpCode(const Zhttp::Result &result)
{
  switch (result.code) {
    case Zhttp::ResultCode::Cancelled: return Code::CANCELLED;
    case Zhttp::ResultCode::TimedOut: return Code::DEADLINE_EXCEEDED;
    default: break;
  }
  switch (result.errorDomain) {
    case Zhttp::ErrorDomain::H2Reset:
      switch (result.errorCode) {
	case 7: return Code::UNAVAILABLE; // REFUSED_STREAM
	case 8: return Code::CANCELLED; // CANCEL
	case 11: return Code::RESOURCE_EXHAUSTED; // ENHANCE_YOUR_CALM
	case 12: return Code::PERMISSION_DENIED; // INADEQUATE_SECURITY
	default: return Code::INTERNAL;
      }
    case Zhttp::ErrorDomain::H3Reset:
    case Zhttp::ErrorDomain::H3Stop:
    case Zhttp::ErrorDomain::H3Error:
      switch (result.errorCode) {
	case 0x107: return Code::RESOURCE_EXHAUSTED; // H3_EXCESSIVE_LOAD
	case 0x10b: return Code::UNAVAILABLE; // H3_REQUEST_REJECTED
	case 0x10c: return Code::CANCELLED; // H3_REQUEST_CANCELLED
	default: return Code::INTERNAL;
      }
    default: return Code::UNAVAILABLE;
  }
}

int64_t deadlineNow()
{
  return int64_t(Zm::now().nanosecs());
}

int64_t expiry(int64_t duration)
{
  if (duration < 0) return -1;
  auto now = deadlineNow();
  return duration > INT64_MAX - now ? INT64_MAX : now + duration;
}
int64_t remaining(int64_t expires)
{
  return expires < 0 ? -1 : expires - deadlineNow();
}

bool EndpointCalls::init(const Zhttp::HubConfig &hub, Limits limits, StartFn start, StopFn stop)
{
  if (m_rx.scheduler || !hub.mx() || !valid(limits)) return false;
  auto mx = hub.mx();
  m_rx = {mx, hub.rxThread() ? mx->sid(hub.rxThread()) : mx->rxThread()};
  m_tx = {mx, hub.txThread() ? mx->sid(hub.txThread()) : mx->txThread()};
  m_limits = limits;
  m_start = ZuMv(start);
  m_stop = ZuMv(stop);
  m_stopping = m_httpStopped = m_httpStarted = m_stopSent = false;
  m_stopOK = true;
  return true;
}

bool EndpointCalls::reserve()
{
  auto state = m_gate.load_();
  for (;;) {
    if (!(state & Up) || (state & CountMask) >= m_limits.maxPending) return false;
    auto prior = m_gate.cmpXch(state + 1, state);
    if (prior == state) return true;
    state = prior;
  }
}

void EndpointCalls::insert(ZmRef<WorkEntry> work)
{
  ZiAssert(rxInvoked(), "Zgrpc", (), "call registration outside Rx", return);
  ZiAssert(!work->registered, "Zgrpc", (), "duplicate call registration", return);
  work->registered = true;
  m_work.pushNode(ZuMv(work));
}
void EndpointCalls::release(WorkEntry *work)
{
  ZiAssert(rxInvoked(), "Zgrpc", (), "call removal outside Rx", return);
  if (!work->registered) return;
  work->registered = false;
  auto retained = m_work.delNode(work);
  release();
}
void EndpointCalls::release()
{
  ZiAssert(rxInvoked(), "Zgrpc", (), "ingress release outside Rx", return);
  ZiAssert(pending(), "Zgrpc", (), "ingress release without reservation", return);
  --m_gate;
  stop_3();
}

void EndpointCalls::start(DoneFn done)
{
  if (!m_rx.scheduler || m_gate.cmpXch(Starting, 0)) { done(false); return; }
  m_rx.run([this, done = ZuMv(done)]() mutable { start_0(ZuMv(done)); });
}
void EndpointCalls::start_0(DoneFn done)
{
  m_cancelNext = nullptr;
  m_httpStopped = m_stopSent = false;
  m_stopOK = true;
  if (m_gate.load_() & Stopping) { start_1(ZuMv(done), false); return; }
  m_start(DoneFn{[this, done = ZuMv(done)](bool ok) mutable {
    m_rx.run([this, done = ZuMv(done), ok]() mutable { start_1(ZuMv(done), ok); });
  }});
}
void EndpointCalls::start_1(DoneFn done, bool ok)
{
  m_httpStarted = ok;
  bool stopping = m_stopping;
  auto state = m_gate.load_();
  bool admitted;
  for (;;) {
    admitted = ok && !(state & Stopping);
    auto next = (state & ~Starting) | (admitted ? Up : 0);
    auto prior = m_gate.cmpXch(next, state);
    if (prior == state) break;
    state = prior;
  }
  try { done(admitted); } catch (...) { }
  // Without a pending stop a failed-start callback may destroy its owner.
  if (stopping) stop_2();
}
void EndpointCalls::stop(DoneFn done)
{
  auto state = m_gate.load_();
  for (;;) {
    if (!state) { done(true); return; }
    if ((state & PostMask) == PostMask) { done(false); return; }
    auto prior = m_gate.cmpXch(((state & ~Up) | Stopping) + StopPost, state);
    if (prior == state) break;
    state = prior;
  }
  if (rxInvoked()) stop_0(ZuMv(done));
  else m_rx.run([this, done = ZuMv(done)]() mutable { stop_0(ZuMv(done)); });
}
void EndpointCalls::stop_0(DoneFn done)
{
  m_waiters.push(ZuMv(done));
  m_gate -= StopPost;
  if (m_stopping) { stop_3(); return; }
  m_stopping = true;
  m_httpStopped = false;
  m_stopSent = false;
  m_stopOK = true;
  m_cancelNext = m_work.headPtr();
  stop_1();
}
void EndpointCalls::stop_1()
{
  ZtArray<ZmRef<WorkEntry>, ZtArrayHeapID<"Zgrpc.Endpoint.Cancel">> batch;
  // A retained cursor bounds visits as well as cancellations. If the next
  // entry retired between turns its links were cleared; restart from the live
  // head. Admission is closed, so each such restart consumes a retirement.
  if (m_cancelNext && !m_cancelNext->registered) m_cancelNext = m_work.headPtr();
  unsigned visited = 0;
  while (m_cancelNext && visited++ < m_limits.workBatch) {
    auto work = ZuMv(m_cancelNext);
    m_cancelNext = work->Ext::next;
    if (!work->terminal) batch.push(ZuMv(work));
  }
  for (auto &work : batch) work->cancelFn(work.ptr(), Code::CANCELLED);
  if (m_cancelNext) {
    m_rx.run([this]() { stop_1(); });
    return;
  }
  stop_2();
}
void EndpointCalls::stop_2()
{
  if (m_stopSent || (m_gate.load_() & Starting)) return;
  m_stopSent = true;
  if (!m_httpStarted) { m_httpStopped = true; stop_3(); return; }
  m_stop(DoneFn{[this](bool ok) {
    m_rx.run([this, ok]() {
      m_httpStarted = !ok; m_httpStopped = true; m_stopOK = ok; stop_3();
    });
  }});
}
void EndpointCalls::stop_3()
{
  if (!m_stopping || !m_httpStopped || m_gate.load_() != Stopping) return;
  ZiAssert(!m_work.count_(), "Zgrpc", (), "registered calls after drain", return);
  auto waiters = ZuMv(m_waiters);
  m_waiters.null();
  bool ok = m_stopOK;
  m_stopping = false;
  if (m_gate.cmpXch(ok ? 0 : Up, Stopping) != Stopping) {
    m_waiters = ZuMv(waiters);
    m_stopping = true;
    return;
  }
  // Publishing idle permits another stop callback to destroy the owner.
  // Everything below uses the detached callbacks and the local result only.
  for (auto &done : waiters) {
    try { done(ok); } catch (...) { }
    done = {};
  }
}
void EndpointCalls::final()
{
  ZmAssert_(m_stopOK && !m_gate.load_() && !m_work.count_());
  m_stop = {};
  m_start = {};
  m_rx = {}; m_tx = {};
}

} // Zgrpc
