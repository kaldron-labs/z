//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// IO Transmit Stream
// - streams output into a sequence of fixed-capacity buffers
// - each buffer is assumed to be a single protocol frame, e.g. a TLS record
// - applications define:
//   - maxSize - overall capacity for each buffer
//   - headRoom - how much space is reserved for a frame header
//   - tailRoom - how much space is reserved for a frame trailer
//   - how new buffers are allocated - the alloc callback:
//     - ZmRef<IOBuf> alloc(unsigned headRoom)
//     - return a new buffer with skip == headRoom
//   - how buffers are sent - the send callback
//     - void send(ZmRef<IOBuf> buf)
//     - send buf to lower-level protocol

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

template <typename Alloc, typename Send>
class TxStream {
  TxStream(const TxStream &) = delete;
  TxStream &operator =(const TxStream &) = delete;

public:
  TxStream(
    unsigned maxSize, unsigned headRoom, unsigned tailRoom,
    Alloc alloc, Send send)
  :
    m_maxSize(maxSize), m_headRoom(headRoom), m_tailRoom(tailRoom),
    m_alloc(ZuMv(alloc)), m_send(ZuMv(send)), m_buf(m_alloc(m_headRoom)) { }
  ~TxStream() = default;

  TxStream(TxStream &&) = default;
  TxStream &operator =(TxStream &&) = default;

  void append(const uint8_t *data, unsigned length) {
  next:
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
    m_send(ZuMv(m_buf));
    m_buf = m_alloc(m_headRoom);
    goto next;
  }

private:
  template <typename U, typename R = void>
  using MatchPDelegate = ZuIfT<ZuPrint<U>::Delegate, R>;
  template <typename U, typename R = void>
  using MatchPBuffer = ZuIfT<ZuPrint<U>::Buffer, R>;

  template <typename P>
  MatchPDelegate<P> append(P &&p) {
    ZuPrint<P>::print(*m_buf, ZuFwd<P>(p));
  }
  template <typename P>
  MatchPBuffer<P> append(const P &p) {
    unsigned length_ = ZuPrint<P>::length(p);
    unsigned bufLen = m_buf->length;
    unsigned total = bufLen + m_headRoom + m_tailRoom;
    ZmAssert(total <= m_maxSize);
    unsigned avail = m_maxSize - total;
    if (avail < length_) {
      m_send(ZuMv(m_buf));
      m_buf = m_alloc(m_headRoom);
      avail = m_maxSize - (m_headRoom + m_tailRoom);
      if (length_ > avail)
	throw ZeEXCEPT(Fatal, "ZiIOStream", ([avail, length_](auto &s) {
		  s << "output length " << length_ << " exceeds maximum size " << avail;
		}));
      bufLen = 0;
    }
    m_buf->length = bufLen + ZuPrint<P>::print(
	reinterpret_cast<char *>(m_buf->ensure(bufLen + length_) + bufLen),
	length_, p);
  }

  template <typename U, typename R = void>
  using MatchChar = ZuSame<U, char, R>;

  template <typename U, typename R = void>
  using MatchReal = ZuIfT<
    ZuTraits<U>::IsPrimitive &&
    ZuTraits<U>::IsReal &&
    !ZuIsSame<U, char>{}, R>;

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
    append(reinterpret_cast<const uint8_t *>(&c), 1);
    return *this;
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
  TxStream &operator <<(Flush) {
    if (m_buf->length) {
      m_send(ZuMv(m_buf));
      m_buf = m_alloc(m_headRoom);
    }
    return *this;
  }

private:
  unsigned		m_maxSize;
  unsigned		m_headRoom;
  unsigned		m_tailRoom;
  Alloc			m_alloc;
  Send			m_send;
  ZmRef<ZiIOBuf>	m_buf;
};

template <typename Alloc, typename Send>
auto txStream(
  unsigned maxSize, unsigned headRoom, unsigned tailRoom,
  Alloc alloc, Send send)
{
  return TxStream<Alloc, Send>(
    maxSize, headRoom,
    tailRoom, ZuMv(alloc), ZuMv(send));
}

} // Zi

template <typename Alloc, typename Send>
using ZiTxStream = Zi::TxStream<Alloc, Send>;

#endif /* ZiTxStream_HH */
