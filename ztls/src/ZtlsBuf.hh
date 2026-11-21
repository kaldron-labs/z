//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// TLS record buffers and packet reservations

#ifndef ZtlsBuf_HH
#define ZtlsBuf_HH

#ifndef ZtlsLib_HH
#include <zlib/ZtlsLib.hh>
#endif

#include <zlib/ZiIOBuf.hh>

#include <zlib/ZtlsEngine.hh>

namespace Ztls {

using TxPacket = ZiIOBuf;

// Holds provisional plaintext privately until the receive operation authenticates it.
// The buffer does not invoke application code; the owner takes output after return.
class PlaintextSink {
public:
  PlaintextSink() = default;
  PlaintextSink(Zi::IOBufAllocator *allocator, void *owner)
  :
    m_allocator{allocator}, m_owner{owner}
  { }
  PlaintextSink(const PlaintextSink &) = delete;
  PlaintextSink &operator=(const PlaintextSink &) = delete;
  ~PlaintextSink() { clear(); }

  void reset(Zi::IOBufAllocator *allocator, void *owner)
  {
    clear();
    m_allocator = allocator;
    m_owner = owner;
  }
  void clear()
  {
    discard();
    if (m_ready) {
      ZuClear(m_ready->data(), m_ready->length);
      m_ready = nullptr;
    }
  }
  ZmRef<ZiIOBuf> take() { return ZuMv(m_ready); }

  int reserve(size_t size, void **base)
  {
    if (m_private || m_ready || !m_allocator || m_allocator->type != Zi::IOBufType::RxTLS ||
      size > Ztls_RxRecordCapacity)
      return Ztls_ErrorLibrary;
    ZmRef<ZiIOBuf> buf;
    try {
      buf = m_allocator->allocBuf(m_owner);
    } catch (...) {
      return Ztls_ErrorNoMemory;
    }
    if (!buf) return Ztls_ErrorNoMemory;
    if (buf->registered() || buf->skip > buf->size ||
      m_allocator->tailRoom > buf->size - buf->skip ||
      buf->size - buf->skip - m_allocator->tailRoom < Ztls_RxRecordCapacity)
      return Ztls_ErrorLibrary;
    m_extent = size;
    m_private = ZuMv(buf);
    *base = m_private->data();
    return 0;
  }
  int commit(size_t length)
  {
    if (!m_private || m_ready || length > m_extent || length > Ztls_RxAppCapacity) {
      discard();
      return Ztls_ErrorLibrary;
    }
    // Clear authenticated padding/content type as well as discarded provisional data.
    ZuClear(m_private->data() + length, m_extent - length);
    m_private->length = uint32_t(length);
    m_ready = ZuMv(m_private);
    m_extent = 0;
    return 0;
  }
  void discard()
  {
    if (m_private) {
      ZuClear(m_private->data(), m_extent);
      m_private = nullptr;
    }
    m_extent = 0;
  }
private:
  Zi::IOBufAllocator	*m_allocator = nullptr;
  void			*m_owner = nullptr;
  ZmRef<ZiIOBuf>	m_private, m_ready;
  size_t		m_extent = 0;
};

// Borrowed for the synchronous reservation/publication operation.
struct RecordPublisher {
  virtual ~RecordPublisher() = default;
  virtual int publish(ZmRef<ZiIOBuf>) = 0;
  virtual void ready(ZmRef<ZiIOBuf>) { }
};

// Owns one unpublished record reservation. Packet backing is never resized.
class RecordSink {
public:
  RecordSink(Zi::IOBufAllocator *allocator, void *owner, uint32_t mps, RecordPublisher &publisher)
  :
    m_allocator{allocator}, m_owner{owner}, m_mps{mps}, m_publisher{publisher}
  { }

  ~RecordSink() { discard(); }

  int reserve(size_t size)
  try {
    if (m_active || !m_mps) return Ztls_ErrorLibrary;
    m_active = true;
    m_size = size;
    size_t remaining = size;
    while (remaining) {
      auto buf = m_allocator->allocBuf(m_owner, 0, m_mps);
      if (!buf) {
	discard();
	return Ztls_ErrorNoMemory;
      }
      if (buf->skip > buf->size || m_allocator->tailRoom >= buf->size - buf->skip) {
	discard();
	return Ztls_ErrorLibrary;
      }
      auto capacity = buf->size - buf->skip - m_allocator->tailRoom;
      if (capacity > remaining) capacity = unsigned(remaining);
      remaining -= capacity;
      m_packets.pushNode(ZuMv(buf));
    }
    m_current = m_cursor();
    return 0;
  } catch (...) {
    discard();
    return Ztls_ErrorNoMemory;
  }

  int next(void **base, size_t *capacity)
  {
    if (!m_active || !m_current || m_committed == m_size) return Ztls_ErrorLibrary;
    auto buf = m_current;
    auto room = buf->size - buf->skip - m_allocator->tailRoom - buf->length;
    if (!room) {
      buf = m_current = m_cursor();
      if (!buf) return Ztls_ErrorLibrary;
      room = buf->size - buf->skip - m_allocator->tailRoom;
    }
    if (room > m_size - m_committed) room = unsigned(m_size - m_committed);
    *base = static_cast<ZiIOBuf *>(buf)->data() + buf->length;
    *capacity = m_exposed = room;
    return room ? 0 : Ztls_ErrorLibrary;
  }

  int commit(size_t length)
  {
    if (!m_active || !m_current || !m_exposed || length > m_exposed) return Ztls_ErrorLibrary;
    m_current->length += uint32_t(length);
    m_committed += length;
    m_exposed = 0;
    return 0;
  }

  int finish()
  try {
    if (!m_active || m_committed != m_size) return Ztls_ErrorLibrary;
    m_publisher.ready(ZmRef<ZiIOBuf>{m_packets.tailPtr()});
    while (auto buf = m_packets.shift()) {
      int n = m_publisher.publish(ZuMv(buf));
      if (n) {
	discard();
	return n;
      }
    }
    discard();
    return 0;
  } catch (...) {
    discard();
    return Ztls_ErrorLibrary;
  }

  void discard()
  {
    m_cursor.reset();
    m_packets.clean();
    m_current = nullptr;
    m_size = m_committed = m_exposed = 0;
    m_active = false;
  }
private:
  Zi::IOBufAllocator	*m_allocator;
  void			*m_owner;
  uint32_t		m_mps;
  RecordPublisher	&m_publisher;
  Zi::IOQueue		m_packets;
  Zi::IOQueue::CIter	m_cursor{m_packets};
  using Node = Zi::IOQueue::Node;
  Node			*m_current = nullptr;
  size_t		m_size = 0;
  size_t		m_committed = 0;
  size_t		m_exposed = 0;
  bool			m_active = false;
};

} // namespace Ztls

// ZtlsBuf_HH
#endif
