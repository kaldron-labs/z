//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// IO Rx
// - framing CRTP receiver
// - implementation supplies Hdr, Body functions (see below)
// - implementation supplies an App framing rxAllocator()
// - ZuInvoke is used - functions can be implementation member functions,
//   bound functions (first parameter is Impl *), or unbound

#ifndef ZiRx_HH
#define ZiRx_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <zlib/ZuInvoke.hh>

#include <zlib/ZmRef.hh>

#include <zlib/ZiLog.hh>

#include <zlib/ZiIOContext.hh>
#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiRxStream.hh>

using ZiRxBuf = ZiRxQueue::Node;

template <typename Impl_, unsigned MaxBufSize_>
class ZiRx {
public:
  using Impl = Impl_;
  enum { MaxBufSize = MaxBufSize_ };

  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  static auto impl(const ZiIOBuf *buf) {
    return static_cast<Impl *>(buf->owner);
  }

public:
  // I/O receiver

  // int Hdr(Impl *, const ZiRxContext &io, ZiIOBuf *buf)
  //   +ve - length of hdr+body
  //   INT_MAX - insufficient data
  //   -ve - disconnect

  // asynchronous (queued) receive from ZiRxContext
  //
  // int Body(Impl *, const ZiRxContext &io, ZmRef<ZiIOBuf> buf)
  //   0   - skip remaining data (defends against DOS)
  //   +ve - buffer consumed
  //   -ve - disconnect immediately
  template <auto Hdr, auto Body>
  void recv(ZiRxContext &io) { arm<false, Hdr, Body>(io); }

  // synchronous receive from ZiRxContext
  //
  // int Body(Impl *, const ZiRxContext &io, ZiIOBuf *buf, unsigned len)
  //   0   - skip remaining data (defends against DOS)
  //   +ve - consumed length, no greater than the framed length
  //   -ve - disconnect immediately
  template <auto Hdr, auto Body>
  void recvSync(ZiRxContext &io) { arm<true, Hdr, Body>(io); }

private:
  // rxAllocator() supplies App message storage only. Transport receive slots
  // always come from the physical connection's network allocator.
  static ZmRef<ZiIOBuf> copyFrame(Impl *impl_, const ZiIOBuf *buf, unsigned off) {
    unsigned len = buf->length - off;
    auto copy = impl_->rxAllocator()->allocBuf(impl_, len);
    memcpy(copy->data(), buf->data() + off, len);
    return copy;
  }

  template <bool Sync, auto Hdr, auto Body>
  static ZmRef<ZiIOBuf> frames(
    Impl *impl_, ZiRxContext &io, const ZmAnyFn &current,
    ZmRef<ZiIOBuf> msg, ZmRef<ZiIOBuf> input)
  {
    while (msg || input) {
      if (!msg) msg = ZuMv(input);
      unsigned len = msg->length;
      int frameLen = ZuInvoke<Hdr>(impl_, io, msg.ptr());
      if (io.fn != current) return nullptr;
      if (ZuUnlikely(frameLen < 0 ||
	  (frameLen != INT_MAX && unsigned(frameLen) > MaxBufSize) ||
	  (frameLen == INT_MAX && len >= MaxBufSize))) {
	ZiLOG(Error, "ZiRx", "message too big / corrupt");
	io.disconnect();
	return nullptr;
      }
      if (len < unsigned(frameLen)) {
	// An incomplete packet is copied once into App storage. Subsequent
	// chunks append to that accumulator, never to the transport target.
	if (msg->allocator() != impl_->rxAllocator()) {
	  msg = copyFrame(impl_, msg, 0);
	}
	unsigned limit = frameLen == INT_MAX ? MaxBufSize : unsigned(frameLen);
	unsigned reserve = limit;
	if (frameLen == INT_MAX) {
	  reserve = len + (input ? input->length : 0);
	  if (reserve > limit) reserve = limit;
	}
	if (ZuUnlikely(!msg->ensure(reserve))) { io.disconnect(); return nullptr; }
	if (!input) return msg;
	unsigned n = input->length;
	if (n > limit - len) n = limit - len;
	memcpy(msg->data() + len, input->data(), n);
	msg->length += n;
	input->skip += n;
	input->length -= n;
	if (!input->length) input = nullptr;
	continue;
      }

      if constexpr (Sync) {
	int used = ZuInvoke<Body>(impl_, io, msg.ptr(), unsigned(frameLen));
	// Body may have destroyed/replaced its receive callback. All input
	// held here is local; never touch the retired closure's accumulator.
	if (io.fn != current) return nullptr;
	if (ZuUnlikely(used < 0 || unsigned(used) > unsigned(frameLen))) {
	  io.disconnect();
	  return nullptr;
	}
	if (!used) return nullptr;
	msg->skip += used;
	msg->length -= used;
	if (!msg->length) msg = nullptr;
      } else {
	ZmRef<ZiIOBuf> next;
	if (len > unsigned(frameLen)) {
	  next = copyFrame(impl_, msg, frameLen);
	  msg->length = frameLen;
	}
	int used = ZuInvoke<Body>(impl_, io, ZuMv(msg));
	if (io.fn != current) return nullptr;
	if (ZuUnlikely(used < 0)) { io.disconnect(); return nullptr; }
	if (!used) return nullptr;
	msg = ZuMv(next);
      }
    }
    return nullptr;
  }

  template <bool Sync, auto Hdr, auto Body>
  void arm(ZiRxContext &io) {
    auto impl_ = impl();
    // Owner-directed setup also notifies a plugin when Body explicitly rearms.
    io.cxn->recv(ZiRxFn{[impl_](ZiRxContext &io) {
      auto buf = io.cxn->info().rxAllocator->allocBuf(impl_, 0);
      if (ZuUnlikely(!buf)) { io.disconnect(); return true; }
      io.init(ZiRxFn{[
	impl_, pending = ZmRef<ZiIOBuf>{}
      ](ZiRxContext &io) mutable {
	if (!io.buf || !io.buf->length) return true; // empty/stub before receive
	// Keep this invocation alive across Body completion/replacement, and
	// move accumulation out before calling any external parser callback.
	auto current = io.fn;
	auto implLocal = impl_;
	auto msg = ZuMv(pending);
	auto input = ZuMv(io.buf);
	io.buf = io.cxn->info().rxAllocator->allocBuf(implLocal, 0);
	if (ZuUnlikely(!io.buf)) { io.disconnect(); return true; }
	auto next = frames<Sync, Hdr, Body>(
	  implLocal, io, current, ZuMv(msg), ZuMv(input));
	if (io.fn == current) pending = ZuMv(next);
	return true;
      }}, ZuMv(buf));
      return true;
    }});
  }

public:
  // in-memory receiver

  // int Hdr(Impl *, const ZiIOBuf *buf)
  //   +ve - length of hdr+body
  //   INT_MAX - insufficient data
  //   -ve - disconnect

  // asynchronous recv from memory
  // returns bytes consumed, -1 on error
  //
  // int Body(Impl *, ZmRef<ZiIOBuf> buf)
  //   0   - skip remaining data (defends against DOS)
  //   +ve - buffer consumed
  //   -ve - disconnect immediately
  template <auto Hdr, auto Body>
  int recvMem(const uint8_t *data, unsigned rxLen, ZmRef<ZiIOBuf> &buf) {
    if (!buf) buf = impl()->rxAllocator()->allocBuf(impl());
    unsigned oldLen = buf->length;
    unsigned len = oldLen + rxLen;
    auto rxData = buf->ensure(len);
    if (ZuUnlikely(!rxData)) return -1;
    memcpy(rxData + oldLen, data, rxLen);
    buf->length = len;

    while (len) {
      // scan header
      int frameLen = ZuInvoke<Hdr>(impl(buf), buf);
      if (ZuUnlikely(frameLen < 0)) return -1;
      if (ZuUnlikely(frameLen != INT_MAX && unsigned(frameLen) > MaxBufSize)) {
	ZiLOG(Error, "ZiRx", "message too big / corrupt");
	return -1;
      }
      if (len < unsigned(frameLen)) {
	if (frameLen != INT_MAX && ZuUnlikely(buf->size < unsigned(frameLen))) {
	  // grow buffer
	  if (ZuUnlikely(!buf->ensure(frameLen))) return -1;
	}
	return 0;
      }

      // due to queuing, cannot recycle rx msg buffer for the next message
      ZmRef<ZiIOBuf> next;
      unsigned nextLen = len - frameLen;
      // copy any trailing data that is (part of) the next message(s)
      if (nextLen) {
	next = impl()->rxAllocator()->allocBuf(impl(), nextLen);
	if (ZuUnlikely(!next)) return -1;
	memcpy(next->data(), buf->data() + frameLen, nextLen);
	buf->length = frameLen;
      }

      // process body
      frameLen = ZuInvoke<Body>(impl(buf), ZuMv(buf));
      if (ZuUnlikely(frameLen < 0)) return -1; // error
      if (!frameLen) return rxLen; // EOF - discard remainder

      // move on to next message/buffer
      buf = ZuMv(next);
      len = nextLen;
    }
    return rxLen;
  }

  // synchronous recv from memory (e.g. TLS)
  // returns bytes consumed, -1 on error
  //
  // int Hdr(Impl *, const ZiIOBuf *buf)
  //   >=0 - minimum size of body (may be 0)
  //   -ve - disconnect immediately
  //
  // int Body(Impl *, const ZiIOBuf *buf, unsigned len)
  //   0   - EOF, skip remaining data (defends against DOS)
  //   +ve - length of hdr+body (may be <= that returned by Hdr())
  //   -ve - disconnect immediately
  template <auto Hdr, auto Body>
  int recvMemSync(const uint8_t *data, unsigned rxLen, ZmRef<ZiIOBuf> &buf) {
    if (!buf) buf = impl()->rxAllocator()->allocBuf(impl());
    unsigned oldLen = buf->length;
    unsigned len = oldLen + rxLen;
    auto rxData = buf->ensure(len);
    if (ZuUnlikely(!rxData)) return -1;
    memcpy(rxData + oldLen, data, rxLen);
    buf->length = len;

    while (len) {
      // scan header
      int frameLen = ZuInvoke<Hdr>(impl(buf), buf);
      if (ZuUnlikely(frameLen < 0)) return -1;
      if (ZuUnlikely(frameLen != INT_MAX && unsigned(frameLen) > MaxBufSize)) {
	ZiLOG(Error, "ZiRx", "message too big / corrupt");
	return -1;
      }
      if (len < unsigned(frameLen)) {
	if (frameLen != INT_MAX && ZuUnlikely(buf->size < unsigned(frameLen))) {
	  // grow buffer
	  if (ZuUnlikely(!buf->ensure(frameLen))) return -1;
	}
	return 0;
      }

      // process body
      frameLen = ZuInvoke<Body>(impl(buf), buf.ptr(), frameLen);
      if (ZuUnlikely(frameLen < 0)) return frameLen; // error
      if (ZuUnlikely(!frameLen)) return rxLen; // EOF - discard remainder

      // skip to next msg
      buf->skip += frameLen;
      rxData += frameLen;
      buf->length = (len -= frameLen);
    }
    if (len && buf->skip) {
      // move down any trailing data
      buf->skip = 0;
      memmove(buf->data(), rxData, len);
    } else
      buf->skip = 0;
    buf->length = len;
    return rxLen;
  }
};

#endif /* ZiRx_HH */
