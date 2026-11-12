//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZmAssert.hh>
#include <zlib/ZxdpRetire.hh>

namespace Zxdp {

KernelRetirement::~KernelRetirement() { ZmAssert_(!waiting()); }

int KernelRetirement::begin(Xsk &socket, ZtString<> &error)
{
  error = {};
  if (m_socket || !socket.bound()) {
    error = "kernel retirement requires a live bound socket and unused observer";
    return Error;
  }
  // Same arena, no parallel payload allocation. The observer's small private
  // rings remain empty for its entire lifetime, even after a successful bind.
  if (!m_probe.prepare(socket.arena(), 2, 2, error)) return Error;
  if (m_probe.namespaceID() != socket.namespaceID()) {
    m_probe.close();
    error = "kernel retirement observer belongs to a different network namespace";
    return Error;
  }
  m_socket = &socket;
  m_ifindex = socket.ifindex();
  m_queue = socket.queue();
  socket.close();
  return retry(error);
}

int KernelRetirement::retry(ZtString<> &error)
{
  error = {};
  if (!m_socket) { error = "kernel retirement has not begun"; return Error; }
  if (done()) return Done;
  // Linux assigns pools under RTNL and refuses an occupied queue. Its deferred
  // pool release disables driver zero-copy before clearing that queue identity.
  // A successful independent bind therefore observes the old pool's retirement,
  // rather than assuming fd closure or TX cursor progress is sufficient.
  if (!m_probe.bind_(m_ifindex, m_queue, error, false)) {
    if (!m_probe.retryBind()) return Error;
    error = {};
    return Pending;
  }
  m_probe.close(); // no FILL/TX was ever published by this observer
  m_ifindex = 0; // the observer no longer needs a target after successful bind
  return Done;
}

void KernelRetirement::reset()
{
  if (!m_socket) return;
  ZmAssert_(done());
  m_socket = nullptr;
  m_ifindex = m_queue = 0;
}

} // namespace Zxdp
