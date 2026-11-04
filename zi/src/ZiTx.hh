//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// IO Tx
// - CRTP sender

#ifndef ZiTx_HH
#define ZiTx_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <zlib/ZiIOContext.hh>
#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiRxStream.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZmList.hh>

ZmListDerive(ZiTxQueue, ZiIOBuf,
  ZmListNode<ZiIOBuf, ZmListHeapID<"">>);

using ZiTxBuf = ZiTxQueue::Node;

template <
  unsigned Size = ZiIOBuf_DefltSize,
  unsigned MaxSize = ZiIOBuf_DefltMaxSize,
  ZuString HeapID = ZiIOBuf_HeapID{}()>
using ZiTxBufAlloc = Zi::IOBufAlloc<ZiTxBuf, Size, MaxSize, ZuStringT<HeapID>>;

// Compile-time pending-node operations. The default queue contains actual
// transmit buffers; protocols may instead queue existing refcounted metadata.
// Operations add no state to buffers or contexts and allocate no submission node.
struct ZiTxBufOps {
  struct State { };
  using Queue = ZiTxQueue;
  using Node = Queue::Node;
  using Ref = Queue::NodeRef;

  template <typename Impl, typename Cxn>
  ZuInline static bool prepare(Impl *, Node *, Cxn *) { return true; }
  template <typename Impl> ZuInline static void queued(Impl *, Node *, ZiTxContext &) { }
  template <typename Impl>
  ZuInline static void cleanup(Impl *, State &, Queue &, ZiConnection *) { }

  template <typename Impl> static void owner(Node *node, Impl *impl) {
    node->owner = impl;
  }
  template <typename Impl> static Impl *owner(const Node *node) {
    return static_cast<Impl *>(node->owner);
  }
  template <typename Impl>
  static void select(Impl *impl, Ref node, ZiTxContext &io) {
    io.buf = ZuMv(node);
    impl->sending(io);
  }
  template <typename Impl> static void retired(Impl *impl, ZiTxContext &io) {
    bool ok = io.length >= 0;
    auto buf = ZmRef<ZiTxBuf>::acquire(
      static_cast<ZiTxBuf *>(ZuMv(io.buf).release()));
    impl->sent(ZuMv(buf), ok);
  }
  template <typename Impl> static void aborted(Impl *impl, Ref node, bool ok) {
    impl->aborted(ZuMv(node), ok);
  }
};

// CRTP - implementation must conform to the following interface:
#if 0
struct Impl : public ZiTx<Impl> {
  ZiConnection *cxn();				// optional
  void sending(ZiTxContext &);			// optional metadata setup
  void sent(ZmRef<ZiTxBuf>, bool ok);		// ''
  void aborted(ZmRef<ZiTxBuf> buf, bool ok);	// ''
};
#endif
template <typename Impl_, typename Ops_ = ZiTxBufOps>
class ZiTx : private Ops_::State {
public:
  using Impl = Impl_;
  using Ops = Ops_;
  using State = typename Ops::State;
  using Queue = typename Ops::Queue;
  using Node = typename Queue::Node;
  using NodeRef = typename Queue::NodeRef;

  Queue		txQueue;

  State &txState_() { return *this; }

  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  static auto impl(const Node *node) {
    return Ops::template owner<Impl>(node);
  }

  ZiConnection *cxn() { return static_cast<ZiConnection *>(impl()); }

  void send(NodeRef node) {
    send(impl()->cxn(), ZuMv(node));
  }

  // Queue already owned output for its physical connection.
  template <typename Cxn>
  void send(Cxn *cxn, NodeRef node) {
    if (ZuUnlikely(!node || !cxn)) return;
    Ops::owner(node.ptr(), impl());
    if (!Ops::prepare(impl(), node.ptr(), cxn)) return;
    cxn->send(ZiTxFn::mvFn(ZuMv(node),
      [](NodeRef node, ZiTxContext &io) {
	auto self = impl(node);
	if (ZuUnlikely(!io.cxn->up())) {
	  Ops::aborted(self, ZuMv(node), true);
	  return false;
	}
	auto &queue = self->ZiTx::txQueue;
	Ops::queued(self, node.ptr(), io);
	queue.pushNode(ZuMv(node));
	if (!io.completed()) return true;
	io.init(ZiTxFn{self, [](Impl *self, ZiTxContext &io) {
	  auto &queue = self->ZiTx::txQueue;
	  if (io.buf) {
	    bool ok = io.length >= 0;
	    Ops::retired(self, io);
	    if (!ok) { self->ZiTx::clean(); io.complete(); }
	  } else {
	    auto node = queue.shift();
	    if (!node) io.complete();
	    else Ops::select(self, ZuMv(node), io);
	  }
	  return true;
	}});
	return io();
      }));
  }

  void sending(ZiTxContext &) { } // optional address/TOS from the installed buffer
  void sent(ZmRef<ZiTxBuf>, bool ok) { } // can be overridden

  // I/O Tx owns the queue and cancels dependent metadata before dropping it.
  void cleanup(ZiConnection *cxn) {
    Ops::cleanup(impl(), txState_(), txQueue, cxn);
  }
  void clean() {
    cleanup(nullptr);
    while (auto node = txQueue.shift()) Ops::aborted(impl(), ZuMv(node), true);
  }

  void abort(NodeRef node) {
    auto cxn = impl()->cxn();
    if constexpr (!ZuIsSame<decltype(&Impl::cxn), decltype(&ZiTx::cxn)>{})
      if (ZuUnlikely(!cxn)) return;
    cxn->mx()->txInvoke([node = ZuMv(node)]() mutable {
      auto impl_ = impl(node);
      auto &queue = impl_->ZiTx::txQueue;
	bool ok = !!queue.delNode(node);
	Ops::aborted(impl_, ZuMv(node), ok);
    });
  }

  void aborted(NodeRef node, bool ok) { } // can be overridden
};

#endif /* ZiTx_HH */
