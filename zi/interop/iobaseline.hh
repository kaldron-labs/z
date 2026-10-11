//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Frozen custom Zfb::IOBuilder baseline, isolated from production.
// Keep allocation, growth, alignment and ownership behavior unchanged.

#ifndef IOBaseline_HH
#define IOBaseline_HH

#include <flatbuffers/flatbuffers.h>
#include <zlib/ZmAssert.hh>
#include <zlib/ZiIOBuf.hh>

namespace Bench {

// IOBuilder customizes FlatBufferBuilder with an allocator that
// builds directly into a detachable IOBuf for transmission/persistence
// - Note: IOBuilder is immovable - use a ZuPtr<> if needed
class IOBuilder : public flatbuffers::Allocator, public flatbuffers::FlatBufferBuilder {
public:
  using Builder = flatbuffers::FlatBufferBuilder;
  enum { Align = 8 };

  IOBuilder(ZmRef<ZiIOBuf> buf) :
    Builder{(buf->size & ~(Align - 1)) - buf->skip,
      this, false, Align},
    m_buf{ZuMv(buf)} {
    ZmAssert(!(m_buf->skip & (Align - 1)));
    m_buf->length = 0;
    m_buf->size &= ~(Align - 1);
  }

  // Release the builder arena while its owning IOBuf is still alive,
  // including when serialization throws before Finish()/buf().
  ~IOBuilder() { Reset(); }

  IOBuilder(IOBuilder &&) = delete;
  IOBuilder &operator =(IOBuilder &&) = delete;

  // attach buffer to builder
  void buf(ZmRef<ZiIOBuf> buf) {
    ZmAssert(!(buf->skip & (Align - 1)));
    buf->length = 0;
    buf->size &= ~(Align - 1);
    Builder::operator =(
      Builder{buf->size - buf->skip, this, false, Align});
    m_buf = ZuMv(buf);
  }

  // detach buffer from builder
  ZmRef<ZiIOBuf> buf() {
    if (ZuUnlikely(!m_buf)) return nullptr;
    auto buf = ZuMv(m_buf);
    size_t size, skip;
    ReleaseRaw(size, skip);
    buf->skip += skip;
    buf->length = size - skip;
    Clear();
    return buf;
  }

  // read buffer without detaching
  const ZiIOBuf *cspan() const { return m_buf.ptr(); }

protected:
  uint8_t *allocate(size_t size) {
    ZmAssert(m_buf);
    if (ZuUnlikely(size > UINT32_MAX - m_buf->skip)) return nullptr;
    m_buf->alloc(unsigned(size) + m_buf->skip);
    return m_buf->data();
  }

  void deallocate(uint8_t *ptr, size_t size) {
    if (m_buf) m_buf->free(ptr - m_buf->skip);
  }

  // override ZiIOBuf's default grow() with a pass-through because flatbuffers
  // has it's own buffer growth algorithm in vector_downward::reallocate()
private:
  static unsigned grow(unsigned, unsigned n) { return n; }
protected:
  uint8_t *reallocate_downward(
      uint8_t *old_p, size_t old_size, size_t new_size,
      size_t in_use_back, size_t in_use_front) {
    ZmAssert(m_buf);
    if (ZuUnlikely(new_size > UINT32_MAX - m_buf->skip)) return nullptr;
    return m_buf->template realloc<grow>(
	old_size, new_size, in_use_front, in_use_back);
  }

private:
  ZmRef<ZiIOBuf>	m_buf;
};

} // Bench

#endif
