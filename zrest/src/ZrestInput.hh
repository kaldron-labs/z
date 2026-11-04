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

#include <zlib/ZuDerive.hh>
#include <zlib/ZuPtr.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuTraits.hh>

#include <zlib/ZmHeap.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmRef.hh>

#include <zlib/ZtArray.hh>

#include <zlib/ZfJSON.hh>
#include <zlib/ZfURI.hh>

#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiRxStream.hh>

namespace Zrest {

ZuDerive(InputBuf, (Zi::IOBufAlloc<ZiIOBuf,
  ZiIOBuf_DefltSize, ZiIOBuf_DefltMaxSize, ZuStringT<"Zrest.Input">>));

struct InputSlice : public ZmNHeap<InputSlice>, public ZiIOBuf {
  ZmRef<ZiIOBuf> backing;

  friend inline ZuStringT<"Zrest.InputSlice"> ZmNHeapID(InputSlice *);
  InputSlice(Zi::RxRange range) :
    ZiIOBuf{range.buf->data_() + range.offset, range.length, nullptr, range.length},
    backing{ZuMv(range.buf)} { }
  void releaseView_() override { backing = nullptr; }
  uint64_t memory() const override {
    return ZmHeapAllocSize<sizeof(InputSlice)>{} + storageMemory_() +
      (backing ? backing->memory() : 0);
  }
};

class Input {
public:
  bool begin(uint64_t length, uint64_t max) {
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
      m_body = new InputBuf{nullptr, 0};
      m_body->ensure(m_length + span.length());
      m_body->append(shared->cspan());
    }
    if (!m_body->append(span)) { reset(); return false; }
    m_length += span.length();
    return true;
  }

  template <typename Rx>
  bool body(Rx &rx) {
    auto length = rx.length();
    if (!fits_(length)) { reset(); return false; }
    rx.eachRange(length, [this](Zi::RxRange range) -> int64_t {
      auto n = range.length;
      retain_(ZuMv(range));
      return n;
    });
    rx.advance(length);
    return true;
  }

  const ZmRef<ZiIOBuf> &buffer() {
    if (m_body || !m_max) return m_body;
    if (m_ranges.length() == 1) {
      auto range = ZuMv(m_ranges[0]);
      if (range.offset == range.buf->skip && range.length == range.buf->length)
	m_body = ZuMv(range.buf);
      else m_body = new InputSlice{ZuMv(range)};
    } else {
      m_body = new InputBuf{nullptr, 0};
      m_body->ensure(m_length);
      for (const auto &range : m_ranges) m_body->append(range.cspan());
    }
    m_ranges.length(0);
    return m_body;
  }
  ZmRef<ZiIOBuf> take() {
    buffer();
    auto result = ZuMv(m_body);
    reset();
    return result;
  }
  void reset() { m_body = nullptr; m_ranges.length(0); m_max = m_length = 0; }

private:
  bool fits_(uint64_t length) const {
    return m_max && length <= m_max - m_length;
  }
  void retain_(Zi::RxRange range) {
    if (m_body) {
      auto n = m_body->length;
      m_ranges.push(Zi::RxRange{ZuMv(m_body), 0, n});
    }
    auto n = m_ranges.length();
    if (n && m_ranges[n - 1].buf == range.buf &&
	m_ranges[n - 1].offset + m_ranges[n - 1].length == range.offset)
      m_ranges[n - 1].length += range.length;
    else m_ranges.push(range);
    m_length += range.length;
  }

  ZmRef<ZiIOBuf> m_body;
  ZtArray<Zi::RxRange, ZtArrayHeapID<"Zrest.Input.Ranges">> m_ranges;
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

  ZuPtr<ZfJSON::AnyNode> json;
  ZuPtr<ZfURI::AnyNode> query;
  ZuPtr<ZfURI::AnyNode> body;
  ZtArray<ZmRef<ZiIOBuf>,
    ZtArrayHeapID<"Zrest.Borrowed.Buffers">> buffers;

  template <typename Span>
  bool retain(Span input, Span &output) {
    if (!input.length()) { output = input; return true; }
    ZmRef<ZiIOBuf> buffer = new InputBuf{nullptr, 0};
    if (!buffer->append(input)) return false;
    output = Span{buffer->data(), buffer->length};
    buffers.push(ZuMv(buffer));
    return true;
  }
};

} // Zrest

#endif /* ZrestInput_HH */
