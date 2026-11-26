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
#include <zlib/ZmFn.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmRef.hh>

#include <zlib/ZiIOBuf.hh>

#include <zlib/ZutcpIP.hh>

namespace Zutcp {

enum { DefaultControlBufCount = 256 };
enum { DefaultTxControlCredits = 64 };
enum { DefaultRxRetainPerCxn = 256 };
enum { TCPHeadRoom = 14 + 40 + 60 }; // Ethernet + IPv6 + TCP with options

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
struct TxRetry {
  bool spin = false; // permit immediate retries on this submission
  bool refused = false; // ready device refused with no outstanding completions
};

struct PacketBuf : public Zi::IOBuf {
  PacketLease *retained = nullptr;
  TCPHeader tcp;
  TxRetry retry;
  using Zi::IOBuf::IOBuf;
};

// Unregistered codec/recovery fixtures have no raw allocation lease.
inline void bindPacketLease(Zi::IOBuf *, PacketLease *) { }
inline void bindPacketLease(PacketBuf *buf, PacketLease *lease) {
  buf->retained = lease;
}

} // namespace Zutcp

#endif /* ZutcpAlloc_HH */
