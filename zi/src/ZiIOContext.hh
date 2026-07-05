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

class ZiConnection;

using ZiTOS = ZuUnion<void, uint8_t>;	// IP TOS raw byte

struct ZiIOContext {
  ZiConnection	*cxn = nullptr;	// connection - set by ZiMultiplex
  ZmAnyFn	fn;		// callback - set by app
  uint8_t	*ptr = nullptr;	// buffer - set by app
  unsigned	size = 0;	// size of buffer - set by app
  unsigned	offset = 0;	// offset within buffer - set by app
  int		length = 0;	// length - set by ZiMultiplex (-1 on error)
  ZiSockAddr	addr;		// UDP - set by app (send) / ZiMultiplex (recv)
  ZiTOS		tos;		// TOS raw byte

  // ptr sentinel value to disconnect
  static constexpr uintptr_t invalid_ptr() { return uintptr_t(-1); }

private:
friend ZiConnection;
  // initialize (called from within send/recv)
  template <typename L>
  void init_(L &&l) {
    fn = ZuFwd<L>(l);
    ptr = nullptr;
    size = offset = length = 0;
    tos = {};
    (*this)();
  }

public:
  // send/receive
  template <typename L>
  void init(L &&l, const void *ptr_, unsigned size_, unsigned offset_) {
    ZmAssert(size_);
    fn = ZuFwd<L>(l);
    ptr = static_cast<uint8_t *>(const_cast<void *>(ptr_));
    size = size_; offset = offset_; length = 0;
    tos = {};
  }
  // UDP send
  template <typename L, typename Addr>
  void init(L &&l,
      const void *ptr_, unsigned size_, unsigned offset_, Addr &&addr_) {
    ZmAssert(size_);
    fn = ZuFwd<L>(l);
    ptr = static_cast<uint8_t *>(const_cast<void *>(ptr_));
    size = size_; offset = offset_; length = 0;
    addr = ZuFwd<Addr>(addr_);
    tos = {};
  }
  // initially, ptr will be null and app must set it via init()
  bool initialized() { return ptr; }

  // complete send/receive without disconnecting
  void complete() {
    fn = {};
    ptr = nullptr;
  }
  bool completed() const { return !fn; }

  // complete send/receive and disconnect
  void disconnect() {
    fn = {};
    ptr = reinterpret_cast<uint8_t *>(invalid_ptr());
  }
  bool disconnected() const {
    return ptr == reinterpret_cast<uint8_t *>(invalid_ptr());
  }

  bool operator()();	// return true if complete
};
using ZiIOFn = ZmFn<bool(ZiIOContext &), ZmFnHeapID<"ZiIOFn">>;
inline bool ZiIOContext::operator ()() { return fn.as<ZiIOFn>()(*this); }

#endif /* ZiIOContext_HH */
