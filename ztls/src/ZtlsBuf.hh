//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// TLS record buffers and packet reservations

#ifndef ZtlsBuf_HH
#define ZtlsBuf_HH

#include <zlib/ZtlsEngine.hh>
#include <zlib/ZiIOBuf.hh>

namespace Ztls {

using TxPacket = ZiIOBuf;

// Holds provisional plaintext privately until the receive operation authenticates it.
// The buffer does not invoke application code; the owner takes output after return.
class PlaintextSink {
public:
  PlaintextSink() = default;
  PlaintextSink(Zi::IOBufAllocator *allocator, void *owner)
  : m_allocator{allocator}, m_owner{owner} { }
  PlaintextSink(const PlaintextSink &) = delete;
  PlaintextSink &operator =(const PlaintextSink &) = delete;
  ~PlaintextSink() { clear(); }

  void reset(Zi::IOBufAllocator *allocator, void *owner) {
    clear(); m_allocator = allocator; m_owner = owner;
  }
  void clear() {
    discard();
    if (m_ready) {
      ZuClear(m_ready->data(), m_ready->length);
      m_ready = nullptr;
    }
  }
  ZmRef<ZiIOBuf> take() { return ZuMv(m_ready); }

  int reserve(size_t size, void **base, size_t *capacity) {
    if (m_private || m_ready || !m_allocator ||
	m_allocator->type != Zi::IOBufType::RxTLS || size > Ztls_RECEIVE_RECORD_CAPACITY)
      return Ztls_ERROR_LIBRARY;
    ZmRef<ZiIOBuf> buf;
    try { buf = m_allocator->allocBuf(m_owner); }
    catch (...) { return Ztls_ERROR_NO_MEMORY; }
    if (!buf) return Ztls_ERROR_NO_MEMORY;
    if (buf->registered() || buf->skip > buf->size ||
	m_allocator->tailRoom > buf->size - buf->skip ||
	buf->size - buf->skip - m_allocator->tailRoom < Ztls_RECEIVE_RECORD_CAPACITY)
      return Ztls_ERROR_LIBRARY;
    m_extent = size;
    m_private = ZuMv(buf);
    *base = m_private->data();
    *capacity = size;
    return 0;
  }
  int commit(size_t length) {
    if (!m_private || m_ready || length > m_extent ||
	length > Ztls_RECEIVE_APPLICATION_CAPACITY) {
      discard(); return Ztls_ERROR_LIBRARY;
    }
    // Clear authenticated padding/content type as well as discarded provisional data.
    ZuClear(m_private->data() + length, m_extent - length);
    m_private->length = uint32_t(length);
    m_ready = ZuMv(m_private);
    m_extent = 0;
    return 0;
  }
  void discard() {
    if (m_private) {
      ZuClear(m_private->data(), m_extent);
      m_private = nullptr;
    }
    m_extent = 0;
  }

private:
  Zi::IOBufAllocator	*m_allocator = nullptr;
  void			*m_owner = nullptr;
  ZmRef<ZiIOBuf>		m_private, m_ready;
  size_t			m_extent = 0;
};

// Owns one unpublished record reservation. Packet backing is never resized.
class RecordSink {
public:
  using PublishFn = int (*)(void *, ZmRef<ZiIOBuf>);
  using ReadyFn = void (*)(void *, ZmRef<ZiIOBuf>);

  RecordSink(
    Zi::IOBufAllocator *allocator, void *owner, uint32_t mps,
    void *ctx, PublishFn publish, ReadyFn ready = nullptr)
  : m_allocator{allocator}, m_owner{owner}, m_mps{mps},
    m_ctx{ctx}, m_publish{publish}, m_ready{ready} { }

  ~RecordSink() { discard(); }

  int reserve(size_t size) try {
    if (m_active || !m_mps) return Ztls_ERROR_LIBRARY;
    m_active = true;
    m_size = size;
    size_t remaining = size;
    while (remaining) {
      auto buf = m_allocator->allocBuf(m_owner, 0, m_mps);
      if (!buf) { discard(); return Ztls_ERROR_NO_MEMORY; }
      if (buf->skip > buf->size ||
	m_allocator->tailRoom >= buf->size - buf->skip) {
	discard(); return Ztls_ERROR_LIBRARY;
      }
      auto capacity = buf->size - buf->skip - m_allocator->tailRoom;
      if (capacity > remaining) capacity = unsigned(remaining);
      remaining -= capacity;
      m_packets.pushNode(ZuMv(buf));
    }
    m_current = m_packets.headPtr();
    return 0;
  } catch (...) {
    discard();
    return Ztls_ERROR_NO_MEMORY;
  }

  int next(void **base, size_t *capacity) {
    if (!m_active || !m_current || m_committed == m_size)
      return Ztls_ERROR_LIBRARY;
    auto buf = m_current;
    auto room = buf->size - buf->skip - m_allocator->tailRoom - buf->length;
    if (!room) {
      m_written.pushNode(m_packets.shift());
      buf = m_current = m_packets.headPtr();
      if (!buf) return Ztls_ERROR_LIBRARY;
      room = buf->size - buf->skip - m_allocator->tailRoom;
    }
    if (room > m_size - m_committed) room = unsigned(m_size - m_committed);
    *base = static_cast<ZiIOBuf *>(buf)->data() + buf->length;
    *capacity = m_exposed = room;
    return room ? 0 : Ztls_ERROR_LIBRARY;
  }

  int commit(size_t length) {
    if (!m_active || !m_current || !m_exposed || length > m_exposed)
      return Ztls_ERROR_LIBRARY;
    m_current->length += uint32_t(length);
    m_committed += length;
    m_exposed = 0;
    return 0;
  }

  int finish() try {
    if (!m_active || m_committed != m_size) return Ztls_ERROR_LIBRARY;
    m_written += ZuMv(m_packets);
    if (m_ready) m_ready(m_ctx, ZmRef<ZiIOBuf>{m_written.tailPtr()});
    while (auto buf = m_written.shift()) {
      int n = m_publish(m_ctx, ZuMv(buf));
      if (n) { discard(); return n; }
    }
    discard();
    return 0;
  } catch (...) {
    discard();
    return Ztls_ERROR_LIBRARY;
  }

  void discard() {
    m_packets.clean();
    m_written.clean();
    m_current = nullptr;
    m_size = m_committed = m_exposed = 0;
    m_active = false;
  }

private:
  Zi::IOBufAllocator *m_allocator;
  void		*m_owner;
  uint32_t	m_mps;
  void		*m_ctx;
  PublishFn	m_publish;
  ReadyFn	m_ready;
  Zi::IOQueue	m_packets;
  Zi::IOQueue	m_written;
  using Node = Zi::IOQueue::Node;
  Node		*m_current = nullptr;
  size_t	m_size = 0;
  size_t	m_committed = 0;
  size_t	m_exposed = 0;
  bool		m_active = false;
};

} // namespace Ztls

#endif /* ZtlsBuf_HH */
