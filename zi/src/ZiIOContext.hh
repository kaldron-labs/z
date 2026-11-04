//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// IO context
// - intentionally sacrifices encapsulation for performance

#ifndef ZiIOContext_HH
#define ZiIOContext_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <limits.h>
#include <stdint.h>

#include <zlib/ZuUnion.hh>

#include <zlib/ZmAssert.hh>
#include <zlib/ZmFn.hh>

#include <zlib/ZiIP.hh>
#include <zlib/ZiIOBuf.hh>

class ZiConnection;

using ZiTOS = ZuUnion<void, uint8_t>;	// IP TOS raw byte

struct ZiRxContext {
  ZiConnection	*cxn = nullptr;	// connection - set by ZiMultiplex
  ZmAnyFn	fn;		// callback - set by app
  ZmRef<Zi::IOBuf> buf;		// original backing, moved through the I/O path
  ZiSockAddr	addr;		// source address for UDP
  unsigned	flags = 0;	// receive message flags
  ZiTOS		tos;		// TOS raw byte
  uint8_t	state = 0;	// internal control, distinct from native flags

  enum { Disconnect = 1 };

  // Available input is buf->length, not the latest receive count. A callback
  // may move the original backing out while leaving its callback armed.
  template <typename L>
  void init(L &&l, ZmRef<Zi::IOBuf> buf_ = {}) {
    fn = ZuFwd<L>(l);
    buf = ZuMv(buf_);
    flags = 0;
    state = 0;
    tos = {};
  }

  bool initialized() const { return bool(fn); }
  void complete() { fn = {}; buf = nullptr; }
  bool completed() const { return !fn; }
  void disconnect() { complete(); state |= Disconnect; }
  bool disconnected() const { return state & Disconnect; }

  // True ends immediate callback processing; complete() separately disarms it.
  bool operator()();
};
using ZiRxFn = ZmFn<bool(ZiRxContext &), ZmFnHeapID<"ZiRxFn">>;
inline bool ZiRxContext::operator ()() { return fn.as<ZiRxFn>()(*this); }

struct ZiTxContext {
  ZiConnection	*cxn = nullptr;	// physical connection, also without backing
  ZmAnyFn	fn;		// output selection and safe retirement callback
  ZmRef<Zi::IOBuf> buf;		// original backing retained by this operation
  ZiSockAddr	addr;		// destination address for UDP
  int		length = 0;	// latest data result (-1 on error)
  unsigned	txOffset = 0;	// physical backing offset, owned by this send
  unsigned	txRemaining = UINT_MAX; // UINT_MAX: select buf's published span
  ZiTOS		tos;		// TOS raw byte
  uint8_t	flags = 0;	// packed operation state

  enum { TxDataDone = 1, Disconnect = 2, Range = 4 };

  // Tx calls fn without buf to select pending output, and with buf to report
  // its final result, once all kernel holds retire. Selecting subsequent
  // output does not imply retirement of an earlier buffer. Native next-output
  // callbacks carry the preceding data result in length (-1 on failure).
  // Retained-range progress belongs to this context and leaves its backing
  // descriptor unchanged. Ordinary exclusive buffers retain their existing
  // skip/length advancement semantics.

  void txRange(ZmRef<Zi::IOBuf> backing, unsigned offset, unsigned length_) {
    buf = ZuMv(backing); txOffset = offset; txRemaining = length_; flags |= Range;
  }
  void txSelect() {
    if (txRemaining == UINT_MAX) {
      txOffset = buf->skip; txRemaining = buf->length;
    }
  }
  uint8_t *txData() const { return buf->data_() + txOffset; }
  void txAdvance(unsigned n) {
    txOffset += n; txRemaining -= n;
    if (!(flags & Range)) buf->advance(n);
  }

  // Select output; send_(fn) uses its callback's return to decide whether to pump.
  template <typename L>
  void init(L &&l, ZmRef<Zi::IOBuf> buf_ = {}) {
    fn = ZuFwd<L>(l);
    buf = ZuMv(buf_);
    length = 0;
    flags = 0;
    txRemaining = UINT_MAX;
    tos = {};
  }
  // UDP send
  template <typename L, typename Addr>
  void init(L &&l,
    ZmRef<Zi::IOBuf> buf_,
    Addr &&addr_, ZiTOS tos_ = {})
  {
    init(ZuFwd<L>(l), ZuMv(buf_));
    addr = ZuFwd<Addr>(addr_);
    tos = tos_;
  }

  bool initialized() const { return bool(fn); }

  // Disarm without clearing a previously reported result or disconnect status.
  void complete() {
    fn = {};
    buf = nullptr;
    txRemaining = UINT_MAX; flags &= ~Range;
  }
  bool completed() const { return !fn; }

  // Disarm and request connection teardown.
  void disconnect() {
    complete();
    flags |= Disconnect;
  }
  bool disconnected() const {
    return flags & Disconnect;
  }

  // True ends immediate callback processing; complete() separately disarms it.
  bool operator()();
};
using ZiTxFn = ZmFn<bool(ZiTxContext &), ZmFnHeapID<"ZiTxFn">>;
inline bool ZiTxContext::operator ()() {
  txRemaining = UINT_MAX;
  flags &= ~Range;
  return fn.as<ZiTxFn>()(*this);
}

#endif /* ZiIOContext_HH */
