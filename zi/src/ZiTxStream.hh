//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// I/O Transmit Stream
// - streams output into a sequence of fixed-capacity buffers
// - each buffer is assumed to be a single protocol frame, e.g. a TLS record
// - applications define:
//   - maxSize - overall capacity for each buffer
//   - headRoom - how much space is reserved for a frame header
//   - tailRoom - how much space is reserved for a frame trailer
//   - how new buffers are allocated - the allocBuf_ callback:
//     - ZmRef<IOBuf> allocBuf_(unsigned headRoom)
//     - return a new buffer with skip == headRoom
//   - how buffers are sent - the sendBuf_ callback
//     - bool sendBuf_(ZmRef<IOBuf> buf, bool final)
//     - send buf (via lower-level protocol)
//     - final is false for rollover and true for flush/destruction
//     - return false on failure

#ifndef ZiTxStream_HH
#define ZiTxStream_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <zlib/ZuSpan.hh>
#include <zlib/ZuPrint.hh>

#include <zlib/ZmFn.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZiIOBuf.hh>

namespace Zi {

using TxErrorFn = ZmFn<bool(bool, ZeException &)>;

struct Flush { };
inline Flush flush() { return {}; }

// CRTP - implementation must conform to the following interface:
#if 0
struct Impl : public TxStream<Impl> {
  using Base = TxStream<Impl>;

  ZmRef<ZiIOBuf> allocBuf_(unsigned headRoom);

  bool sendBuf_(ZmRef<ZiIOBuf>, bool final);
};
#endif

template <typename Impl>
class TxStream {
  TxStream(const TxStream &) = delete;
  TxStream &operator =(const TxStream &) = delete;

public:
  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  TxStream(unsigned maxSize, unsigned headRoom, unsigned tailRoom) :
    m_maxSize(maxSize), m_headRoom(headRoom), m_tailRoom(tailRoom) { }
  ~TxStream() { flush(); }

  TxStream(TxStream &&stream) :
    m_maxSize{stream.m_maxSize},
    m_headRoom{stream.m_headRoom},
    m_tailRoom{stream.m_tailRoom},
    m_buf{ZuMv(stream.m_buf)},
    m_failed{stream.m_failed}
  {
    stream.m_failed = false;
  }
  TxStream &operator =(TxStream &&stream) {
    if (this == &stream) return *this;
    flush();
    m_maxSize = stream.m_maxSize;
    m_headRoom = stream.m_headRoom;
    m_tailRoom = stream.m_tailRoom;
    m_buf = ZuMv(stream.m_buf);
    m_failed = stream.m_failed;
    stream.m_failed = false;
    return *this;
  }

  unsigned maxSize() const { return m_maxSize; }
  unsigned headRoom() const { return m_headRoom; }
  unsigned tailRoom() const { return m_tailRoom; }
  bool failed() const { return m_failed; }
  bool operator !() const { return m_failed; }
  ZuOpBool

private:
  void allocBuf() { m_buf = impl()->allocBuf_(m_headRoom); }
  void ensureBuf() { if (!m_failed && !m_buf) allocBuf(); }
  void fail() { m_buf = {}; m_failed = true; }
  bool sendBuf() {
    if (ZuUnlikely(m_failed)) return false;
    if (ZuUnlikely(!impl()->sendBuf_(ZuMv(m_buf), false))) {
      fail();
      return false;
    }
    allocBuf();
    return true;
  }
  bool flushBuf() {
    if (ZuUnlikely(m_failed)) return false;
    if (ZuUnlikely(!impl()->sendBuf_(ZuMv(m_buf), true))) {
      fail();
      return false;
    }
    m_buf = {};
    return true;
  }

public:
  void append(const uint8_t *data, unsigned length) {
    if (!length || ZuUnlikely(m_failed)) return;
    ensureBuf();
    for (;;) {
      unsigned total = m_buf->length + m_headRoom + m_tailRoom;
      ZmAssert(total <= m_maxSize);
      unsigned avail = m_maxSize - total;
      unsigned length_ = length > avail ? avail : length;
      if (length_) {
	m_buf->append(data, length_);
	data += length_;
	length -= length_;
      }
      if (!length) return;
      if (ZuUnlikely(!sendBuf())) return;
    }
  }

private:
  template <typename U, typename R = void>
  using MatchPDelegate = ZuIfT<ZuPrint<U>::Delegate, R>;
  template <typename U, typename R = void>
  using MatchPBuffer = ZuIfT<ZuPrint<U>::Buffer, R>;

  template <typename P>
  MatchPDelegate<P> append(P &&p) {
    if (ZuUnlikely(m_failed)) return;
    ensureBuf();
    ZuPrint<P>::print(*m_buf, ZuFwd<P>(p));
  }
  template <typename P>
  MatchPBuffer<P> append(const P &p) {
    if (ZuUnlikely(m_failed)) return;
    ensureBuf();
    unsigned length_ = ZuPrint<P>::length(p);
    unsigned bufLen = m_buf->length;
    unsigned total = bufLen + m_headRoom + m_tailRoom;
    ZmAssert(total <= m_maxSize); // sanity check on current buf
    unsigned avail = m_maxSize - total;
    if (avail < length_) {
      // need new buf, unless the output itself exceeds an empty buffer
      if (bufLen && ZuUnlikely(!sendBuf())) return;
      avail = m_maxSize - (m_headRoom + m_tailRoom);
      if (length_ > avail)
	throw ZeEXCEPT(Fatal, "ZiTxStream", ([avail, length_](auto &s) {
	  s << "output length " << length_ << " exceeds maximum size " << avail;
	}));
      bufLen = 0;
    }
    m_buf->length = bufLen + ZuPrint<P>::print(
	reinterpret_cast<char *>(m_buf->ensure(bufLen + length_) + bufLen),
	length_, p);
  }

  template <typename U, typename R = void>
  using MatchChar = ZuIfT<ZuEquiv<U, char>{}, R>;

  template <typename U, typename R = void>
  using MatchReal = ZuIfT<
    ZuTraits<U>::IsPrimitive &&
    ZuTraits<U>::IsReal &&
    !ZuEquiv<U, char>{}, R>;

  template <typename U, typename R = void>
  using MatchPrint = ZuIfT<
    ZuPrint<U>::OK && !ZuPrint<U>::String, R>;

public:
  // ZuBSpan will match any string/span type
  TxStream &operator <<(ZuBSpan buf) {
    append(buf.data(), buf.length());
    return *this;
  }
  template <typename C>
  MatchChar<C, TxStream &> operator <<(C c) {
    return *this << ZuSpan{&c, 1};
  }
  template <typename R>
  MatchReal<R, TxStream &> operator <<(const R &r) {
    append(ZuBoxed(r));
    return *this;
  }
  template <typename P>
  MatchPrint<P, TxStream &> operator <<(const P &p) {
    append(p);
    return *this;
  }

  // flush output
  void flush() {
    if (m_buf && m_buf->length)
      flushBuf();
  }
  TxStream &operator <<(Flush) {
    flush();
    return *this;
  }

private:
  unsigned		m_maxSize;
  unsigned		m_headRoom;
  unsigned		m_tailRoom;
  ZmRef<ZiIOBuf>	m_buf;
  bool			m_failed = false;
};

// CRTP - implementation must conform to the following interface:
#if 0
struct Impl : public TxLayer<Impl, ...> {
  using Base = TxStream<Impl, ...>;

  void prepareBuf_(ZiIOBuf *, bool final); // prepare for sending
};
#endif

template <typename Impl, typename Below>
class TxLayer : public TxStream<TxLayer<Impl, Below>> {
  using Base = TxStream<TxLayer<Impl, Below>>;

public:
  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  TxLayer(Below &below, unsigned headRoom, unsigned tailRoom) :
    TxLayer(below, headRoom, tailRoom, below.maxSize()) { }
  TxLayer(
      Below &below, unsigned headRoom, unsigned tailRoom,
      unsigned maxSize) :
    Base(
      maxSize < below.maxSize() ? maxSize : below.maxSize(),
      below.headRoom() + headRoom,
      below.tailRoom() + tailRoom),
    m_below(below)
  {
    m_below.flush();
  }
  ~TxLayer() {
    this->flush();
    m_below.flush();
  }

  ZmRef<ZiIOBuf> allocBuf_(unsigned headRoom) {
    return m_below.allocBuf_(headRoom);
  }

  bool sendBuf_(ZmRef<ZiIOBuf> buf, bool final) {
    impl()->prepareBuf_(buf, final);
    return m_below.sendBuf_(ZuMv(buf), final);
  }

private:
  Below			&m_below;
};

} // Zi

template <typename Impl>
using ZiTxStream = Zi::TxStream<Impl>;

using ZiTxErrorFn = Zi::TxErrorFn;

template <typename Impl, typename Lower>
using ZiTxLayer = Zi::TxLayer<Impl, Lower>;

#endif /* ZiTxStream_HH */
