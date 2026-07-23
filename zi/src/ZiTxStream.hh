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
//     - void sendBuf_(ZmRef<IOBuf> buf)
//     - send buf (via lower-level protocol)

#ifndef ZiTxStream_HH
#define ZiTxStream_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <zlib/ZuSpan.hh>
#include <zlib/ZuPrint.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZiIOBuf.hh>

namespace Zi {

struct Flush { };
inline Flush flush() { return {}; }

// CRTP - implementation must conform to the following interface:
#if 0
struct Impl : public TxStream<Impl> {
  using Base = TxStream<Impl>;

  ZmRef<ZiIOBuf> allocBuf_(unsigned headRoom);

  void sendBuf_(ZmRef<ZiIOBuf>);
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

  TxStream(TxStream &&) = default;
  TxStream &operator =(TxStream &&) = default;

  unsigned maxSize() const { return m_maxSize; }
  unsigned headRoom() const { return m_headRoom; }
  unsigned tailRoom() const { return m_tailRoom; }

private:
  void allocBuf() { m_buf = impl()->allocBuf_(m_headRoom); }
  void ensureBuf() { if (!m_buf) allocBuf(); }
  void sendBuf() { impl()->sendBuf_(ZuMv(m_buf)); allocBuf(); }
  void flushBuf() { impl()->sendBuf_(ZuMv(m_buf)); m_buf = {}; }

public:
  void append(const uint8_t *data, unsigned length) {
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
      sendBuf();
    }
  }

private:
  template <typename U, typename R = void>
  using MatchPDelegate = ZuIfT<ZuPrint<U>::Delegate, R>;
  template <typename U, typename R = void>
  using MatchPBuffer = ZuIfT<ZuPrint<U>::Buffer, R>;

  template <typename P>
  MatchPDelegate<P> append(P &&p) {
    ensureBuf();
    ZuPrint<P>::print(*m_buf, ZuFwd<P>(p));
  }
  template <typename P>
  MatchPBuffer<P> append(const P &p) {
    ensureBuf();
    unsigned length_ = ZuPrint<P>::length(p);
    unsigned bufLen = m_buf->length;
    unsigned total = bufLen + m_headRoom + m_tailRoom;
    ZmAssert(total <= m_maxSize); // sanity check on current buf
    unsigned avail = m_maxSize - total;
    if (avail < length_) {
      // need new buf
      sendBuf();
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
  void flush() { if (m_buf && m_buf->length) flushBuf(); }
  TxStream &operator <<(Flush) {
    flush();
    return *this;
  }

private:
  unsigned		m_maxSize;
  unsigned		m_headRoom;
  unsigned		m_tailRoom;
  ZmRef<ZiIOBuf>	m_buf;
};

// CRTP - implementation must conform to the following interface:
#if 0
struct Impl : public TxLayer<Impl, ...> {
  using Base = TxStream<Impl, ...>;

  void prepareBuf_(ZiIOBuf *); // prepare for sending
};
#endif

template <typename Impl, typename Below>
class TxLayer : public TxStream<TxLayer<Impl, Below>> {
  using Base = TxStream<TxLayer<Impl, Below>>;

public:
  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  TxLayer(Below &below, unsigned headRoom, unsigned tailRoom) :
    Base(
      below.maxSize(),
      below.headRoom() + headRoom,
      below.tailRoom() + tailRoom),
    m_below(below)
  {
    m_below.flush();
  }
  ~TxLayer() { m_below.flush(); }

  ZmRef<ZiIOBuf> allocBuf_(unsigned headRoom) {
    return m_below.allocBuf_(headRoom);
  }

  void sendBuf_(ZmRef<ZiIOBuf> buf) {
    impl()->prepareBuf_(buf);
    m_below.sendBuf_(ZuMv(buf));
  }

private:
  Below			&m_below;
};

} // Zi

template <typename Impl>
using ZiTxStream = Zi::TxStream<Impl>;

template <typename Impl, typename Lower>
using ZiTxLayer = Zi::TxLayer<Impl, Lower>;

#endif /* ZiTxStream_HH */
