//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// IO Rx
// - framing CRTP receiver
// - implementation supplies Hdr, Body functions (see below)
// - implementation supplies a typed rxAllocator()
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
#include <zlib/ZiRxStream.hh>

using ZiRxBuf = ZiRxQueue::Node;

template <
  unsigned Size = ZiIOBuf_DefltSize,
  unsigned MaxSize = ZiIOBuf_DefltMaxSize,
  ZuString HeapID = ZiIOBuf_HeapID{}()>
using ZiRxBufAlloc = Zi::IOBufBuiltin<ZiRxBuf, Size, ZiIOBuf_DefltAlign, ZuStringT<HeapID>, MaxSize>;

template <typename Impl_, typename BufAlloc_>
class ZiRx {
public:
  using Impl = Impl_;
  using BufAlloc = BufAlloc_;
  static constexpr unsigned MaxBufSize = BufAlloc::MaxSize;

  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  static auto impl(const ZiIOBuf *buf) {
    return static_cast<Impl *>(buf->owner);
  }

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
  void recv(ZiRxContext &io) {
    auto fn = [](ZiRxContext &io) {
      auto buf = io.buf.ptr();
      auto impl_ = impl(buf);
      unsigned len = buf->length;

      // scan header
      int frameLen = ZuInvoke<Hdr>(impl_, io, buf);
      if (ZuUnlikely(frameLen < 0)) {
	io.disconnect();
	return true;
      }
      if (ZuUnlikely(frameLen != INT_MAX && unsigned(frameLen) > MaxBufSize)) {
	ZiLOG(Error, "ZiRx", "TCP message too big / corrupt");
	io.disconnect();
	return true;
      }
      if (len < unsigned(frameLen)) {
	if (frameLen != INT_MAX && ZuUnlikely(buf->size < unsigned(frameLen))) {
	  // grow buffer
	  if (ZuUnlikely(!buf->ensure(frameLen))) {
	    io.disconnect();
	    return true;
	  }
	}
	return true;
      }

      // due to queuing, cannot recycle rx msg buffer for the next message
      ZmRef<ZiIOBuf> next;
      unsigned nextLen = len - frameLen;
      uint8_t *nextPtr = nullptr;

      // copy any trailing data that is (part of) the next message(s)
      if (nextLen) {
	next = impl_->rxAllocator()->allocBuf(impl_);
	nextPtr = next->ensure(nextLen);
	memcpy(nextPtr, buf->data() + frameLen, nextLen);
	next->length = nextLen;
	buf->length = frameLen;
      }

      // process body
      frameLen = ZuInvoke<Body>(impl_, io, ZuMv(io.buf));
      if (ZuUnlikely(frameLen < 0)) {
	io.disconnect();
	return true;
      }
      if (!frameLen) {
	// Body may have rearmed reception with its own buffer.
	if (!io.completed() && !io.buf)
	  io.buf = impl_->rxAllocator()->allocBuf(impl_);
	return true;
      }

      // no trailing data - allocate blank next message
      if (!next) {
	next = impl_->rxAllocator()->allocBuf(impl_);
      }

      // set up I/O context for next message
      io.buf = ZuMv(next);
      return false;
    };
    if (!io.buf) io.buf = impl()->rxAllocator()->allocBuf(impl());
    if (io.fn)
      io.fn = ZiRxFn{ZuMv(fn)};
    else
      io.init(ZiRxFn{ZuMv(fn)}, ZuMv(io.buf));
  }

  // synchronous receive from ZiRxContext
  //
  // int Body(const ZiRxContext &io, ZiIOBuf *buf, unsigned len)
  //   0   - skip remaining data (defends against DOS)
  //   +ve - length of hdr+body (may be <= that returned by Hdr())
  //   -ve - disconnect immediately
  template <auto Hdr, auto Body>
  void recvSync(ZiRxContext &io) {
    auto fn = [](ZiRxContext &io) {
      auto buf = io.buf.ptr();
      auto impl_ = impl(buf);
      unsigned len = buf->length;

      // scan header
      int frameLen = ZuInvoke<Hdr>(impl_, io, buf);
      if (ZuUnlikely(frameLen < 0)) {
	io.disconnect();
	return true;
      }
      if (ZuUnlikely(frameLen != INT_MAX && unsigned(frameLen) > MaxBufSize)) {
	ZiLOG(Error, "ZiRx", "ZiRx::recv TCP message too big / corrupt");
	io.disconnect();
	return true;
      }
      if (len < unsigned(frameLen)) {
	if (frameLen != INT_MAX && ZuUnlikely(buf->size < unsigned(frameLen))) {
	  // grow buffer
	  if (ZuUnlikely(!buf->ensure(frameLen))) {
	    io.disconnect();
	    return true;
	  }
	}
	return true;
      }

      // process body
      frameLen = ZuInvoke<Body>(impl_, io, buf, frameLen);
      if (ZuUnlikely(frameLen < 0)) {
	io.disconnect();
	return true;
      }
      if (!frameLen) return true;

      // calculate length of trailing data
      unsigned nextLen = len - frameLen;

      // move trailing data down - recycle buffer for subsequent messages
      if (nextLen) memmove(buf->data(), buf->data() + frameLen, nextLen);

      // set up I/O context for next message
      buf->length = nextLen;
      return false;
    };
    if (!io.buf) io.buf = impl()->rxAllocator()->allocBuf(impl());
    if (io.fn)
      io.fn = ZiRxFn{ZuMv(fn)};
    else
      io.init(ZiRxFn{ZuMv(fn)}, ZuMv(io.buf));
  }

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
      uint8_t *nextPtr = nullptr;

      // copy any trailing data that is (part of) the next message
      if (nextLen) {
	next = impl()->rxAllocator()->allocBuf(impl());
	nextPtr = next->ensure(nextLen);
	memcpy(nextPtr, buf->data() + frameLen, nextLen);
	next->length = nextLen;
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
