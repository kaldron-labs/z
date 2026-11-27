//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <infiniband/mlx5dv.h>

#include <zlib/ZmAssert.hh>

#include "ZpktMlxQueue.hh"

namespace Zpkt::Mlx {

void Queue::WorkSet::init(unsigned depth)
{
  ZmAssert_(!m_pending);
  m_work.length(depth);
  for (unsigned i = 0; i < depth; ++i)
    m_work[i] = {{nullptr, 0}, i + 1 < depth ? i + 2 : 0};
  m_head = depth ? 1 : 0;
}

void Queue::WorkSet::add(Completion frame)
{
  ZmAssert_(m_head);
  auto &work = m_work[m_head - 1];
  m_head = work.next;
  work.frame = frame;
  ++m_pending;
}

Completion Queue::WorkSet::take(uint64_t id)
{
  if (!id || id > m_work.length()) return {};
  auto &work = m_work[id - 1];
  auto frame = work.frame;
  if (!frame.slot) return {};
  work.frame = {};
  work.next = m_head;
  m_head = unsigned(id);
  --m_pending;
  return frame;
}

void Queue::WorkSet::reclaim(unsigned lease)
{
  for (auto &work : m_work)
    if (work.frame.slot) work.frame.slot->release(lease, false);
  m_work.null();
  m_head = m_pending = 0;
}

int Queue::start(ibv_pd *pd, unsigned port, unsigned rxDepth, unsigned txDepth)
{
  ZmAssert_(!open());
  if (!port || port > UINT8_MAX || !rxDepth || rxDepth > INT_MAX ||
      !txDepth || txDepth > INT_MAX) return EINVAL;
  auto ctx = pd->context;
  ibv_device_attr limits{};
  if (int rc = ibv_query_device(ctx, &limits)) return rc;
  if (rxDepth > unsigned(limits.max_cqe) || rxDepth > unsigned(limits.max_qp_wr) ||
      txDepth > unsigned(limits.max_cqe) || txDepth > unsigned(limits.max_qp_wr))
    return EINVAL;

  m_channel = ibv_create_comp_channel(ctx);
  if (!m_channel) return errno;
  int flags = fcntl(m_channel->fd, F_GETFL);
  if (flags < 0 || fcntl(m_channel->fd, F_SETFL, flags | O_NONBLOCK) < 0) return errno;
  m_rxCQ = ibv_create_cq(ctx, int(rxDepth), nullptr, m_channel, 0);
  if (!m_rxCQ) return errno;
  m_txCQ = ibv_create_cq(ctx, int(txDepth), nullptr, nullptr, 0);
  if (!m_txCQ) return errno;

  ibv_qp_init_attr_ex attr{};
  attr.comp_mask = IBV_QP_INIT_ATTR_PD | IBV_QP_INIT_ATTR_SEND_OPS_FLAGS;
  attr.pd = pd;
  attr.qp_type = IBV_QPT_RAW_PACKET;
  attr.send_cq = m_txCQ;
  attr.recv_cq = m_rxCQ;
  attr.cap = {txDepth, rxDepth, 1, 1, 0};
  attr.send_ops_flags = IBV_QP_EX_WITH_SEND;
  mlx5dv_qp_init_attr dv{};
  m_qp = mlx5dv_create_qp(ctx, &attr, &dv);
  if (!m_qp) return errno;
  m_txQP = ibv_qp_to_qp_ex(m_qp);
  if (!m_txQP || attr.cap.max_send_wr < txDepth || attr.cap.max_recv_wr < rxDepth ||
      attr.cap.max_send_sge < 1 || attr.cap.max_recv_sge < 1 ||
      m_rxCQ->cqe < int(rxDepth) || m_txCQ->cqe < int(txDepth)) return ENOTSUP;

  ibv_qp_attr state{};
  state.port_num = uint8_t(port);
  state.qp_state = IBV_QPS_INIT;
  if (int rc = ibv_modify_qp(m_qp, &state, IBV_QP_STATE | IBV_QP_PORT)) return rc;
  state.qp_state = IBV_QPS_RTR;
  if (int rc = ibv_modify_qp(m_qp, &state, IBV_QP_STATE)) return rc;
  state.qp_state = IBV_QPS_RTS;
  if (int rc = ibv_modify_qp(m_qp, &state, IBV_QP_STATE)) return rc;

  m_rx.init(rxDepth);
  m_tx.init(txDepth);
  return 0;
}

int Queue::steer(unsigned port)
{
  struct {
    ibv_flow_attr	attr;
    ibv_flow_spec_eth	spec;
  } flow{};
  ZuAssert(offsetof(decltype(flow), spec) == sizeof(flow.attr));
  flow.attr.type = IBV_FLOW_ATTR_NORMAL;
  flow.attr.size = sizeof(flow);
  flow.attr.num_of_specs = 1;
  flow.attr.port = uint8_t(port);
  flow.spec.type = IBV_FLOW_SPEC_ETH;
  flow.spec.size = sizeof(flow.spec);
  m_flow = ibv_create_flow(m_qp, &flow.attr);
  return m_flow ? 0 : errno;
}

int Queue::unsteer()
{
  if (!m_flow) return 0;
  if (int rc = ibv_destroy_flow(m_flow)) return rc;
  m_flow = nullptr;
  return 0;
}

int Queue::close()
{
  if (int rc = unsteer()) return rc;
  if (m_qp) {
    if (int rc = ibv_destroy_qp(m_qp)) return rc;
    m_qp = nullptr;
    m_txQP = nullptr;
  }
  m_tx.reclaim(Slot::Submitted);
  m_rx.reclaim(Slot::Caller);
  if (m_rxCQ) {
    if (int rc = ack()) return rc;
    if (int rc = ibv_destroy_cq(m_rxCQ)) return rc;
    m_rxCQ = nullptr;
  }
  if (m_txCQ) {
    if (int rc = ibv_destroy_cq(m_txCQ)) return rc;
    m_txCQ = nullptr;
  }
  if (m_channel) {
    if (int rc = ibv_destroy_comp_channel(m_channel)) return rc;
    m_channel = nullptr;
  }
  return 0;
}

int Queue::refill(BufPool &pool)
{
  while (auto id = m_rx.next()) {
    auto data = pool.alloc();
    if (!data) break;
    auto slot = pool.find(data);
    auto size = pool.rxExtent();
    ibv_sge sge{uintptr_t(data), size, pool.lkey()};
    ibv_recv_wr wr{id, nullptr, &sge, 1}, *bad = nullptr;
    if (int rc = ibv_post_recv(m_qp, &wr, &bad)) {
      pool.discard(data);
      return rc;
    }
    m_rx.add({slot, size});
  }
  return 0;
}

int Queue::receive(RxFrame *frames, unsigned limit, int &error)
{
  error = 0;
  unsigned count = 0;
  while (count < limit) {
    ibv_wc wc{};
    int n = ibv_poll_cq(m_rxCQ, 1, &wc);
    if (!n) break;
    if (n < 0 || wc.status != IBV_WC_SUCCESS || wc.opcode != IBV_WC_RECV)
      { error = EIO; break; }
    auto frame = m_rx.take(wc.wr_id);
    if (!frame.slot) { error = EPROTO; break; }
    frames[count++] = {frame.slot->packet(), wc.byte_len <= frame.length ? wc.byte_len : 0};
  }
  return int(count);
}

int Queue::arm() { return ibv_req_notify_cq(m_rxCQ, 0); }

int Queue::ack()
{
  for (;;) {
    ibv_cq *cq;
    void *context;
    if (ibv_get_cq_event(m_channel, &cq, &context) < 0) {
      if (errno == EINTR) continue;
      return errno == EAGAIN ? 0 : errno;
    }
    ibv_ack_cq_events(cq, 1);
    if (cq != m_rxCQ) return EPROTO;
  }
}

int Queue::send(Slot *slot, const uint8_t *data, unsigned length)
{
  unsigned id = m_tx.next();
  if (!id || !slot->submit()) return EAGAIN;
  ibv_wr_start(m_txQP);
  m_txQP->wr_id = id;
  m_txQP->wr_flags = IBV_SEND_SIGNALED;
  ibv_wr_send(m_txQP);
  ibv_wr_set_sge(m_txQP, slot->pool->lkey(), uintptr_t(data), length);
  if (int rc = ibv_wr_complete(m_txQP)) {
    slot->release(Slot::Submitted, false);
    return rc;
  }
  m_tx.add({slot, length});
  return 0;
}

int Queue::retire(Completion *retired, unsigned limit, int &error)
{
  error = 0;
  unsigned count = 0;
  while (count < limit) {
    ibv_wc wc{};
    int n = ibv_poll_cq(m_txCQ, 1, &wc);
    if (!n) break;
    if (n < 0 || wc.status != IBV_WC_SUCCESS || wc.opcode != IBV_WC_SEND)
      { error = EIO; break; }
    auto frame = m_tx.take(wc.wr_id);
    if (!frame.slot) { error = EPROTO; break; }
    retired[count++] = frame;
  }
  return int(count);
}

} // namespace Zpkt::Mlx
