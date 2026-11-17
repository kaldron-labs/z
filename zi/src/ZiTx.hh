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

// CRTP - implementation must conform to the following interface:
#if 0
struct Impl : public ZiTx<Impl> {
  ZiConnection *cxn();				// optional
  unsigned maxQueueLen();			// ''
  void sent(ZmRef<ZiIOBuf>, bool ok);		// ''
  void sending(ZiTxContext &);			// '' - per-buffer address/TOS
  void aborted(ZmRef<ZiIOBuf> buf, bool ok);	// ''
};
#endif
template <typename Impl_>
class ZiTx {
public:
  Zi::IOQueue	txQueue;

  using Impl = Impl_;

  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  static auto impl(const ZiIOBuf *buf) {
    return static_cast<Impl *>(buf->owner);
  }

  ZiConnection *cxn() { return static_cast<ZiConnection *>(impl()); }

  void send(ZmRef<ZiIOBuf> buf) {
    send(impl()->cxn(), ZuMv(buf));
  }

  // Bind an already prepared wire representation to its physical connection.
  template <typename Cxn>
  void send(Cxn *cxn, ZmRef<ZiIOBuf> buf) {
    if (ZuUnlikely(!buf || !buf->length || !cxn)) return;
    buf->owner = impl();
    cxn->send(ZiTxFn::mvFn(ZuMv(buf), [](ZmRef<ZiIOBuf> buf, ZiTxContext &io) {
      // A queued setup can run after abrupt close and physical-queue handoff.
      if (ZuUnlikely(!io.cxn->up())) return false;
      auto impl_ = impl(buf);
      auto &queue = impl_->ZiTx::txQueue;
      if (auto count = queue.count_()) {
	if (count >= impl_->maxQueueLen())
	  impl_->sent(ZuMv(buf), false);
	else
	  queue.pushNode(ZuMv(buf));
	return true;
      }
      auto buf_ = buf.ptr();
      queue.pushNode(ZuMv(buf));
      io.init(ZiTxFn{buf_,
	[](ZiIOBuf *buf, ZiTxContext &io) {
	  auto impl_ = impl(buf);
	  auto &queue = impl_->ZiTx::txQueue;
	  if (ZuUnlikely(io.length < 0)) {
	    impl_->sent(queue.shift(), false);
	    queue.clean();
	    io.complete();
	    return true;
	  }
	  if (ZuUnlikely((io.offset += io.length) < io.buf->length)) return true;
	  impl_->sent(queue.shift(), true);
	  if (queue.count_()) {
	    io.buf = queue.headNode();
	    io.fn.object(io.buf.ptr());
	    io.offset = 0;
	    impl_->sending(io);
	  } else {
	    io.complete();
	  }
	  return true;
	}}, ZmRef<ZiIOBuf>{buf_});
      impl_->sending(io);
      return true;
    }));
  }

  unsigned maxQueueLen() { return 1<<10; } // can be overridden

  void sent(ZmRef<ZiIOBuf>, bool ok) { } // can be overridden
  void sending(ZiTxContext &) { } // optional per-buffer address/TOS setup

  void abort(ZmRef<ZiIOBuf> buf) {
    auto cxn = impl()->cxn();
    if constexpr (!ZuIsSame<decltype(&Impl::cxn), decltype(&ZiTx::cxn)>{})
      if (ZuUnlikely(!cxn)) return;
    cxn->mx()->txInvoke([buf = ZuMv(buf)]() mutable {
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

  void aborted(ZmRef<ZiIOBuf> buf, bool ok) { } // can be overridden
};

#endif /* ZiTx_HH */
