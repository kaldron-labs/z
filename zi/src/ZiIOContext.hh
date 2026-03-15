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

struct ZiIOContext {
  ZiConnection	*cxn = nullptr;	// connection - set by ZiMultiplex
  ZmAnyFn	fn;		// callback - set by app (clear to complete I/O)
  uint8_t	*ptr = nullptr;	// buffer - set by app (clear to disconnect)
  unsigned	size = 0;	// size of buffer - set by app
  unsigned	offset = 0;	// offset within buffer - set by app
  int		length = 0;	// length - set by ZiMultiplex (-1 on error)
  ZiSockAddr	addr;		// UDP - set by app (send) / ZiMultiplex (recv)

  static constexpr uintptr_t invalid_ptr() { return uintptr_t(-1); }

private:
friend ZiConnection;
  // initialize (called from within send/recv)
  template <typename Fn>
  void init_(Fn &&fn_) {
    fn = ZuFwd<Fn>(fn_); ptr = nullptr; size = offset = length = 0; (*this)();
  }

public:
  // send/receive
  template <typename Fn>
  void init(Fn &&fn_, const void *ptr_, unsigned size_, unsigned offset_) {
    ZmAssert(size_);
    fn = ZuFwd<Fn>(fn_);
    ptr = static_cast<uint8_t *>(const_cast<void *>(ptr_));
    size = size_; offset = offset_; length = 0;
  }
  // UDP send
  template <typename Fn, typename Addr>
  void init(Fn &&fn_,
      const void *ptr_, unsigned size_, unsigned offset_, Addr &&addr_) {
    ZmAssert(size_);
    fn = ZuFwd<Fn>(fn_);
    ptr = static_cast<uint8_t *>(const_cast<void *>(ptr_));
    size = size_; offset = offset_; length = 0;
    addr = ZuFwd<Addr>(addr_);
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
    return reinterpret_cast<uintptr_t>(ptr) == invalid_ptr();
  }

  bool operator()();	// return true if complete
};
using ZiIOFn = ZmFn<bool(ZiIOContext &)>;
inline bool ZiIOContext::operator ()() { return fn.as<ZiIOFn>()(*this); }

#endif /* ZiIOContext_HH */
