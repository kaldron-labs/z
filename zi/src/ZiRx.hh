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

#include <limits.h>

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
using ZiRxBufAlloc = Zi::IOBufAlloc<ZiRxBuf, Size, MaxSize, ZuStringT<HeapID>>;

// Hdr examines queued spans and returns the frame size, INT_MAX while its
// header is incomplete, or a negative error. Body examines that prefix and
// returns bytes consumed, zero to discard pending input, or a negative error.
// Body may retain backing/ranges; it must not advance the stream itself.
// Network callbacks take (Impl *, const ZiIOContext &, Stream &, [size]);
// memory callbacks omit the context. Neither callback owns a socket destination.
template <typename Impl_>
class ZiRx {
public:
  using Impl = Impl_;
  using Stream = ZiRxStream<ZiRxQueue>;

  ZiRx(unsigned maxSize = ZiIOBuf_DefltMaxSize) : m_maxSize{maxSize} { }

  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  template <auto Hdr, auto Body>
  void recv(ZiIOContext &io) { arm_<Hdr, Body>(io); }

  // Synchronous consumers borrow spans only for the duration of Body.
  // Asynchronous consumers explicitly retain the original backing/ranges.
  template <auto Hdr, auto Body>
  void recvSync(ZiIOContext &io) { arm_<Hdr, Body>(io); }

  void rxReset() { m_stream.clean(); m_frameLen = 0; }
  const Stream &rxStream() const { return m_stream; }

  // Borrowed memory needs ownership acquisition. The caller supplies the
  // final-buffer allocator at this boundary, not as a network receiver policy.
  template <auto Hdr, auto Body, typename Alloc>
  int recvMem(const uint8_t *data, unsigned length, Alloc &&alloc) {
    auto buf = alloc();
    if (ZuUnlikely(!buf->append(data, length))) return -1;
    m_stream.push(ZuMv(buf));
    int result = process_<Hdr, Body, false>(nullptr);
    return result > 0 ? int(length) : result;
  }
  template <auto Hdr, auto Body, typename Alloc>
  int recvMemSync(const uint8_t *data, unsigned length, Alloc &&alloc) {
    return recvMem<Hdr, Body>(data, length, ZuFwd<Alloc>(alloc));
  }

private:
  template <auto Hdr, auto Body>
  void arm_(ZiIOContext &io) {
    io.init(ZiIOFn{impl(), [](Impl *self, ZiIOContext &io) {
      auto rx = static_cast<ZiRx *>(self);
      if (io.buf) {
	auto buf = ZmRef<ZiRxBuf>::acquire(
	  static_cast<ZiRxBuf *>(ZuMv(io.buf).release()));
	buf->owner = self;
	rx->m_stream.push(ZuMv(buf));
      }
      if (rx->template process_<Hdr, Body, true>(&io) < 0) io.disconnect();
      return true;
    }});
  }

  template <auto Hdr, auto Body, bool Network>
  int process_(const ZiIOContext *io) {
    while (m_stream) {
      if (!m_frameLen || m_frameLen == INT_MAX) {
	if constexpr (Network)
	  m_frameLen = ZuInvoke<Hdr>(impl(), *io, m_stream);
	else
	  m_frameLen = ZuInvoke<Hdr>(impl(), m_stream);
	if (ZuUnlikely(m_frameLen < 0 ||
	    (m_frameLen != INT_MAX && unsigned(m_frameLen) > m_maxSize)))
	  return -1;
      }
      if (m_frameLen == INT_MAX || m_stream.length() < unsigned(m_frameLen)) return 0;
      int consumed;
      if constexpr (Network)
	consumed = ZuInvoke<Body>(impl(), *io, m_stream, unsigned(m_frameLen));
      else
	consumed = ZuInvoke<Body>(impl(), m_stream, unsigned(m_frameLen));
      if (ZuUnlikely(consumed < 0)) return consumed;
      if (!consumed) { rxReset(); return 1; }
      m_stream.advance(unsigned(consumed));
      m_frameLen = 0;
    }
    return 1;
  }

  Stream		m_stream;
  unsigned		m_maxSize;
  int			m_frameLen = 0;
};

#endif /* ZiRx_HH */
