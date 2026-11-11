//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Bounded REST entity assembly

#ifndef ZrestInput_HH
#define ZrestInput_HH

#include <zlib/ZrestLib.hh>

#include <limits.h>
#include <stdint.h>

#include <zlib/ZuPtr.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuTraits.hh>

#include <zlib/ZiMultiplex.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmRef.hh>

#include <zlib/ZtArray.hh>

#include <zlib/ZfJSON.hh>
#include <zlib/ZfURI.hh>

#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiRxStream.hh>

namespace Zrest {

inline Zi::IOBufAllocator *inputBufAllocator(Zi::IOBufType::T type)
{
  if (type == Zi::IOBufType::App) {
    static ZiMultiplex mx;
    static auto allocator = mx.allocator<Zi::IOBufType::App,
      ZiIOBuf, ZiIOBuf_DefltSize, ZiIOBuf_DefltAlign, ZuStringT<"Zrest.Input">>();
    return allocator;
  }
  if (type == Zi::IOBufType::RxTCP) {
    static ZiMultiplex mx;
    static auto allocator = mx.allocator<Zi::IOBufType::RxTCP,
      ZiIOBuf, ZiIOBuf_DefltSize, ZiIOBuf_DefltAlign, ZuStringT<"Zrest.Input">>();
    return allocator;
  }
  {
    static ZiMultiplex mx;
    static auto allocator = mx.allocator<Zi::IOBufType::RxTLS,
      ZiIOBuf, ZiIOBuf_DefltSize, ZiIOBuf_DefltAlign, ZuStringT<"Zrest.Input">>();
    return allocator;
  }
}

inline Zi::IOBufAllocator *inputNodeAllocator(Zi::IOBufType::T type)
{
  if (type == Zi::IOBufType::App) {
    static ZiMultiplex mx;
    static auto allocator = mx.allocator<Zi::IOBufType::App,
      ZiRxQueue::Node, ZiIOBuf_DefltSize, ZiIOBuf_DefltAlign,
      ZuStringT<"Zrest.Input">>();
    return allocator;
  }
  if (type == Zi::IOBufType::RxTCP) {
    static ZiMultiplex mx;
    static auto allocator = mx.allocator<Zi::IOBufType::RxTCP,
      ZiRxQueue::Node, ZiIOBuf_DefltSize, ZiIOBuf_DefltAlign,
      ZuStringT<"Zrest.Input">>();
    return allocator;
  }
  {
    static ZiMultiplex mx;
    static auto allocator = mx.allocator<Zi::IOBufType::RxTLS,
      ZiRxQueue::Node, ZiIOBuf_DefltSize, ZiIOBuf_DefltAlign,
      ZuStringT<"Zrest.Input">>();
    return allocator;
  }
}

class Input {
public:
  bool begin(uint64_t length, uint64_t max,
      Zi::IOBufType::T type = Zi::IOBufType::App) {
    m_type = type;
    reset();
    if (!max || length > max) return false;
    if (max > UINT_MAX) max = UINT_MAX;
    if (length > max) return false;
    m_max = unsigned(max);
    return true;
  }

  // Borrowed-memory ingress acquires ownership once at this boundary.
  bool feed(ZuSpan<uint8_t> span) {
    if (!fits_(span.length())) { reset(); return false; }
    if (!span.length()) return true;
    buffer();
    if (m_body->refCount() > 1) {
      auto shared = ZuMv(m_body);
      m_body = inputBufAllocator(m_type)->allocBuf();
      m_body->ensure(m_length + span.length());
      m_body->append(shared->cspan());
    }
    if (!m_body->append(span)) { reset(); return false; }
    m_length += span.length();
    return true;
  }

  bool feed(ZmRef<ZiIOBuf> buf) {
    if (!buf || !fits_(buf->length)) { reset(); return false; }
    if (!m_body) {
      m_length = buf->length;
      m_body = ZuMv(buf);
      return true;
    }
    return feed(buf->span());
  }

  template <typename Rx>
  bool body(Rx &rx) {
    if (!fits_(rx.length())) { reset(); return false; }
    while (rx.length()) {
      ZmRef<ZiIOBuf> buf;
      auto n = rx.extract(
        [](ZuSpan<uint8_t> span) -> int64_t { return span.length(); },
        [this]() -> ZmRef<ZiRxQueue::Node> {
	  return inputNodeAllocator(m_type)->allocBuf();
        }, buf);
      if (n <= 0 || !feed(ZuMv(buf))) { reset(); return false; }
    }
    return true;
  }

  const ZmRef<ZiIOBuf> &buffer() {
    if (!m_body && m_max)
      m_body = inputBufAllocator(m_type)->allocBuf();
    return m_body;
  }
  ZmRef<ZiIOBuf> take() {
    buffer();
    auto result = ZuMv(m_body);
    reset();
    return result;
  }
  void reset() { m_body = nullptr; m_max = m_length = 0; }

private:
  bool fits_(uint64_t length) const {
    return m_max && length <= m_max - m_length;
  }

  ZmRef<ZiIOBuf> m_body;
  Zi::IOBufType::T m_type = Zi::IOBufType::App;
  unsigned m_max = 0;
  unsigned m_length = 0;
};

inline bool jsonTail(ZuSpan<uint8_t> span, int offset) {
  if (offset <= 0 || unsigned(offset) > span.length()) return false;
  for (unsigned i = unsigned(offset); i < span.length(); ++i)
    switch (span[i]) {
      case ' ': case '\t': case '\r': case '\n': break;
      default: return false;
    }
  return true;
}

struct BorrowedData : public ZmNHeap<BorrowedData>, public ZmObject {
friend inline ZuStringT<"Zrest.Borrowed"> ZmNHeapID(BorrowedData *);

  explicit BorrowedData(Zi::IOBufType::T type) : m_type{type} { }

  ZuPtr<ZfJSON::AnyNode> json;
  ZuPtr<ZfURI::AnyNode> query;
  ZuPtr<ZfURI::AnyNode> body;
  ZtArray<ZmRef<ZiIOBuf>,
    ZtArrayHeapID<"Zrest.Borrowed.Buffers">> buffers;

  template <typename Span>
  bool retain(Span input, Span &output) {
    if (!input.length()) { output = input; return true; }
    ZmRef<ZiIOBuf> buffer = inputBufAllocator(m_type)->allocBuf();
    if (!buffer->append(input)) return false;
    output = Span{buffer->data(), buffer->length};
    buffers.push(ZuMv(buffer));
    return true;
  }

private:
  Zi::IOBufType::T m_type;
};

} // Zrest

#endif /* ZrestInput_HH */
