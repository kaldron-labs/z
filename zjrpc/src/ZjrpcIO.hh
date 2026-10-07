//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Bounded JSON-RPC output into pooled I/O buffers

#ifndef ZjrpcIO_HH
#define ZjrpcIO_HH

#ifndef ZjrpcLib_HH
#include <zlib/ZjrpcLib.hh>
#endif

#include <zlib/ZuDerive.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZuTraits.hh>
#include <zlib/ZiIOBuf.hh>
#include <zlib/Zjrpc.hh>

namespace Zjrpc {

using BatchBuf = ZiIOBufAlloc<ZiIOBuf_DefltSize,
  ZiIOBuf_DefltMaxSize, "Zjrpc.Batch">;

ZuDerive(InputBuf, (ZiIOBufAlloc<
  ZiIOBuf_DefltSize, ZiIOBuf_DefltMaxSize, "Zjrpc.Input">));

template <typename Out>
class Output {
public:
  Output(Out &out_, uint64_t max_) : m_out{out_}, m_max{max_} { }

  bool failed() const { return m_overflow || m_out.failed(); }
  explicit operator bool() const { return !failed(); }
  uint64_t produced() const { return m_produced; }

  bool flush() {
    if (!m_out.flush()) m_overflow = true;
    return !failed();
  }

  Output &operator <<(ZuBSpan value) {
    if (failed()) return *this;
    unsigned length = value.length();
    if (ZuUnlikely(length > m_max - m_produced)) {
      m_overflow = true;
      return *this;
    }
    m_out << value;
    if (m_out.failed()) { m_overflow = true; return *this; }
    m_produced += length;
    return *this;
  }

  template <typename C, typename = ZuSame<C, char>>
  Output & operator <<(C value) {
    return *this << ZuSpan{&value, 1};
  }

  template <typename R,
    typename = ZuIfT<
      (ZuTraits<R>::IsPrimitive && ZuTraits<R>::IsReal && !ZuIsSame<R, char>{}) ||
      (ZuPrint<R>::OK && !ZuPrint<R>::String)>>
  Output &
  operator <<(const R &value) {
    if constexpr (ZuTraits<R>::IsPrimitive && ZuTraits<R>::IsReal && !ZuIsSame<R, char>{}) {
      return append_(ZuBoxed(value));
    } else {
      return append_(value);
    }
  }

private:

  template <typename P,
    typename = ZuIfT<(ZuPrint<P>::Delegate) || (ZuPrint<P>::Buffer)>>
  Output &append_(const P &value) {
    if constexpr (ZuPrint<P>::Delegate) {
      if (!failed()) ZuPrint<P>::print(*this, value);
      return *this;
    } else {
      if (failed()) return *this;
      uint64_t length = ZuPrint<P>::length(value);
      if (ZuUnlikely(length > m_max - m_produced)) {
	m_overflow = true;
	return *this;
      }
      m_out << value;
      if (m_out.failed()) { m_overflow = true; return *this; }
      m_produced += length;
      return *this;
    }
  }

  Out		&m_out;
  uint64_t	m_max;
  uint64_t	m_produced = 0;
  bool		m_overflow = false;
};


// Bounded assembly of one transport message, independent of RPC routing/policy.
class Input {
public:
  bool begin(uint64_t length, unsigned max) {
    m_body = nullptr;
    m_max = max;
    if (length > max) return false;
    m_body = new InputBuf{};
    if (length && !m_body->alloc(length)) { m_body = nullptr; return false; }
    return true;
  }
  bool feed(ZuSpan<uint8_t> span) {
    if (!m_body) return false;
    unsigned length = m_body->length;
    if (length > m_max || span.length() > m_max - length || !m_body->append(span)) {
      m_body = nullptr;
      return false;
    }
    return true;
  }
  template <typename Rx>
  bool body(Rx &rx) {
    rx.consume([](ZuSpan<uint8_t> span) -> int64_t { return span.length(); },
      [this](ZuSpan<uint8_t> span) { (void)feed(span); });
    return bool(m_body);
  }
  const ZmRef<ZiIOBuf> &buffer() const { return m_body; }
  ZmRef<ZiIOBuf> take() { return ZuMv(m_body); }
  void reset() { m_body = nullptr; }

private:
  ZmRef<ZiIOBuf> m_body;
  unsigned m_max = 0;
};

class BufOutput {
public:
  BufOutput(ZiIOBuf &buf_, uint64_t max_) : m_buf{buf_}, m_max{max_} { }

  bool failed() const { return m_overflow || m_buf.failed(); }
  explicit operator bool() const { return !failed(); }

  BufOutput &operator <<(ZuBSpan value) {
    if (failed()) return *this;
    unsigned length = value.length();
    unsigned offset = m_buf.length;
    if (ZuUnlikely(offset > m_max || length > m_max - offset)) {
      m_overflow = true;
      return *this;
    }
    if (!m_buf.append(value)) m_overflow = true;
    return *this;
  }

  template <typename C, typename = ZuSame<C, char>>
  BufOutput & operator <<(C value) {
    return *this << ZuSpan{&value, 1};
  }

  template <typename R,
    typename = ZuIfT<
      (ZuTraits<R>::IsPrimitive && ZuTraits<R>::IsReal && !ZuIsSame<R, char>{}) ||
      (ZuPrint<R>::OK && !ZuPrint<R>::String)>>
  BufOutput &
  operator <<(const R &value) {
    if constexpr (ZuTraits<R>::IsPrimitive && ZuTraits<R>::IsReal && !ZuIsSame<R, char>{}) {
      return append_(ZuBoxed(value));
    } else {
      return append_(value);
    }
  }

private:

  template <typename P,
    typename = ZuIfT<(ZuPrint<P>::Delegate) || (ZuPrint<P>::Buffer)>>
  BufOutput &append_(const P &value) {
    if constexpr (ZuPrint<P>::Delegate) {
      if (!failed()) ZuPrint<P>::print(*this, value);
      return *this;
    } else {
      if (failed()) return *this;
      unsigned length = ZuPrint<P>::length(value);
      unsigned offset = m_buf.length;
      if (ZuUnlikely(offset > m_max || length > m_max - offset)) {
	m_overflow = true;
	return *this;
      }
      auto data = m_buf.ensure(offset + length);
      if (ZuUnlikely(!data)) {
	m_overflow = true;
	return *this;
      }
      ZuSpan<char> span{data + offset, length};
      m_buf.length += ZuPrint<P>::print(span.data(), length, value);
      return *this;
    }
  }

  ZiIOBuf	&m_buf;
  uint64_t	m_max;
  bool		m_overflow = false;
};

} // Zjrpc

#endif /* ZjrpcIO_HH */
