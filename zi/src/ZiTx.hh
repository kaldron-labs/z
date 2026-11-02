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
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZmList.hh>

ZmListDerive(ZiTxQueue, ZiIOBuf, ZmListNode<ZiIOBuf, ZmListHeapID<"">>);

using ZiTxBuf = ZiTxQueue::Node;

template <
  unsigned Size = ZiIOBuf_DefltSize,
  unsigned MaxSize = ZiIOBuf_DefltMaxSize,
  ZuString HeapID = ZiIOBuf_HeapID{}()>
using ZiTxBufAlloc = Zi::IOBufAlloc<ZiTxBuf, Size, MaxSize, ZuStringT<HeapID>>;

// CRTP - implementation must conform to the following interface:
#if 0
struct Impl : public ZiTx<Impl> {
  ZiConnection *cxn();				// optional
  void sending(ZiIOContext &);			// optional metadata setup
  void sent(ZmRef<ZiTxBuf>, bool ok);		// ''
  void aborted(ZmRef<ZiTxBuf> buf, bool ok);	// ''
};
#endif
template <typename Impl_>
class ZiTx {
public:
  ZiTxQueue	txQueue;

  using Impl = Impl_;

  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  static auto impl(const ZiIOBuf *buf) {
    return static_cast<Impl *>(buf->owner);
  }

  ZiConnection *cxn() { return static_cast<ZiConnection *>(impl()); }

  void send(ZmRef<ZiTxBuf> buf) {
    send(impl()->cxn(), ZuMv(buf));
  }

  // Bind an already prepared wire representation to its physical connection.
  template <typename Cxn>
  void send(Cxn *cxn, ZmRef<ZiTxBuf> buf) {
    if (ZuUnlikely(!buf || !cxn)) return;
    buf->owner = impl();
    cxn->send(ZiIOFn::mvFn(ZuMv(buf),
      [](ZmRef<ZiIOBuf> buf, ZiIOContext &io) {
	if (ZuUnlikely(!io.cxn->up())) return false;
	auto self = impl(buf);
	auto &queue = self->ZiTx::txQueue;
	queue.pushNode(ZmRef<ZiTxBuf>::acquire(
	  static_cast<ZiTxBuf *>(ZuMv(buf).release())));
	if (!io.completed()) return true;
	io.init(ZiIOFn{self, [](Impl *self, ZiIOContext &io) {
	  auto &queue = self->ZiTx::txQueue;
	  if (io.buf) {
	    bool ok = io.length >= 0;
	    self->sent(ZmRef<ZiTxBuf>::acquire(
	      static_cast<ZiTxBuf *>(ZuMv(io.buf).release())), ok);
	    if (!ok) { queue.clean(); io.complete(); }
	  } else {
	    io.buf = queue.shift();
	    if (!io.buf) io.complete();
	    else self->sending(io);
	  }
	  return true;
	}});
	return io();
      }));
  }

  void sending(ZiIOContext &) { } // optional address/TOS from the installed buffer
  void sent(ZmRef<ZiTxBuf>, bool ok) { } // can be overridden

  void abort(ZmRef<ZiTxBuf> buf) {
    auto cxn = impl()->cxn();
    if constexpr (!ZuIsSame<decltype(&Impl::cxn), decltype(&ZiTx::cxn)>{})
      if (ZuUnlikely(!cxn)) return;
    cxn->mx()->txInvoke([buf = ZuMv(buf)]() mutable {
      auto impl_ = impl(buf);
      auto &queue = impl_->ZiTx::txQueue;
	bool ok = !!queue.delNode(buf);
	impl_->aborted(ZuMv(buf), ok);
    });
  }

  void aborted(ZmRef<ZiTxBuf> buf, bool ok) { } // can be overridden
};

#endif /* ZiTx_HH */
