//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <errno.h>
#include <fcntl.h>
#include <linux/netlink.h>
#include <rdma/rdma_netlink.h>
#include <string.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <infiniband/mlx5dv.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmTime.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZpktEnv.hh>

#include "ZpktMlx.hh"
#include "ZpktMlxQueue.hh"
#include "ZpktTestCf.hh"

using namespace ZuTestUtil;

static ZuUnion<Zpkt::NifInfo, ZeException> unexpectedQuery(ZuCSpan)
{
  ZmAssert_(false);
  return ZeEXCEPT(Error, "Zpkt.MlxTest", "unexpected host interface query");
}

template <typename Fn>
static bool onTx(ZiMultiplex &mx, Fn fn)
{
  return ZmBlock<bool>{}([&mx, &fn](auto done) {
    mx.txRun([&fn, done]() mutable { done(fn()); });
  });
}

static void loop()
{
  ZuTestScope(loop);
  ZiMultiplex mx{ZiMxParams{}};
  ZmRef<Zpkt::Env> env = new Zpkt::Env;
  env->nifQueryFn(unexpectedQuery);
  auto cf = ZpktTest::object();
  ZpktTest::value(cf.ptr(), "module", ZPKT_MLX_MODULE);
  auto entry = ZpktTest::object(ZpktTest::object(cf.ptr(), "interfaces"), "_loop");
  ZpktTest::value(entry, "rxDepth", 1);
  ZpktTest::value(entry, "txDepth", 1);
  env->init(cf.ptr());
  cf = nullptr;
  ZuCHECK(env->loopback(), "configured mlx module advertises software loopback");

  auto opened = env->nif(&mx, "_loop");
  if (opened.is<ZeException>()) { ZuCHECK(false, opened.p<ZeException>()); return; }
  ZmRef<Zpkt::Nif> nif = opened.p<Zpkt::Nif *>();
  auto cached = env->nif(&mx, "_loop");
  ZuCHECK(cached.is<Zpkt::Nif *>() && cached.p<Zpkt::Nif *>() == nif.ptr() &&
    nif->ifindex() == 0 && nif->mtu() == 1500 && nif->rxQueues() == 1,
    "same name reuses the software Nif and reports its geometry");

  auto ingress = nif->bufPool(Zi::IOBufType::Rx, 1, 1514);
  auto tx = nif->bufPool(Zi::IOBufType::TxUDP, 2, 64);
  Zi::IOBufPoolList pools;
  pools.pushNode(tx.ptr());
  auto allocation = nif->alloc(ingress.ptr(), pools);
  if (allocation.is<ZeException>()) { ZuCHECK(false, allocation.p<ZeException>()); return; }
  if (!mx.start()) { ZuCHECK(false, "start mlx fixture multiplexer"); return; }

  auto a = tx->alloc(), b = tx->alloc();
  ZmAssert_(a && b);
  memset(a, 0x6a, 64);
  memset(b, 0x6b, 64);
  ZmSemaphore received, rxDone;
  uint8_t *held = nullptr;
  bool bytesOK = false;
  auto receive = [&nif, &received, &held, &bytesOK, a](uint8_t *data, unsigned length, ZiRxBurst *burst) {
    bytesOK = !burst && data != a && length == 64 && !memcmp(data, a, length);
    held = data;
    nif->wake();
    received.post();
  };
  bool started = nif->start(receive);
  ZuCHECK(started && nif->ready() && !nif->zeroCopy() && !nif->tcpChksumOffload(),
    "software loop uses normal start and reports copy mode");
  if (started) {
    mx.rxRun([&nif, &rxDone] { nif->rx(); rxDone.post(); });
    ZuCHECK(onTx(mx, [&nif, a, b] {
      const Zpkt::TxPacket frames[]{{a, 64}, {b, 64}};
      return nif->send(frames) == 1 && nif->txReady(a) == Zi::NotReady &&
	nif->txReady(b) == Zi::OK;
    }), "loop accepts only the leading frame at depth one");

    auto deadline = Zm::now() + ZuTime{5};
    bool delivered = !received.timedwait(deadline);
    if (!delivered) nif->wake();
    rxDone.wait();
    ZuCHECK(delivered && bytesOK, "Rx owner receives an exact copy in a distinct pool slot");
    ZuCHECK(onTx(mx, [&nif, a, b] {
      return nif->pollTx() == 1 && !nif->pollTx() && nif->txReady(a) == Zi::OK &&
	nif->send(b, 64) == Zi::NotReady;
    }), "isolated send retires while an exhausted Rx pool still applies backpressure");
    if (held) {
      auto first = held;
      held = nullptr;
      mx.rxRun([&nif, &rxDone] { nif->rx(); rxDone.post(); });
      ingress->free(first);
      bool resubmitted = onTx(mx, [&nif, a] {
	auto end = Zm::now() + ZuTime{2};
	do {
	  auto result = nif->send(a, 64);
	  if (result == Zi::OK) return nif->pollTx() == 1;
	  if (result != Zi::NotReady) return false;
	  Zm::sleep(ZuTime{0, 100000});
	} while (Zm::now() < end);
	return false;
      });
      bool receivedAgain = resubmitted && !received.timedwait(Zm::now() + ZuTime{3});
      if (!receivedAgain) nif->wake();
      rxDone.wait();
      ZuCHECK(receivedAgain && bytesOK, "off-owner return replenishes the blocked receiver");
      if (held) ingress->free(held);
    }
    ZuCHECK(nif->stop(), "stop before attempting to finalize with held buffers");
    nif->final();
    ZuCHECK(nif->env() == env.ptr() && !!nif->error(),
      "finalization with caller leases leaves the device registered");
    ZuCHECK(nif->start(receive) &&
      onTx(mx, [&nif, a] { return nif->send(a, 64) == Zi::OK && nif->pollTx() == 1; }) &&
      nif->stop(), "restart reuses retained Tx backing and safely discards queued ingress");
  }
  if (started && nif->state() == ZmEngineState::Stopped) {
    ZmSemaphore stopped;
    bool txOwner = false, stopOK = false;
    bool run = nif->start(
      [&nif, &ingress, &stopped, &stopOK](uint8_t *data, unsigned, ZiRxBurst *) {
	ingress->free(data);
	nif->stop([&stopped, &stopOK](bool ok) { stopOK = ok; stopped.post(); });
      },
      [&mx, &txOwner] { txOwner = mx.invoked(mx.txThread()); });
    if (run) {
      mx.rxRun([&nif, &rxDone] { nif->rx(); rxDone.post(); });
      bool sent = onTx(mx, [&nif, a] { return nif->send(a, 64) == Zi::OK; });
      bool complete = sent && !stopped.timedwait(Zm::now() + ZuTime{3});
      if (!complete) nif->stop();
      rxDone.wait();
      ZuCHECK(complete && stopOK && txOwner,
	"asynchronous stop from Rx drains the accepted send and calls back on Tx");
    } else {
      ZuCHECK(false, "start asynchronous stop fixture");
    }
  }
  tx->free(a);
  tx->free(b);
  if (nif->state() != ZmEngineState::Stopped) nif->stop();
  nif->final();
  mx.stop();
  auto replacement = env->nif(&mx, "_loop");
  ZuCHECK(!nif->env() && replacement.is<Zpkt::Nif *>() &&
    replacement.p<Zpkt::Nif *>() != nif.ptr() && env->final().is<void>(),
    "software Nif finalization retires the cached child and permits reopen");
}

static void portSelection()
{
  ZuTestScope(portSelection);
  struct Reply {
    nlmsghdr header;
    struct Field {
      nlattr	attr;
      uint32_t	value;
    } fields[3];
  };
  auto reply = [](unsigned device, unsigned number, unsigned ifindex) {
    constexpr unsigned type = RDMA_NL_GET_TYPE(RDMA_NL_NLDEV, RDMA_NLDEV_CMD_PORT_GET);
    constexpr unsigned length = NLA_HDRLEN + sizeof(uint32_t);
    return Reply{{sizeof(Reply), type, NLM_F_MULTI, 1, 0}, {
      {{length, RDMA_NLDEV_ATTR_DEV_INDEX}, device},
      {{length, RDMA_NLDEV_ATTR_PORT_INDEX}, number},
      {{length, RDMA_NLDEV_ATTR_NDEV_INDEX}, ifindex}
    }};
  };
  auto first = reply(0, 1, 12), second = reply(0, 2, 19);
  Zpkt::Mlx::Port port{};
  ZuCHECK(!Zpkt::Mlx::matchPort(first.header, port, 19, 0) && !port.number &&
      !Zpkt::Mlx::matchPort(second.header, port, 19, 0) &&
      port.device == 0 && port.number == 2,
    "RDMA discovery chooses the matching port on a two-port device");
  auto conflicting = reply(2, 1, 19);
  ZuCHECK(Zpkt::Mlx::matchPort(conflicting.header, port, 19, 2) == ENOTUNIQ,
    "the same netdev on two devices is ambiguous");
  second.fields[1].attr.nla_len = 3;
  ZuCHECK(Zpkt::Mlx::matchPort(second.header, port, 19, 0) == EPROTO,
    "malformed port attributes are rejected");
}

struct Verbs {
  ibv_context		context{};
  ibv_pd		pd{};
  ibv_comp_channel	channel{};
  ibv_cq		rx{}, tx{};
  ibv_qp_ex		qp{};
  ibv_mr		mr[2]{};
  ibv_wc		rxWC{}, txWC{};
  ibv_sge		rxSGE{}, txSGE{};
  uint64_t		rxID = 0, txID = 0;
  unsigned		mrs = 0, destroys = 0, transitions = 0, acks = 0, arms = 0;
  unsigned		rxPosts = 0, creates = 0;
  bool			rxDone = false, txDone = false, event = false, qpOK = false;
  bool			badTransition = false, badDestroy = false, badReg = false;
  bool			badDereg = false, sendCalled = false;
  int			sendError = 0;
};

static Verbs *verbs = nullptr;

extern "C" ibv_mr *__wrap_ibv_reg_mr(ibv_pd *pd, void *address, size_t length, int access)
{
  ZmAssert_(pd == &verbs->pd && verbs->mrs < 2);
  unsigned id = verbs->mrs;
  if (id && verbs->badReg) { errno = ENOMEM; return nullptr; }
  ZmAssert_(access == (id ? 0 : IBV_ACCESS_LOCAL_WRITE));
  auto &mr = verbs->mr[verbs->mrs++];
  mr.addr = address;
  mr.length = length;
  mr.lkey = 101 + id;
  return &mr;
}

extern "C" int __wrap_ibv_dereg_mr(ibv_mr *mr)
{
  ZmAssert_(verbs->mrs && mr == &verbs->mr[verbs->mrs - 1]);
  if (verbs->badDereg) return EBUSY;
  --verbs->mrs;
  return 0;
}

extern "C" int __wrap_ibv_query_device(ibv_context *ctx, ibv_device_attr *attr)
{
  ZmAssert_(ctx == &verbs->context);
  attr->max_cqe = attr->max_qp_wr = 8;
  return 0;
}

extern "C" ibv_comp_channel *__wrap_ibv_create_comp_channel(ibv_context *ctx)
{
  ZmAssert_(ctx == &verbs->context);
  auto &channel = verbs->channel;
  channel.fd = eventfd(0, EFD_CLOEXEC);
  channel.context = ctx;
  return channel.fd >= 0 ? &channel : nullptr;
}

extern "C" int __wrap_ibv_destroy_comp_channel(ibv_comp_channel *channel)
{
  ZmAssert_(channel == &verbs->channel);
  int rc = close(channel->fd);
  channel->fd = -1;
  return rc ? errno : 0;
}

extern "C" ibv_cq *__wrap_ibv_create_cq(
  ibv_context *ctx, int depth, void *, ibv_comp_channel *channel, int vector)
{
  ZmAssert_(ctx == &verbs->context && !vector && depth == 1);
  ZmAssert_(!channel || channel == &verbs->channel);
  auto &cq = channel ? verbs->rx : verbs->tx;
  cq.context = ctx;
  cq.channel = channel;
  cq.cqe = depth;
  ++verbs->creates;
  return &cq;
}

extern "C" int __wrap_ibv_destroy_cq(ibv_cq *cq)
{
  ZmAssert_(cq == &verbs->rx || cq == &verbs->tx);
  --verbs->creates;
  return 0;
}

extern "C" int __wrap_ibv_get_cq_event(ibv_comp_channel *channel, ibv_cq **cq, void **)
{
  ZmAssert_(channel == &verbs->channel);
  if (!verbs->event) { errno = EAGAIN; return -1; }
  verbs->event = false;
  *cq = &verbs->rx;
  return 0;
}

extern "C" void __wrap_ibv_ack_cq_events(ibv_cq *cq, unsigned count)
{
  ZmAssert_(cq == &verbs->rx);
  verbs->acks += count;
}

extern "C" ibv_qp *__wrap_mlx5dv_create_qp(
  ibv_context *ctx, ibv_qp_init_attr_ex *attr, mlx5dv_qp_init_attr *dv)
{
  verbs->qpOK = ctx == &verbs->context && attr->pd == &verbs->pd &&
    attr->qp_type == IBV_QPT_RAW_PACKET && !dv->comp_mask &&
    attr->send_cq == &verbs->tx && attr->recv_cq == &verbs->rx &&
    attr->comp_mask == (IBV_QP_INIT_ATTR_PD | IBV_QP_INIT_ATTR_SEND_OPS_FLAGS) &&
    attr->send_ops_flags == IBV_QP_EX_WITH_SEND && !attr->sq_sig_all &&
    attr->cap.max_send_wr == 1 && attr->cap.max_recv_wr == 1 &&
    attr->cap.max_send_sge == 1 && attr->cap.max_recv_sge == 1 &&
    !attr->cap.max_inline_data;
  verbs->qp.qp_base.context = ctx;
  verbs->transitions = 0;
  return &verbs->qp.qp_base;
}

extern "C" ibv_qp_ex *__wrap_ibv_qp_to_qp_ex(ibv_qp *qp)
{
  ZmAssert_(qp == &verbs->qp.qp_base);
  return &verbs->qp;
}

extern "C" int __wrap_ibv_modify_qp(ibv_qp *qp, ibv_qp_attr *attr, int mask)
{
  ZmAssert_(qp == &verbs->qp.qp_base);
  unsigned i = verbs->transitions++;
  constexpr int states[]{IBV_QPS_INIT, IBV_QPS_RTR, IBV_QPS_RTS};
  if (i >= 3 || attr->qp_state != states[i] ||
      mask != (i ? IBV_QP_STATE : IBV_QP_STATE | IBV_QP_PORT) ||
      (!i && attr->port_num != 2)) return EPROTO;
  return i == 1 && verbs->badTransition ? EIO : 0;
}

extern "C" int __wrap_ibv_destroy_qp(ibv_qp *qp)
{
  ZmAssert_(qp == &verbs->qp.qp_base);
  if (verbs->badDestroy) return EBUSY;
  ++verbs->destroys;
  return 0;
}

static void wireOps(Verbs &v)
{
  v.pd.context = &v.context;
  v.context.ops.post_recv = [](ibv_qp *qp, ibv_recv_wr *wr, ibv_recv_wr **) {
    ZmAssert_(qp == &verbs->qp.qp_base && wr->num_sge == 1 && !wr->next);
    verbs->rxID = wr->wr_id;
    verbs->rxSGE = *wr->sg_list;
    ++verbs->rxPosts;
    return 0;
  };
  v.context.ops.poll_cq = [](ibv_cq *cq, int count, ibv_wc *wc) {
    ZmAssert_(count == 1 && (cq == &verbs->rx || cq == &verbs->tx));
    bool rx = cq == &verbs->rx;
    auto &pending = rx ? verbs->rxDone : verbs->txDone;
    if (!pending) return 0;
    pending = false;
    *wc = rx ? verbs->rxWC : verbs->txWC;
    return 1;
  };
  v.context.ops.req_notify_cq = [](ibv_cq *cq, int solicited) {
    ZmAssert_(cq == &verbs->rx && !solicited);
    ++verbs->arms;
    return 0;
  };
  v.qp.wr_start = [](ibv_qp_ex *) { verbs->sendCalled = false; };
  v.qp.wr_send = [](ibv_qp_ex *qp) {
    ZmAssert_(qp->wr_flags == IBV_SEND_SIGNALED);
    verbs->txID = qp->wr_id;
    verbs->sendCalled = true;
  };
  v.qp.wr_set_sge = [](ibv_qp_ex *, uint32_t key, uint64_t address, uint32_t length) {
    verbs->txSGE = {address, length, key};
  };
  v.qp.wr_complete = [](ibv_qp_ex *) {
    ZmAssert_(verbs->sendCalled);
    return verbs->sendError;
  };
}

static void nativeQueues()
{
  ZuTestScope(nativeQueues);
  using namespace Zpkt::Mlx;
  Verbs v;
  verbs = &v;
  wireOps(v);
  ZmRef<BufPool> rx = new BufPool{Zi::IOBufType::Rx, 1, 1514, 32, 0, 64};
  ZmRef<BufPool> tx = new BufPool{Zi::IOBufType::TxTCP, 1, 256, 32, 0, 128};
  Zi::IOBufPoolList list;
  list.pushNode(tx.ptr());
  PoolSet pools;
  v.badReg = true;
  auto failed = pools.alloc(rx.ptr(), list, {}, 1500, &v.pd);
  ZuCHECK(failed.is<ZeException>() && !pools.allocated() && !v.mrs,
    "failed second registration unwinds the first pool before publishing");
  v.badDereg = true;
  failed = pools.alloc(rx.ptr(), list, {}, 1500, &v.pd);
  ZuCHECK(failed.is<ZeException>() && !pools.allocated() && v.mrs == 1,
    "failed cleanup retains the provisional registration without exposing its backing");
  v.badReg = false;
  failed = pools.alloc(rx.ptr(), list, {}, 1500, &v.pd);
  ZuCHECK(failed.is<ZeException>() && !pools.allocated() && v.mrs == 1,
    "allocation retry waits for the earlier registration to be released");
  v.badDereg = false;
  auto allocated = pools.alloc(rx.ptr(), list, {}, 1500, &v.pd);
  ZuCHECK(allocated.is<void>() && pools.allocated() && v.mrs == 2,
    "registration retry publishes both pools");

  Queue queue;
  v.badTransition = true;
  ZuCHECK(queue.start(&v.pd, 2, 1, 1) == EIO && v.transitions == 2 &&
      !queue.close() && v.destroys == 1 && !v.creates,
    "failed QP transition tears down its QP, CQs and completion channel");
  v.badTransition = false;
  int started = queue.start(&v.pd, 2, 1, 1);
  ZuCHECK(!started && v.qpOK && v.transitions == 3 &&
      fcntl(queue.fd(), F_GETFL) & O_NONBLOCK,
    "raw Ethernet QP uses the selected port and provider completions");
  if (started) {
    queue.close();
    pools.final();
    return;
  }

  ZuCHECK(!queue.refill(*rx) && queue.rxPosted() == 1 && v.rxPosts == 1 &&
      v.rxSGE.lkey == rx->lkey() && v.rxSGE.length >= 1518 && v.rxID,
    "Rx work posts one registered slot with space for VLAN headers");
  v.rxDone = true;
  v.rxWC.wr_id = v.rxID;
  v.rxWC.opcode = IBV_WC_RECV;
  v.rxWC.byte_len = 60;
  v.event = true;
  int error = 0;
  RxFrame frame{};
  ZuCHECK(!queue.arm() && v.arms == 1 && queue.receive(&frame, 1, error) == 1 &&
      !error && frame.data == reinterpret_cast<uint8_t *>(v.rxSGE.addr) &&
      frame.length == 60 && !queue.ack() && v.acks == 1,
    "armed recheck delivers the Rx slot and drains a queued CQ event");
  if (frame.data) rx->free(frame.data);

  auto data = tx->alloc();
  ZmAssert_(data && !(uintptr_t(data) & 127));
  auto slot = pools.findTx(data + 16);
  v.sendError = ENOMEM;
  ZuCHECK(queue.send(slot, data + 16, 64) == ENOMEM && !queue.txPending() &&
      slot->leases.load_() == Slot::Caller,
    "provider refusal rolls back only the submission lease");
  v.sendError = 0;
  ZuCHECK(!queue.send(slot, data + 16, 64) && v.txID &&
      v.txSGE.addr == uintptr_t(data + 16) && v.txSGE.length == 64 &&
      v.txSGE.lkey == tx->lkey() && queue.send(slot, data, 60) == EAGAIN,
    "one outstanding send uses the caller's exact registered extent");
  tx->free(data);
  ZuCHECK(!tx->alloc(), "caller return leaves the device's submission live");
  v.txWC.wr_id = v.txID;
  v.txWC.opcode = IBV_WC_SEND;
  v.txDone = true;
  Completion completion{};
  int completed = queue.retire(&completion, 1, error);
  ZuCHECK(completed == 1 && !error && completion.slot == slot && completion.length == 64,
    "the signaled completion returns the exact submission");
  if (completion.slot) completion.slot->release(Slot::Submitted);

  auto again = tx->alloc();
  ZmAssert_(again == data);
  ZuCHECK(!queue.send(slot, again, 60), "recycled backing can be submitted again");
  tx->free(again);
  v.txWC.wr_id = v.txID;
  v.txWC.status = IBV_WC_WR_FLUSH_ERR;
  v.txDone = true;
  ZuCHECK(!queue.retire(&completion, 1, error) && error == EIO && !tx->alloc(),
    "an error completion defers lease retirement until safe teardown");
  v.badDestroy = true;
  ZuCHECK(queue.close() == EBUSY && !tx->alloc(),
    "failed QP destroy retains backing that can still be referenced");
  v.badDestroy = false;
  ZuCHECK(!queue.close() && v.destroys == 2 && !v.creates && !tx->used() &&
      pools.final().is<void>() && !v.mrs,
    "successful teardown retires outstanding work before deregistering backing");
  verbs = nullptr;
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZiLog::init("ZpktMlxTest");
  ZiLog::level(0);
  ZiLog::start();
  ZuTestMain();
  ZuTestCall(loop);
  ZuTestCall(portSelection);
  ZuTestCall(nativeQueues);
  ZiLog::stop();
}
