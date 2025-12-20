//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// IO Tx
// - CRTP sender

#ifndef ZiTx_HH
#define ZiTx_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <zlib/ZiIOContext.hh>

ZuDerive(ZiTxQueue,
  (ZmList<ZiIOBuf, ZmListNode<ZiIOBuf, ZmListHeapID<"">>>));

using ZiTxBuf = ZiTxQueue::Node;

template <
  unsigned Size = ZiIOBuf_DefltSize,
  unsigned MaxSize = ZiIOBuf_DefltMaxSize,
  ZuString HeapID = ZiIOBuf_HeapID{}()>
using ZiTxBufAlloc = Zi::IOBufAlloc<ZiTxBuf, Size, MaxSize, ZuStringT<HeapID>>;

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

  void send(ZmRef<ZiTxBuf> buf) {
    buf->owner = impl();
    impl()->ZiConnection::send(ZiIOFn::mvFn(ZuMv(buf),
	[](ZmRef<ZiIOBuf> buf, ZiIOContext &io) {
	  auto impl_ = impl(buf);
	  auto &queue = impl_->ZiTx::txQueue;
	  if (queue.count_()) {
	    queue.pushNode(ZuMv(buf));
	    return true;
	  }
	  auto buf_ = buf.ptr();
	  queue.pushNode(ZuMv(buf));
	  io.init(ZiIOFn{queue.headPtr(),
	    [](ZiIOBuf *buf, ZiIOContext &io) {
	      auto impl_ = impl(buf);
	      auto &queue = impl_->ZiTx::txQueue;
	      if (ZuUnlikely(io.length < 0)) {
		impl_->sent(queue.shift(), false);
		queue.clean();
		io.complete();
		return true;
	      }
	      if (ZuUnlikely((io.offset += io.length) < io.size)) return true;
	      impl_->sent(queue.shift(), true);
	      if (queue.count_()) {
		buf = queue.headPtr();
		io.fn.object(buf);
		io.ptr = buf->data();
		io.size = buf->size;
		io.offset = 0;
	      } else {
		io.complete();
	      }
	      return true;
	    }}, buf_->data(), buf_->length, 0);
	  return true;
	}));
  }

  void sent(ZmRef<ZiTxBuf>, bool ok) { } // can be overridden

  void abort(ZmRef<ZiTxBuf> buf) {
    impl()->txInvoke([buf = ZuMv(buf)]() mutable {
      auto impl_ = impl(buf);
      auto &queue = impl_->ZiTx::txQueue;
      if (queue.headPtr() == buf.ptr()) {
	impl_->aborted(ZuMv(buf), false); // too late to abort
	return;
      }
      bool ok = !!queue.delNode(buf);
      impl_->aborted(ZuMv(buf), ok);
    });
  }

  void aborted(ZmRef<ZiTxBuf> buf, bool ok) { } // can be overridden
};

#endif /* ZiTx_HH */
