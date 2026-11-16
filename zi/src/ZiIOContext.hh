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

#include <stdint.h>

#include <zlib/ZuUnion.hh>

#include <zlib/ZmFn.hh>
#include <zlib/ZmAssert.hh>

#include <zlib/ZtEnum.hh>

#include <zlib/ZiIP.hh>
#include <zlib/ZiIOBuf.hh>

class ZiConnection;

using ZiTOS = ZuUnion<void, uint8_t>;	// IP TOS raw byte

ZtFlagsNS(ZiAPI, ZiIOState, uint8_t, Disconnect);

struct ZiRxContext {
  ZiConnection	*cxn = nullptr;	// connection - set by ZiMultiplex
  ZmAnyFn	fn;		// callback - set by app
  ZmRef<Zi::IOBuf> buf;		// original backing, moved through the I/O path
  ZiSockAddr	addr;		// source address for UDP
  unsigned	flags = 0;	// platform-native receive message flags
  uint8_t	state = 0;	// ZiIOState
  ZiTOS		tos;		// TOS raw byte

private:
friend ZiConnection;
  // initialize (called from within recv)
  template <typename L>
  void init_(L &&l) {
    fn = ZuFwd<L>(l);
    buf = nullptr;
    flags = 0;
    state = 0;
    tos = {};
    (*this)();
  }

public:
  // Available input is buf->length, not the latest receive count. A callback
  // may move the original backing out while leaving its callback armed.
  template <typename L>
  void init(L &&l, ZmRef<Zi::IOBuf> buf_) {
    ZmAssert(buf_);
    fn = ZuFwd<L>(l);
    buf = ZuMv(buf_);
    flags = 0;
    state = 0;
    tos = {};
  }

  bool initialized() const { return bool(buf); }
  void complete() { fn = {}; buf = nullptr; }
  bool completed() const { return !fn; }
  void disconnect() { complete(); state |= ZiIOState::Disconnect(); }
  bool disconnected() const { return state & ZiIOState::Disconnect(); }

  // True ends immediate callback processing; complete() separately disarms it.
  bool operator()();
};
using ZiRxFn = ZmFn<bool(ZiRxContext &), ZmFnHeapID<"ZiRxFn">>;
inline bool ZiRxContext::operator ()() { return fn.as<ZiRxFn>()(*this); }

struct ZiTxContext {
  ZiConnection	*cxn = nullptr;	// connection - set by ZiMultiplex
  ZmAnyFn	fn;		// callback - set by app
  ZmRef<Zi::IOBuf> buf;		// backing for the current send
  ZiSockAddr	addr;		// destination address for UDP
  unsigned	offset = 0;	// partial-send cursor
  int		length = 0;	// latest send result (-1 on error)
  uint8_t	state = 0;	// ZiIOState
  ZiTOS		tos;		// TOS raw byte

  template <typename L>
  void init(L &&l, ZmRef<Zi::IOBuf> buf_) {
    ZmAssert(buf_);
    fn = ZuFwd<L>(l);
    buf = ZuMv(buf_);
    offset = 0;
    length = 0;
    state = 0;
    tos = {};
  }
  template <typename L, typename Addr>
  void init(L &&l, ZmRef<Zi::IOBuf> buf_, Addr &&addr_, ZiTOS tos_ = {}) {
    init(ZuFwd<L>(l), ZuMv(buf_));
    addr = ZuFwd<Addr>(addr_);
    tos = tos_;
  }

  bool initialized() const { return bool(buf); }
  ZiAPI void complete();
  bool completed() const { return !fn; }
  void disconnect() { complete(); state |= ZiIOState::Disconnect(); }
  bool disconnected() const { return state & ZiIOState::Disconnect(); }

  bool operator()();
};
using ZiTxFn = ZmFn<bool(ZiTxContext &), ZmFnHeapID<"ZiTxFn">>;
inline bool ZiTxContext::operator ()() { return fn.as<ZiTxFn>()(*this); }

#endif /* ZiIOContext_HH */
