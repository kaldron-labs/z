//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZmAssert.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZxdpRetire.hh>

namespace Zxdp::Xdp {

KernelRetirement::~KernelRetirement() { ZmAssert_(!waiting()); }

ZuUnion<int, ZeException> KernelRetirement::begin(Xsk &socket)
{
  if (m_socket || !socket.bound())
    return ZeEXCEPT(Error, "Zxdp",
      "kernel retirement requires a live bound socket and unused observer");
  // Same arena, no parallel payload allocation. The observer's small private
  // rings remain empty for its entire lifetime, even after a successful bind.
  auto prepared = m_probe.prepare(socket.arena(), 2, 2);
  if (prepared.is<ZeException>()) return ZuMv(prepared).p<ZeException>();
  if (m_probe.namespaceID() != socket.namespaceID()) {
    m_probe.close();
    return ZeEXCEPT(Error, "Zxdp",
      "kernel retirement observer belongs to a different network namespace");
  }
  m_socket = &socket;
  m_ifindex = socket.ifindex();
  m_queue = socket.queue();
  socket.close();
  return retry();
}

ZuUnion<int, ZeException> KernelRetirement::retry()
{
  if (!m_socket) return ZeEXCEPT(Error, "Zxdp", "kernel retirement has not begun");
  if (done()) return int(Done);
  // Linux assigns pools under RTNL and refuses an occupied queue. Its deferred
  // pool release disables driver zero-copy before clearing that queue identity.
  // A successful independent bind therefore observes the old pool's retirement,
  // rather than assuming fd closure or TX cursor progress is sufficient.
  auto bound = m_probe.bind_(m_ifindex, m_queue, false);
  if (bound.is<ZeException>()) {
    if (!m_probe.retryBind()) return ZuMv(bound).p<ZeException>();
    return int(Pending);
  }
  m_probe.close(); // no FILL/TX was ever published by this observer
  m_ifindex = 0; // the observer no longer needs a target after successful bind
  return int(Done);
}

void KernelRetirement::reset()
{
  if (!m_socket) return;
  ZmAssert_(done());
  m_socket = nullptr;
  m_ifindex = m_queue = 0;
}

} // namespace Zxdp::Xdp
