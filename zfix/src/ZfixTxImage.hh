//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZfixTxImage_HH
#define ZfixTxImage_HH

#ifndef ZfixLib_HH
#include <zlib/ZfixLib.hh>
#endif

#include <zlib/ZiRxStream.hh>

namespace Zfix {

// One transmission owns one complete wire buffer. Views carry positions in
// that buffer; history/body storage is never a physical transmit descriptor.
struct TxImage {
  ZmRef<ZiIOBuf> metadata;
  unsigned bytes = 0, origin = 0;

  TxImage() = default;
  explicit TxImage(ZmRef<ZiIOBuf> buf) :
    metadata{ZuMv(buf)}, bytes{metadata ? metadata->length : 0},
    origin{metadata ? metadata->skip : 0} { }
  explicit TxImage(Zi::RxRange range) : metadata{ZuMv(range.buf)},
    bytes{range.length}, origin{range.offset} { }

  explicit operator bool() const { return metadata && bytes; }
  bool operator !() const { return !bool(*this); }
  unsigned length() const { return bytes; }
  ZuCSpan spanAt(unsigned offset, unsigned length) const {
    if (!metadata || offset > bytes || length > bytes - offset) return {};
    return {metadata->data_() + origin + offset, length};
  }
  template <typename Fn> void each(Fn fn) const {
    if (metadata) fn(spanAt(0, bytes));
  }
  template <typename Fn> void each(unsigned offset, unsigned length, Fn fn) const {
    if (metadata && offset <= bytes && length <= bytes - offset)
      fn(spanAt(offset, length));
  }
  struct Cursor {
    const TxImage *image;
    unsigned offset, remaining;
    bool next(ZuCSpan &span) {
      if (!remaining || !image->metadata || offset > image->bytes ||
	  remaining > image->bytes - offset) return false;
      span = image->spanAt(offset, remaining);
      remaining = 0;
      return true;
    }
  };
  // A subview is consumed synchronously by TLS encryption or signing; the
  // enclosing queued image retains its complete original wire buffer.
  struct View {
    const TxImage	*image;
    unsigned		offset, bytes;
    unsigned length() const { return bytes; }
    Cursor cursor() const { return {image, offset, bytes}; }
    template <typename Fn> void each(Fn fn) const { image->each(offset, bytes, fn); }
    ZuCSpan spanAt(unsigned pos, unsigned length) const {
      return pos <= bytes && length <= bytes - pos ?
	image->spanAt(offset + pos, length) : ZuCSpan{};
    }
  };
  View view(unsigned offset, unsigned length) const { return {this, offset, length}; }

  uint64_t memory() const { return metadata ? metadata->memory() : 0; }
};
} // Zfix

#endif /* ZfixTxImage_HH */
