//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Software Ethernet self-loop for the reserved _loop interface.

#ifndef ZpktMlxLoop_HH
#define ZpktMlxLoop_HH

#ifndef ZpktLib_HH
#include <zlib/ZpktLib.hh>
#endif

#include <zlib/ZmRing.hh>

#include "ZpktMlxPool.hh"

namespace Zpkt::Mlx {

class Loop {
public:
  int start(BufPool &, unsigned rxDepth, unsigned txDepth);
  void stop(BufPool &); // both owners quiesced

  void refill(BufPool &); // Rx owner
  bool rxReady() const { return m_rx.readStatus() > 0; }
  unsigned rxPosted() const { return m_rxPosted; }
  bool receive(RxFrame &);

  bool send(Slot *, const uint8_t *, unsigned); // Tx owner
  bool retire(Completion &);

private:
  template <typename T> using Ring = ZmRing<ZmRingT<T>>;

  Ring<uint8_t *>	m_posted;
  Ring<RxFrame>		m_rx;
  Ring<Completion>	m_tx;
  unsigned		m_rxDepth = 0, m_txDepth = 0, m_rxSize = 0;
  alignas(Zm::CacheLineSize) unsigned m_rxPosted = 0;
  alignas(Zm::CacheLineSize) unsigned m_txPending = 0;
};

} // namespace Zpkt::Mlx
#endif /* ZpktMlxLoop_HH */
