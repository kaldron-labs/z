//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <errno.h>
#include <limits.h>
#include <string.h>

#include <zlib/ZmAssert.hh>

#include "ZpktMlxLoop.hh"

namespace Zpkt::Mlx {

template <typename T>
static bool openRing(ZmRing<ZmRingT<T>> &ring, unsigned depth)
{
  if (!depth || depth > unsigned(INT_MAX)/sizeof(T)) return false;
  ring.init(ZmRingParams{}.size(unsigned(depth * sizeof(T))));
  return ring.open(ring.Read | ring.Write) == Zu::OK;
}

int Loop::start(BufPool &pool, unsigned rxDepth, unsigned txDepth)
{
  if (!rxDepth || rxDepth > pool.count || !txDepth) return EINVAL;
  if (!openRing(m_posted, rxDepth) || !openRing(m_rx, rxDepth) ||
      !openRing(m_tx, txDepth)) {
    stop(pool);
    return ENOMEM;
  }
  m_rxDepth = rxDepth;
  m_txDepth = txDepth;
  m_rxSize = pool.size;
  refill(pool);
  return 0;
}

void Loop::stop(BufPool &pool)
{
  ZmAssert_(!m_txPending);
  if (!m_rx.closed()) {
    RxFrame frame;
    while (receive(frame)) pool.discard(frame.data);
  }
  if (!m_posted.closed())
    while (auto posted = m_posted.tryShift()) {
      pool.discard(*posted);
      m_posted.shift2();
      --m_rxPosted;
    }
  ZmAssert_(!m_rxPosted);
  m_tx.close();
  m_rx.close();
  m_posted.close();
}

void Loop::refill(BufPool &pool)
{
  while (m_rxPosted < m_rxDepth) {
    auto data = pool.alloc();
    if (!data) break;
    auto entry = static_cast<uint8_t **>(m_posted.tryPush());
    ZmAssert_(entry);
    *entry = data;
    ++m_rxPosted;
    m_posted.push2();
  }
}

bool Loop::receive(RxFrame &frame)
{
  auto entry = m_rx.tryShift();
  if (!entry) return false;
  frame = *entry;
  m_rx.shift2();
  --m_rxPosted;
  return true;
}

bool Loop::send(Slot *slot, const uint8_t *data, unsigned length)
{
  if (m_txPending == m_txDepth || length > m_rxSize) return false;
  auto posted = m_posted.tryShift();
  if (!posted || !slot->submit()) return false;

  // Each posted slot reserves an Rx completion credit. The exact configured
  // Tx depth is tracked separately from the ring's page-rounded storage.
  auto rx = static_cast<RxFrame *>(m_rx.tryPush());
  auto tx = static_cast<Completion *>(m_tx.tryPush());
  ZmAssert_(rx && tx);
  memcpy(*posted, data, length);
  *rx = {*posted, length};
  *tx = {slot, length};
  m_posted.shift2();
  m_tx.push2();
  ++m_txPending;
  m_rx.push2();
  return true;
}

bool Loop::retire(Completion &completion)
{
  auto entry = m_tx.tryShift();
  if (!entry) return false;
  completion = *entry;
  m_tx.shift2();
  --m_txPending;
  return true;
}

} // namespace Zpkt::Mlx
