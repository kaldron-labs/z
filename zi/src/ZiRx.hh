//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// IO Rx
// - framing CRTP receiver
// - implementation supplies Hdr, Body functions (see below)
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

  // int Hdr(Impl *, const ZiIOContext &io, ZiIOBuf *buf)
  //   +ve - length of hdr+body
  //   INT_MAX - insufficient data
  //   -ve - disconnect

  // asynchronous (queued) receive from ZiIOContext
  //
  // int Body(Impl *, const ZiIOContext &io, ZmRef<ZiIOBuf> buf)
  //   0   - skip remaining data (defends against DOS)
  //   +ve - buffer consumed
  //   -ve - disconnect immediately
  template <auto Hdr, auto Body>
  void recv(ZiIOContext &io) {
    ZmRef<ZiIOBuf> buf = new BufAlloc{impl()};
    auto ptr = buf->data();
    auto size = buf->size;
    io.init(ZiIOFn{ZuMv(buf), [](ZiIOBuf *buf, ZiIOContext &io) {
      unsigned len = io.offset += io.length;
      io.length = 0;

      // scan header
      buf->length = len;
      int frameLen = ZuInvoke<Hdr>(impl(buf), io, buf);
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
	if (frameLen != INT_MAX && ZuUnlikely(io.size < unsigned(frameLen))) {
	  // grow buffer
	  if (ZuUnlikely(!buf->ensure(frameLen))) {
	    io.disconnect();
	    return true;
	  }
	  io.ptr = buf->data();
	  io.size = buf->size;
	}
	return true;
      }

      // due to queuing, cannot recycle rx msg buffer for the next message
      ZmRef<ZiIOBuf> next;
      unsigned nextLen = len - frameLen;
      uint8_t *nextPtr = nullptr;

      // copy any trailing data that is (part of) the next message
      if (nextLen) {
	next = new BufAlloc{impl(buf)};
	nextPtr = next->ensure(nextLen);
	memcpy(nextPtr, io.ptr + frameLen, nextLen);
	next->length = nextLen;
	buf->length = frameLen;
      }

      // process body
      frameLen = ZuInvoke<Body>(impl(buf), io, io.fn.mvObject<ZiIOBuf>());
      if (ZuUnlikely(frameLen < 0)) {
	io.disconnect();
	return true;
      }
      if (!frameLen) return true;

      // no trailing data - allocate blank next message
      if (!next) {
	next = new BufAlloc{impl(buf)};
	nextPtr = next->data();
      }

      // set up I/O context for next message
      io.ptr = nextPtr;
      io.size = next->size;
      io.offset = 0;
      io.length = nextLen;
      io.fn.object(ZuMv(next));
      return false;
    }}, ptr, size, 0);
  }

  // synchronous receive from ZiIOContext
  //
  // int Body(const ZiIOContext &io, ZiIOBuf *buf, unsigned len)
  //   0   - skip remaining data (defends against DOS)
  //   +ve - length of hdr+body (may be <= that returned by Hdr())
  //   -ve - disconnect immediately
  template <auto Hdr, auto Body>
  void recvSync(ZiIOContext &io) {
    ZmRef<ZiIOBuf> buf = new BufAlloc{impl()};
    auto ptr = buf->data();
    auto size = buf->size;
    io.init(ZiIOFn{ZuMv(buf), [](ZiIOBuf *buf, ZiIOContext &io) {
      unsigned len = io.offset += io.length;
      io.length = 0;

      // scan header
      buf->length = len;
      int frameLen = ZuInvoke<Hdr>(impl(buf), io, buf);
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
	if (frameLen != INT_MAX && ZuUnlikely(io.size < unsigned(frameLen))) {
	  // grow buffer
	  if (ZuUnlikely(!buf->ensure(frameLen))) {
	    io.disconnect();
	    return true;
	  }
	  io.ptr = buf->data();
	  io.size = buf->size;
	}
	return true;
      }

      // process body
      frameLen = ZuInvoke<Body>(impl(buf), io, buf, frameLen);
      if (ZuUnlikely(frameLen < 0)) {
	io.disconnect();
	return true;
      }
      if (!frameLen) return true;

      // calculate length of trailing data
      unsigned nextLen = len - frameLen;

      // move trailing data down - recycle buffer for subsequent messages
      if (nextLen) memmove(io.ptr, io.ptr + frameLen, nextLen);

      // set up I/O context for next message
      io.offset = 0;
      io.length = buf->length = nextLen;
      return false;
    }}, ptr, size, 0);
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
    if (!buf) buf = new BufAlloc{impl()};
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
	next = new BufAlloc{impl()};
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
    if (!buf) buf = new BufAlloc{impl()};
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
