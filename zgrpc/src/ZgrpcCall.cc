//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZgrpcCall.hh>

namespace Zgrpc {

bool Direction::message()
{
  if (m_ended || (!m_streamed && m_count)) return false;
  ++m_count;
  return true;
}

bool Direction::end(bool success)
{
  if (m_ended) return false;
  m_ended = true;
  return !success || m_streamed || m_count == 1;
}

CallCore::CallCore(uint64_t id, uint64_t generation, CallRoute rx, void *rxOwner,
  CallRoute tx, void *txOwner, unsigned maxQueue) :
  m_id{id}, m_generation{generation}, m_rxRoute{rx}, m_txRoute{tx},
  m_maxQueue{maxQueue}, m_rxOwner{rxOwner}, m_txOwner{txOwner} { }

void *CallCore::rxOwner_() const
{
  ZiAssert(m_rxRoute.invoked(), "Zgrpc", (), "call outside Rx", return nullptr);
  return m_rxOwner.load_();
}

void *CallCore::txOwner_() const
{
  ZiAssert(m_txRoute.invoked(), "Zgrpc", (), "call outside Tx", return nullptr);
  return m_txOwner.load_();
}

void CallCore::invalidateRx(bool cancelled)
{
  ZiAssert(m_rxRoute.invoked(), "Zgrpc", (), "call invalidation outside Rx", return);
  if (!m_rxOwner.load_()) return;
  if (cancelled) m_cancelled = 1;
  m_rxOwner = nullptr;
}

void CallCore::invalidateTx()
{
  ZiAssert(m_txRoute.invoked(), "Zgrpc", (), "call invalidation outside Tx", return);
  m_txOwner = nullptr;
}

bool CallCore::sendPost()
{
  if (++m_sendPosts <= m_maxQueue) return true;
  --m_sendPosts;
  return false;
}

} // Zgrpc
