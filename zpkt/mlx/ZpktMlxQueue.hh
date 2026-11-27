//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Physical raw packet queues and work request ownership.

#ifndef ZpktMlxQueue_HH
#define ZpktMlxQueue_HH

#ifndef ZpktLib_HH
#include <zlib/ZpktLib.hh>
#endif

#include <zlib/ZtArray.hh>

#include "ZpktMlxPool.hh"

namespace Zpkt::Mlx {

class Queue {
public:
  int start(ibv_pd *, unsigned port, unsigned rxDepth, unsigned txDepth);
  int steer(unsigned port);
  int unsteer();
  int close(); // both service owners quiesced
  bool open() const { return m_channel; }
  bool steered() const { return m_flow; }

  int refill(BufPool &); // Rx owner
  unsigned rxPosted() const { return m_rx.pending(); }
  int receive(RxFrame *, unsigned, int &error);
  int arm();
  int ack();
  int fd() const { return m_channel->fd; }

  int send(Slot *, const uint8_t *, unsigned); // Tx owner
  int retire(Completion *, unsigned, int &error);
  unsigned txPending() const { return m_tx.pending(); }

private:
  struct Work {
    Completion	frame;
    unsigned	next;
  };

  class WorkSet {
  public:
    void init(unsigned);
    unsigned next() const { return m_head; }
    void add(Completion);
    Completion take(uint64_t);
    void reclaim(unsigned lease);
    unsigned pending() const { return m_pending; }

  private:
    ZtArray<Work, ZtArrayHeapID<"Zpkt.Mlx.Work">> m_work;
    unsigned	m_head = 0, m_pending = 0;
  };

  ibv_comp_channel	*m_channel = nullptr;
  ibv_cq		*m_rxCQ = nullptr, *m_txCQ = nullptr;
  ibv_qp		*m_qp = nullptr;
  ibv_qp_ex		*m_txQP = nullptr;
  ibv_flow		*m_flow = nullptr;
  alignas(Zm::CacheLineSize) WorkSet m_rx;
  alignas(Zm::CacheLineSize) WorkSet m_tx;
};

} // namespace Zpkt::Mlx
#endif /* ZpktMlxQueue_HH */
