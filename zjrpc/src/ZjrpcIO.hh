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
#include <zlib/ZmSingleton.hh>
#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiRxStream.hh>
#include <zlib/Zjrpc.hh>

namespace Zjrpc {

using BatchBuf = Zi::IOBufBuiltin<
  ZiIOBuf,
  ZiIOBuf_DefltSize,
  ZiIOBuf_DefltAlign,
  ZuStringT<"Zjrpc.Batch">,
  ZiIOBuf_DefltMaxSize>;

using InputBuf = Zi::IOBufBuiltin<
  ZiIOBuf,
  ZiIOBuf_DefltSize,
  ZiIOBuf_DefltAlign,
  ZuStringT<"Zjrpc.Input">,
  ZiIOBuf_DefltMaxSize>;

template <typename Out>
class Output {
public:
  Output(Out &out_, uint64_t max_) : m_out{out_}, m_max{max_} { }

  void fail() { m_overflow = true; }
  unsigned headRoom() const { return m_out.headRoom(); }
  unsigned tailRoom() const { return m_out.tailRoom(); }
  template <typename O = Out>
  auto allocBuf(unsigned headRoom) -> decltype(ZuDeclVal<O &>().allocBuf(headRoom)) {
    return m_out.allocBuf(headRoom);
  }
  Output &operator <<(ZmRef<ZiIOBuf> buf) {
    if (failed()) return *this;
    if (!buf || buf->length > m_max - m_produced) { fail(); return *this; }
    unsigned length = buf->length;
    m_out << ZuMv(buf);
    if (m_out.failed()) { fail(); return *this; }
    m_produced += length;
    return *this;
  }

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
    m_hint = length <= max ? unsigned(length) : 0;
    m_active = length <= max;
    return m_active;
  }
  bool feed(ZuSpan<uint8_t> span) {
    if (!m_active) return false;
    if (!m_body) m_body = new InputBuf{ZmSingleton<typename InputBuf::Allocator>::instance()};
    if (m_body->refCount() > 1) {
      auto held = ZuMv(m_body);
      m_body = new InputBuf{ZmSingleton<typename InputBuf::Allocator>::instance()};
      if (!m_body->append(held->cspan())) { reset(); return false; }
    }
    if (m_hint && !m_body->ensure(m_hint)) { reset(); return false; }
    unsigned length = m_body->length;
    if (length > m_max || span.length() > m_max - length || !m_body->append(span)) {
      m_body = nullptr;
      m_active = false;
      return false;
    }
    return true;
  }
  bool feed(ZmRef<ZiIOBuf> buf) {
    if (!m_active || !buf || buf->length > m_max - (m_body ? m_body->length : 0)) {
      reset(); return false;
    }
    if (!m_body) { m_body = ZuMv(buf); return true; }
    return feed(buf->span());
  }
  template <typename Rx>
  bool body(Rx &rx) {
    auto used = m_body ? m_body->length : 0;
    if (!m_active || rx.length() > m_max - used) { reset(); return false; }
    while (rx.length()) {
      ZmRef<ZiIOBuf> buf;
      auto n = rx.extract(
        [](ZuSpan<uint8_t> span) -> int64_t { return span.length(); },
        []() {
          using Buf = Zi::IOBufBuiltin<
            ZiRxQueue::Node,
            ZiIOBuf_DefltSize,
            ZiIOBuf_DefltAlign,
            ZuStringT<"Zjrpc.Input">,
            ZiIOBuf_DefltMaxSize>;
          return ZmRef<ZiRxQueue::Node>{new Buf{
            ZmSingleton<typename Buf::Allocator>::instance()}};
        }, buf);
      if (n <= 0 || !feed(ZuMv(buf))) { reset(); return false; }
    }
    return true;
  }
  const ZmRef<ZiIOBuf> &buffer() const { return m_body; }
  ZmRef<ZiIOBuf> take() { return ZuMv(m_body); }
  void reset() { m_body = nullptr; m_active = false; }

private:
  ZmRef<ZiIOBuf> m_body;
  unsigned m_max = 0;
  unsigned m_hint = 0;
  bool m_active = false;
};

class BufOutput {
public:
  BufOutput(ZiIOBuf &buf_, uint64_t max_, unsigned tailRoom = 0) :
    m_buf{buf_}, m_max{max_}, m_tailRoom{tailRoom} { }

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
    if (!reserve_(offset + length) || !m_buf.append(value)) m_overflow = true;
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
      auto data = reserve_(offset + length);
      if (ZuUnlikely(!data)) {
	m_overflow = true;
	return *this;
      }
      ZuSpan<char> span{data + offset, length};
      m_buf.length += ZuPrint<P>::print(span.data(), length, value);
      return *this;
    }
  }

  uint8_t *reserve_(unsigned length) {
    if (uint64_t(length) + m_tailRoom > UINT32_MAX) return nullptr;
    return m_buf.ensure(length + m_tailRoom);
  }
  ZiIOBuf	&m_buf;
  uint64_t	m_max;
  unsigned	m_tailRoom;
  bool		m_overflow = false;
};

struct CountOutput {
  uint64_t length = 0;

  CountOutput &operator <<(ZuBSpan value) {
    length += value.length();
    return *this;
  }

  template <typename P,
    typename = ZuIfT<
      (ZuTraits<P>::IsPrimitive && ZuTraits<P>::IsReal) ||
      (ZuPrint<P>::OK && !ZuPrint<P>::String)>>
  CountOutput &operator <<(const P &value) {
    if constexpr (ZuEquiv<P, char>{}) ++length;
    else if constexpr (ZuTraits<P>::IsPrimitive && ZuTraits<P>::IsReal)
      *this << ZuBoxed(value);
    else if constexpr (ZuPrint<P>::Delegate) ZuPrint<P>::print(*this, value);
    else length += ZuPrint<P>::length(value);
    return *this;
  }
  bool failed() const { return false; }
  bool flush() { return true; }
};

// Network output reserves its unpublished final allocation. Ordinary file/test
// sinks serialize directly into the storage already owned by that sink.
template <typename S, typename Write>
auto writeOwned(S &out, unsigned max, Write &&write, int) ->
  decltype(out.allocBuf(out.headRoom()), void())
{
  auto buf = out.allocBuf(out.headRoom());
  if (!buf) { out.fail(); return; }
  BufOutput wire{*buf, max, out.tailRoom()};
  write(wire);
  if (!wire) { out.fail(); return; }
  out << ZuMv(buf);
}
template <typename S, typename Write>
void writeOwned(S &out, unsigned, Write &&write, long) { write(out); }

} // Zjrpc

#endif /* ZjrpcIO_HH */
