//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Private kernel queue-retirement observation; never publishes packet frames.

#ifndef ZxdpRetire_HH
#define ZxdpRetire_HH

#ifndef ZxdpLib_HH
#include <zlib/ZxdpLib.hh>
#endif

#include <zlib/ZuUnion.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZxdpUMem.hh>

namespace Zxdp::Xdp {

class ZxdpAPI KernelRetirement {
friend class Instance;

public:
  enum { Pending = 0, Done = 1 };
  KernelRetirement() = default;
  ~KernelRetirement();
  KernelRetirement(const KernelRetirement &) = delete;
  KernelRetirement &operator =(const KernelRetirement &) = delete;

  // Tx only, after Rx work/publication is quiescent. Prepare the empty observer
  // before touching the original binding. Caller retains both objects and all
  // packet holds through Done; an error never authorizes their release.
  ZuUnion<int, ZeException> begin(Xsk &);
  // A caller continuation/timer retries Pending; this never spins or waits.
  ZuUnion<int, ZeException> retry();
  void reset(); // Tx only, after both queue owners released their retired holds
  bool waiting() const { return m_socket && m_ifindex; }
  bool done() const { return m_socket && !m_ifindex; }

private:
  Xsk		*m_socket = nullptr;
  unsigned	m_ifindex = 0;
  unsigned	m_queue = 0;
  Xsk		m_probe;
};

} // namespace Zxdp::Xdp

#endif /* ZxdpRetire_HH */
