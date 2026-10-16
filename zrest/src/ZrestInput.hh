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

namespace Zrest {

ZuDerive(InputBuf, (ZiIOBufAlloc<
  ZiIOBuf_DefltSize, ZiIOBuf_DefltMaxSize, "Zrest.Input">));

class Input {
public:
  bool begin(uint64_t length, uint64_t max) {
    reset();
    if (!max || length > max) return false;
    if (max > UINT_MAX) max = UINT_MAX;
    if (length > max) return false;
    m_max = unsigned(max);
    m_body = new InputBuf{};
    if (length && !m_body->alloc(unsigned(length))) {
      reset();
      return false;
    }
    return bool(m_body);
  }

  bool feed(ZuSpan<uint8_t> span) {
    if (!m_body) return false;
    unsigned length = m_body->length;
    if (length > m_max || span.length() > m_max - length ||
	!m_body->append(span)) {
      reset();
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
  void reset() { m_body = nullptr; m_max = 0; }

private:
  ZmRef<ZiIOBuf> m_body;
  unsigned m_max = 0;
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
    ZmRef<ZiIOBuf> buffer = new InputBuf{};
    if (!buffer->append(input)) return false;
    output = Span{buffer->data(), buffer->length};
    buffers.push(ZuMv(buffer));
    return true;
  }
};

} // Zrest

#endif /* ZrestInput_HH */
