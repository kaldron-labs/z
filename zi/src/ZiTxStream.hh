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
//     - null rejects admission or reports lower-layer failure, not heap OOM
//   - how buffers are sent - the sendBuf_ callback
//     - bool sendBuf_(ZmRef<IOBuf> buf, bool flush)
//     - send buf (via lower-level protocol)
//     - flush is false for rollover and true for flush/destruction
//     - return false on failure
// - failed() is sticky; flush() returns false after failure
// - oversized printables retain an earlier prefix for one final flush
// - concrete implementations must flush in their destructor, while their
//   callback state is alive; this base only releases any remaining buffer

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

// ADL tag for streams accepting ownership of transmit buffers.
inline ZuFalse ZiIsTxStream_(...);
template <typename S>
using ZiIsTxStream = decltype(ZiIsTxStream_(ZuDeclVal<S *>()));

namespace Zi {

using TxErrorFn =
  ZmFn<bool(ZeException &), ZmFnHeapID<"Zi.TxErrorFn">>;

struct Flush { };
inline Flush flush() { return {}; }


// CRTP - implementation must conform to the following interface:
#if 0
struct Impl : public TxStream<Impl> {
  using Base = TxStream<Impl>;

  ZmRef<ZiIOBuf> allocBuf_(unsigned headRoom);

  bool sendBuf_(ZmRef<ZiIOBuf>, bool flush);
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
  ~TxStream() = default;

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
    bool ok = flush();
    m_maxSize = stream.m_maxSize;
    m_headRoom = stream.m_headRoom;
    m_tailRoom = stream.m_tailRoom;
    m_buf = ZuMv(stream.m_buf);
    m_failed = !ok || stream.m_failed;
    if (!ok) m_buf = {};
    stream.m_failed = false;
    return *this;
  }

  unsigned maxSize() const { return m_maxSize; }
  unsigned headRoom() const { return m_headRoom; }
  unsigned tailRoom() const { return m_tailRoom; }
  bool failed() const { return m_failed; }
  bool operator !() const { return m_failed; }
  ZuOpBool

protected:
  void fail() { m_buf = {}; m_failed = true; }

private:
  bool allocBuf() {
    m_buf = allocBuf(m_headRoom);
    return !m_failed;
  }
  bool ensureBuf() { return m_buf || allocBuf(); }
  bool sendBuf(ZmRef<ZiIOBuf> buf, bool flush) {
    if (ZuUnlikely(!buf || buf->failed() ||
	!impl()->sendBuf_(ZuMv(buf), flush))) {
      fail();
      return false;
    }
    return !m_failed;
  }
  bool nextBuf() {
    if (ZuUnlikely(!sendBuf(ZuMv(m_buf), false))) return false;
    return allocBuf();
  }

private:
  ZuSpan<uint8_t> reserve_(unsigned length_) {
    if (ZuUnlikely(length_ > m_maxSize - (m_headRoom + m_tailRoom))) {
      m_failed = true; // retain any preceding output for final flush
      return {};
    }
    if (!length_ || ZuUnlikely(!ensureBuf())) return {};
    unsigned bufLen = m_buf->length;
    unsigned total = bufLen + m_headRoom + m_tailRoom;
    ZmAssert(total <= m_maxSize);
    if (length_ > m_maxSize - total) {
      if (ZuUnlikely(!nextBuf())) return {};
      bufLen = 0;
    }
    auto ptr = m_buf->ensure(bufLen + length_);
    if (ZuUnlikely(!ptr)) { fail(); return {}; }
    return {ptr + bufLen, length_};
  }

public:
  ZuSpan<uint8_t> reserve(unsigned length) {
    if (ZuUnlikely(m_failed)) return {};
    return reserve_(length);
  }
  void commit(unsigned length) {
    if (ZuUnlikely(m_failed)) return;
    if (length) m_buf->length += length;
  }

  // Detached allocation for codecs constructing directly in output storage.
  ZmRef<ZiIOBuf> allocBuf(unsigned headRoom) {
    if (ZuUnlikely(m_failed)) return nullptr;
    auto buf = impl()->allocBuf_(headRoom);
    if (ZuUnlikely(!buf || buf->failed())) {
      fail();
      return nullptr;
    }
    return buf;
  }

  void append(const uint8_t *data, unsigned length) {
    if (!length || ZuUnlikely(m_failed)) return;
    if (ZuUnlikely(!ensureBuf())) return;
    for (;;) {
      unsigned total = m_buf->length + m_headRoom + m_tailRoom;
      ZmAssert(total <= m_maxSize);
      unsigned avail = m_maxSize - total;
      unsigned length_ = length > avail ? avail : length;
      if (length_) {
	if (ZuUnlikely(!m_buf->append(data, length_))) { fail(); return; }
	data += length_;
	length -= length_;
      }
      if (!length) return;
      if (ZuUnlikely(!nextBuf())) return;
    }
  }

private:
  template <typename U, typename R = void>
  using MatchPDelegate = ZuIfT<ZuPrint<U>::Delegate, R>;
  template <typename U, typename R = void>
  using MatchPBuffer = ZuIfT<ZuPrint<U>::Buffer, R>;

  template <typename P, typename = MatchPDelegate<P>>
  void append(P &&p) {
    if (ZuUnlikely(m_failed)) return;
    ZuPrint<P>::print(*this, ZuFwd<P>(p));
  }
  template <typename P, typename = MatchPBuffer<P>>
  void append(const P &p) {
    if (ZuUnlikely(m_failed)) return;
    unsigned length_ = ZuPrint<P>::length(p);
    auto span = reserve_(length_);
    if (!span) return;
    m_buf->length += ZuPrint<P>::print(reinterpret_cast<char *>(span.data()), length_, p);
  }

  void append(ZmRef<ZiIOBuf> buf) {
    if (ZuUnlikely(m_failed)) return;
    if (ZuUnlikely(m_buf && !sendBuf(ZuMv(m_buf), false))) return;
    m_buf = ZuMv(buf);
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
  template <typename C, typename = MatchChar<C>>
  TxStream & operator <<(C c) {
    return *this << ZuSpan{&c, 1};
  }

  template <typename R,
    typename = ZuIfT<
      (ZuTraits<R>::IsPrimitive && ZuTraits<R>::IsReal && !ZuEquiv<R, char>{}) ||
      (ZuPrint<R>::OK && !ZuPrint<R>::String)>>
  TxStream & operator <<(const R &r) {
    if constexpr (ZuTraits<R>::IsPrimitive && ZuTraits<R>::IsReal && !ZuEquiv<R, char>{}) {
      append(ZuBoxed(r));
      return *this;
    } else {
      append(r);
      return *this;
    }
  }

  TxStream &operator <<(ZmRef<ZiIOBuf> buf) {
    append(ZuMv(buf));
    return *this;
  }

  // A rejected printable may leave a valid prefix; send it once without
  // clearing failure. Rejected requests/send failures have released m_buf.
  bool flush() {
    if (m_buf && m_buf->length) sendBuf(ZuMv(m_buf), true);
    m_buf = {};
    return !m_failed;
  }
  TxStream &operator <<(Flush) {
    flush();
    return *this;
  }

  friend ZuTrue ZiIsTxStream_(TxStream *);

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

  bool prepareBuf_(ZiIOBuf *, bool flush); // prepare for sending
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
    if (!m_below.flush()) this->fail();
  }
  ~TxLayer() = default;

  bool flush() {
    bool ok = Base::flush();
    if (!m_below.flush()) { this->fail(); return false; }
    return ok;
  }

  ZmRef<ZiIOBuf> allocBuf_(unsigned headRoom) {
    if (ZuUnlikely(m_below.failed())) return nullptr;
    return m_below.allocBuf(headRoom);
  }

  bool sendBuf_(ZmRef<ZiIOBuf> buf, bool flush) {
    if (ZuUnlikely(!impl()->prepareBuf_(buf, flush))) return false;
    m_below << ZuMv(buf);
    if (flush) m_below << Flush{};
    return !m_below.failed();
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
