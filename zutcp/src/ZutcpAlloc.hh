//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Registered Tx/control provider composition over caller-owned raw packet pools.

#ifndef ZutcpAlloc_HH
#define ZutcpAlloc_HH

#ifndef ZutcpLib_HH
#include <zlib/ZutcpLib.hh>
#endif

#include <stdint.h>

#include <zlib/ZmHeap.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZiIOBuf.hh>
#include <zlib/ZutcpIP.hh>
#include <zlib/ZxdpAlloc.hh>

namespace Zutcp {

enum { DefaultControlBufCount = 256 };
enum { DefaultTxControlCredits = 64 };
enum { DefaultRxRetainFrames = 1024 };
enum { DefaultRxRetainPerCxn = 256 };
enum { DefaultRxReserveFrames = 64 };

// Rx-owned identity, independent of Connection lifetime. A slot retains this
// account while application aliases hold its original registered packet.
struct ZutcpAPI RxAccount : public ZmNHeap<RxAccount>, public ZmObject {
friend inline ZuStringT<"Zutcp.RxAccount"> ZmNHeapID(RxAccount *);
  uint64_t generation;
  unsigned limit, frames = 0;

  RxAccount(unsigned limit_, uint64_t generation_) :
    generation{generation_}, limit{limit_} { }
  ~RxAccount();
};

struct RxFrameStats {
  uint64_t admitted = 0, refused = 0, retired = 0;
  unsigned capacity = 0, limit = 0, retained = 0;
  unsigned configured = 0, configuredPerCxn = 0, perCxnLimit = 0, reserve = 0;
};

// One preallocated entry per physical ingress slot, not per packet/slice.
// All operations except returned() run on Rx (or serialized stopped control).
class ZutcpAPI RxFrames : public ZmNHeap<RxFrames>, public ZmObject {
friend inline ZuStringT<"Zutcp.RxFrames"> ZmNHeapID(RxFrames *);
public:
  RxFrames(unsigned count, unsigned limit);
  ~RxFrames();
  bool retain(unsigned slot, RxAccount *);
  void returned(unsigned slot); // final provider return, on any thread
  unsigned reconcile();
  RxFrameStats stats() const;
  void limit(unsigned); // serialized stopped configuration; preserves old charges

private:
  enum { NoSlot = unsigned(-1) };
  struct Slot {
    ZmRef<RxAccount> account;
    unsigned next = NoSlot;
  };
  ZtArray<Slot, ZtArrayHeapID<"Zutcp.RxSlots">> m_slots;
  ZmAtomic<unsigned> m_returns{NoSlot};
  unsigned m_limit, m_retained = 0;
  uint64_t m_admitted = 0, m_refused = 0, m_retired = 0;
};

// Zi adapters retain this provider through final registered returns. Its pool
// therefore outlives the packet instance and all application/DMA-held buffers.
// Freeze geometry and intrusive links while the raw instance is active.
struct ZutcpAPI Allocator : public ZmNHeap<Allocator>, public Zi::IOBufPlugin {
friend inline ZuStringT<"Zutcp.Allocator"> ZmNHeapID(Allocator *);
  using ReturnedFn = ZmFn<void(), ZmFnHeapID<"Zutcp.Returned">>;
  Zxdp::BufPool	pool;
  Allocator	*next = nullptr;
  ReturnedFn	returned;
  ZmRef<RxFrames> rxFrames; // physical ingress only; absent for Tx providers

  Allocator(unsigned count, unsigned bytes, unsigned head = 0, unsigned tail = 0);
  uint8_t *alloc() override;
  void free(uint8_t *) override;
};

// The caller allocation lease lives in an existing recovery/source node,
// independently of copied SentPkt metadata and the raw kernel lease. Owners drain
// recovery before releasing the provider; there is no per-packet owner ref.
class ZutcpAPI PacketLease {
public:
  PacketLease() = default;
  PacketLease(const PacketLease &) = delete;
  PacketLease &operator =(const PacketLease &) = delete;
  ~PacketLease();

  void adopt(Zi::IOBufAllocator *, uint8_t *, unsigned);
  void reset();
  bool operator *() const { return m_data; }
  uint8_t *data() const { return m_data; }

private:
  Zi::IOBufAllocator *m_allocator = nullptr;
  uint8_t *m_data = nullptr;
  unsigned m_size = 0;
};

// Internal packet adapters and retained source nodes use this wire metadata.
// The handoff is Tx-owned, valid for one synchronous submission, and cleared
// by the builder before the sent record can be discarded on refusal.
struct PacketBuf : public Zi::IOBuf {
  PacketLease *retained = nullptr;
  TCPHeader tcp;
  using Zi::IOBuf::IOBuf;
};

// Unregistered codec/recovery fixtures have no raw allocation lease.
inline void bindPacketLease(Zi::IOBuf *, PacketLease *) { }
inline void bindPacketLease(PacketBuf *buf, PacketLease *lease) {
  buf->retained = lease;
}

// Logical Rx context providers own metadata only and retain the single physical
// ingress provider. Final returns reach that original pool after stub attachment.
struct ZutcpAPI RxAllocator : public ZmNHeap<RxAllocator>, public Zi::IOBufPlugin {
friend inline ZuStringT<"Zutcp.RxAllocator"> ZmNHeapID(RxAllocator *);
  ZmRef<Allocator> ingress;
  unsigned count, align; // logical request only; no additional physical Rx region

  RxAllocator(Allocator *, unsigned head, unsigned count, unsigned align);
  uint8_t *alloc() override { return nullptr; } // Zi Rx creates backingless stubs
  void free(uint8_t *) override;
};

} // namespace Zutcp

#endif /* ZutcpAlloc_HH */
