//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmPlatform.hh>
#include <zlib/ZmThread.hh>

#include "ZpktAfpPool.hh"

using namespace ZuTestUtil;
using namespace Zpkt::Afp;

static int failAfter = -1;

extern "C" int __real_posix_memalign(void **, size_t, size_t);
extern "C" int __wrap_posix_memalign(void **ptr, size_t align, size_t size)
{
  // Only the fixture's two MTU-sized slabs take part. Framework metadata
  // heaps also call posix_memalign and treat exhaustion as fatal.
  if (align >= 64 && size >= 2 * 1514) {
    if (failAfter == 0) { failAfter = -1; return ENOMEM; }
    if (failAfter > 0) --failAfter;
  }
  return __real_posix_memalign(ptr, align, size);
}

static void geometry()
{
  ZuTestScope(geometry);
  constexpr unsigned line = Zm::CacheLineSize;
  bool prepared = true, physical = true, aligned = true;
  for (unsigned bytes : {line - 1, line, line + 1}) {
    for (unsigned align : {1U, line << 2}) {
      ZmRef<BufPool> pool = new BufPool{Zi::IOBufType::TxTCP, 4, bytes, 0, 0, align};
      BufPool::Prepared backing;
      int err = pool->prepare(backing);
      prepared &= !err;
      if (err) continue;
      pool->publish(ZuMv(backing));
      const auto &layout = pool->geometry();
      auto effective = align > line ? align : line;
      physical &= layout.backing == ((bytes + line - 1) & ~(line - 1)) &&
	layout.alignment == effective && !(layout.stride & (effective - 1)) &&
	layout.prefix >= sizeof(Slot) && layout.stride >= layout.prefix + layout.backing;
      uint8_t *held[4]{};
      bool exact = true;
      for (unsigned i = 0; i < 4; ++i) {
	auto p = held[i] = pool->alloc();
	exact &= p && !(uintptr_t(p) & (effective - 1)) &&
	  (!i || uintptr_t(p) - uintptr_t(held[i - 1]) == layout.stride) &&
	  pool->find(p) && pool->find(p) == pool->find(p + bytes - 1) &&
	  !pool->find(p + bytes);
      }
      aligned &= exact && !pool->alloc();
      for (auto p : held) if (p) pool->free(p + bytes - 1);
      pool->final();
    }
  }

  ZuCHECK(prepared, "prepare four aligned slots for every tested request");
  ZuCHECK(physical, "physical backing rounds independently of the requested capacity");
  ZuCHECK(aligned, "all slots have aligned bases and strides with padding excluded from lookup");

  ZmRef<BufPool> overflow = new BufPool{Zi::IOBufType::TxTCP, UINT32_MAX,
    UINT32_MAX, 0, 0, 1u << 31};
  BufPool::Prepared backing;
  ZuCHECK(overflow->prepare(backing) == EOVERFLOW, "overflow rejected before allocation");
  overflow->count = 1;
  overflow->size = 64;
  overflow->align = 3;
  ZuCHECK(overflow->prepare(backing) == EINVAL, "alignment must be a power of two");
  overflow->align = 64;
  overflow->headRoom = 64;
  overflow->tailRoom = 1;
  ZuCHECK(overflow->prepare(backing) == EINVAL, "headroom and tailroom fit together");
}

static void transaction()
{
  ZuTestScope(transaction);
  ZmRef<BufPool> rx = new BufPool{Zi::IOBufType::Rx, 2, 1514, 35, 0, 64};
  ZmRef<BufPool> tcp = new BufPool{Zi::IOBufType::TxTCP, 3, 1514, 0, 0, 128};
  ZmRef<BufPool> udp = new BufPool{Zi::IOBufType::TxUDP, 1, 127, 127, 0, 64};
  Zi::IOBufPoolList list;
  list.pushNode(tcp.ptr());
  PoolSet::PoolRefs reserves;
  reserves.push(tcp);
  PoolSet pools;
  ZuCHECK(pools.alloc(rx.ptr(), list, reserves, 1500).is<ZeException>() &&
      !pools.allocated() && !rx->alloc() && !tcp->alloc(),
    "overlapping reserve and caller-list entries fail before publication");
  reserves[0] = udp;
  failAfter = 1;
  ZuCHECK(pools.alloc(rx.ptr(), list, reserves, 1500).is<ZeException>() &&
      failAfter == -1 && !pools.allocated() && !rx->alloc() && !tcp->alloc() && !udp->alloc(),
    "allocation failure after the Rx slab leaves each pool unpublished");
  auto result = pools.alloc(rx.ptr(), list, reserves, 1500);
  ZuCHECK(!result.is<ZeException>() && pools.allocated() && pools.txCapacity() == 4 &&
      list.count_() == 1 && udp->size == 127 && udp->headRoom == 127,
    "retry preserves the intrusive list and all pool geometry");
  if (!pools.allocated()) return;
  ZuCHECK(!pools.alloc(rx.ptr(), list, reserves, 1500).is<ZeException>(),
    "same collection is idempotent");
  ZuCHECK(pools.alloc(rx.ptr(), list, {}, 1500).is<ZeException>(),
    "changed collection is rejected");
  auto input = rx->alloc();
  auto control = udp->alloc();
  ZuCHECK(input && control && rx->find(input + 1513) && !rx->find(input + 1514) &&
      pools.findTx(control + 126) && !pools.findTx(control + 127) && !pools.findTx(input),
    "full-MTU Rx uses full logical extent and Tx index excludes Rx and padding");
  if (input) rx->free(input + 19);
  if (control) udp->free(control);
  auto bytes = tcp->alloc();
  auto slot = pools.findTx(bytes + 14);
  memset(bytes, 0x53, tcp->size);
  ZuCHECK(slot && slot->packet() == bytes && slot->submit() && !slot->submit() &&
      !pools.final(), "caller and submission leases protect the same slot");
  ZmThread caller{[&tcp, bytes] { tcp->free(bytes + 1500); }};
  caller.join();
  ZuCHECK(tcp->used() == 1 && bytes[1500] == 0x53, "caller return leaves submission intact");
  slot->release(Slot::Submitted);
  auto reused = tcp->alloc();
  ZuCHECK(reused == bytes && pools.findTx(reused)->submit(),
    "retirement returns the canonical slot exactly once");
  slot->release(Slot::Submitted);
  ZuCHECK(tcp->used() == 1 && slot->submit(), "retained caller can retransmit after retirement");
  slot->release(Slot::Submitted);
  tcp->free(reused);
  ZuCHECK(pools.final() && !pools.completions() && !pools.txCapacity() &&
      !rx->alloc() && !tcp->alloc() && !udp->alloc(),
    "all backing is retired after callers release their final leases");
  ZuCHECK(!pools.alloc(rx.ptr(), list, reserves, 1500).is<ZeException>() &&
      pools.completions() && pools.txCapacity() == 4 && pools.final(),
    "a finalized pool set can provision a new completion store");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(geometry);
  ZuTestCall(transaction);
}
