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

#include <zlib/ZmAssert.hh>
#include <zlib/ZmFn.hh>

#include <zlib/ZiIP.hh>
#include <zlib/ZiIOBuf.hh>

class ZiConnection;

using ZiTOS = ZuUnion<void, uint8_t>;	// IP TOS raw byte

struct ZiIOContext {
  ZiConnection	*cxn = nullptr;	// connection - set by ZiMultiplex
  ZmAnyFn	fn;		// callback - set by app
  ZmRef<Zi::IOBuf> buf;		// original backing, moved through the I/O path
  unsigned	offset = 0;	// Tx accepted bytes; Rx length lives in buf
  int		length = 0;	// length - set by ZiMultiplex (-1 on error)
  unsigned	flags = 0;	// receive message flags
  ZiSockAddr	addr;		// UDP - set by app (send) / ZiMultiplex (recv)
  ZiTOS		tos;		// TOS raw byte

  bool		disconnect_ = false;

  // Rx delivers the original buffer; consumers move buf out and keep fn armed.
  // Tx calls fn without buf to select pending output, and with buf to report
  // its final result, once all kernel holds retire. Selecting subsequent
  // output does not imply retirement of an earlier buffer. The backend alone
  // updates offset/length; the callback never repeats that progress update.

private:
friend ZiConnection;
  // initialize (called from within send/recv)
  template <typename L>
  void init_(L &&l) {
    fn = ZuFwd<L>(l);
    buf = nullptr;
    offset = length = 0;
    flags = 0;
    disconnect_ = false;
    tos = {};
  }

public:
  // send/receive
  template <typename L>
  void init(L &&l, ZmRef<Zi::IOBuf> buf_ = {}, unsigned offset_ = 0) {
    fn = ZuFwd<L>(l);
    buf = ZuMv(buf_);
    offset = offset_; length = 0;
    flags = 0;
    disconnect_ = false;
    tos = {};
  }
  // UDP send
  template <typename L, typename Addr>
  void init(L &&l,
    ZmRef<Zi::IOBuf> buf_, unsigned offset_,
    Addr &&addr_, ZiTOS tos_ = {})
  {
    init(ZuFwd<L>(l), ZuMv(buf_), offset_);
    addr = ZuFwd<Addr>(addr_);
    tos = tos_;
  }
  bool initialized() const { return bool(fn); }

  // complete send/receive without disconnecting
  void complete() {
    fn = {};
    buf = nullptr;
  }
  bool completed() const { return !fn; }

  // complete send/receive and disconnect
  void disconnect() {
    complete();
    disconnect_ = true;
  }
  bool disconnected() const {
    return disconnect_;
  }

  bool operator()();	// return true if complete
};
using ZiIOFn = ZmFn<bool(ZiIOContext &), ZmFnHeapID<"ZiIOFn">>;
inline bool ZiIOContext::operator ()() { return fn.as<ZiIOFn>()(*this); }

#endif /* ZiIOContext_HH */
