//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Transport byte storage and native buffer metadata

#ifndef Zutcp_HH
#error "include zlib/Zutcp.hh before this header"
#endif

#include <string.h>

#include <zlib/ZmRef.hh>
#include <zlib/ZmPolymorph.hh>
#include <zlib/ZutcpAlloc.hh>
#include <zlib/ZmSingleton.hh>

#include <zlib/ZiAssert.hh>
#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiIP.hh>
#include <zlib/ZiRxStream.hh>
#include <zlib/ZiTxStream.hh>

namespace Zutcp_ {

// Receive packets and retained plaintext remain independent of the Tx queue.
struct BufNode : public ZiIOBuf {
  using ZiIOBuf::ZiIOBuf;
};

using Reader = ZiRxStream<ZiRxQueue>;

template <
  unsigned Size = BufSize,
  unsigned MaxSize = ZiIOBuf_DefltVHMaxSize,
  ZuString HeapID = "Zutcp.Buf">
using BufAlloc =
  Zi::IOBufBuiltinAllocator<
    Zi::IOBufType::App, BufNode, Size, ZiIOBuf_Align, ZuStringT<HeapID>, MaxSize>;

} // namespace Zutcp_

namespace Zutcp {

using Reader = Zutcp_::Reader;

template <
  unsigned Size = BufSize,
  unsigned MaxSize = ZiIOBuf_DefltVHMaxSize,
  ZuString HeapID = "Zutcp.Connection.TxBuf">
using TxBufAlloc = Zutcp_::BufAlloc<Size, MaxSize, HeapID>;

// Adapter for optional receive heap replacement. Owned here, independently of
// the mx registry and native registered packet pools.
inline Zi::IOBufAllocator *inlineAllocator() {
  return ZmSingleton<TxBufAlloc<>>::instance();
}


struct BufDiag { };

// Base of the existing queued-range node. Recovery references this storage,
// independently of its current queued view or submitted Zi metadata. Before
// submission pending owns backing; raw handoff replaces it with the pool lease.
// Once all original bytes are prepared and finally ACKed, retire the lease and data,
// independently of historical packet/frame references retained for late ACKs.
// Its PacketBuf base is a borrowed wire view, using the same node/reference
// count. Stable payload coordinates below remain independent of that view.
struct TxBacking : public PacketBuf {
  ZmRef<ZiIOBuf> pending;
  PacketLease backing;
  Zi::IOBufAllocator *native = nullptr;
  Zxdp::BufPool *pool = nullptr;
  uint8_t *data = nullptr;
  uint32_t size = 0;
  uint32_t payloadOffset = 0, payloadLength = 0;
  uint32_t preparedLength = 0; // consumed queue prefix, including send-refused recovery work
  bool fin = false; // FIN attached to this original packet also retains its backing

  TxBacking() : PacketBuf{nullptr, 0, nullptr, nullptr, 0, true, false} { }
  explicit TxBacking(ZmRef<ZiIOBuf> buf) :
    PacketBuf{nullptr, 0, nullptr, nullptr, 0, true, false},
    pending{ZuMv(buf)}, data{pending ? pending->data_() : nullptr},
    size{pending ? pending->size : 0},
    payloadOffset{pending ? pending->skip : 0},
    payloadLength{pending ? pending->length : 0} { }
};

// Restore an unsent producer view after temporary header placement. Accepted
// submission detaches it instead. The builder keeps metadata alive throughout.
class TxPacketView {
public:
  TxPacketView(PacketBuf *buf, unsigned shift = 0, unsigned head = 0) :
    m_buf{buf}, m_data{buf ? buf->data__ : 0}, m_size{buf ? buf->size : 0},
    m_skip{buf ? buf->skip : 0}, m_length{buf ? buf->length : 0}
  {
    if (!buf) return;
    buf->data__ += shift;
    buf->size -= shift;
    buf->skip = head;
    buf->length = 0;
  }
  TxPacketView(const TxPacketView &) = delete;
  TxPacketView &operator =(const TxPacketView &) = delete;
  ~TxPacketView() {
    if (!m_buf || !m_buf->data_()) return;
    m_buf->data__ = m_data;
    m_buf->size = m_size;
    m_buf->skip = m_skip;
    m_buf->length = m_length;
  }

private:
  PacketBuf *m_buf;
  uintptr_t m_data;
  uint32_t m_size, m_skip, m_length;
};

// Retained payload positions are relative to the original allocation.
struct TxRange {
  ZmRef<TxBacking>	storage;
  uint32_t		offset = 0;
  uint32_t		length = 0;
  uint64_t		dataOffset = 0;

  TxRange() = default;
  TxRange(
    ZmRef<TxBacking> storage_, uint32_t offset_, uint32_t length_,
    uint64_t dataOffset_ = 0) :
    storage{ZuMv(storage_)}, offset{offset_}, length{length_},
    dataOffset{dataOffset_} { }
};

inline unsigned txPktCapacity(const ZiIOBuf *buf)
{
  return buf->avail() - buf->allocator()->tailRoom;
}

inline void assertPktCapacity(const ZiIOBuf *buf, unsigned required)
{
  ZiAssert(buf && buf->size >= required, "Zutcp", (required),
    "packet buffer capacity violation required=" << required,
    return);
}


} // namespace Zutcp
