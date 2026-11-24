//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmThread.hh>

#include <zlib/ZiLog.hh>

#include "ZpktDpdk.hh"
#include "ZpktDpdkPool.hh"

using namespace ZuTestUtil;
using namespace Zpkt::Dpdk;

static void testGeometry()
{
  ZuTestScope(testGeometry);
  ZmRef<BufPool> rx = new BufPool{Zi::IOBufType::Rx, 3, 6000, 114, 0, 512};
  ZmRef<BufPool> tcp = new BufPool{Zi::IOBufType::TxTCP, 2, 192, 37, 11, 256};
  ZmRef<BufPool> udp = new BufPool{Zi::IOBufType::TxUDP, 1, 128, 42, 8, 64};
  BufPool::Prepared ingress, stream, datagram;
  auto rc = rx->prepare(ingress, SOCKET_ID_ANY);
  if (!rc) rc = tcp->prepare(stream, SOCKET_ID_ANY);
  if (!rc) rc = udp->prepare(datagram, SOCKET_ID_ANY);
  ZuCHECK(!rc, "provision Rx and both Tx roles [", rc, "]");
  if (rc) return;
  ZuCHECK(!rx->native() && !tcp->alloc() && !udp->alloc(),
    "provisional backing is inaccessible before publication");
  rte_mempool *native[] = {ingress.native(), stream.native(), datagram.native()};
  AddressIndex index;
  rc = index.init(native);
  ZuCHECK(!rc, "index the complete backing set [", rc, "]");
  if (rc) return;
  rx->publish(ZuMv(ingress));
  tcp->publish(ZuMv(stream));
  udp->publish(ZuMv(datagram));
  ZuCHECK(rx->type == Zi::IOBufType::Rx && tcp->type == Zi::IOBufType::TxTCP &&
      udp->type == Zi::IOBufType::TxUDP && rx->native() != tcp->native() &&
      tcp->native() != udp->native(),
    "native backing preserves distinct immutable roles");
  ZuCHECK(rte_pktmbuf_data_room_size(rx->native()) == rx->size + RTE_PKTMBUF_HEADROOM &&
      rte_pktmbuf_data_room_size(tcp->native()) == tcp->size + RTE_PKTMBUF_HEADROOM &&
      rte_pktmbuf_data_room_size(udp->native()) == udp->size + RTE_PKTMBUF_HEADROOM &&
      !rx->native()->cache_size && !tcp->native()->cache_size && !udp->native()->cache_size,
    "physical alignment padding leaves the data room exact and caches disabled");
  auto mbuf = rte_pktmbuf_alloc(rx->native());
  bool adopted = mbuf && rx->adopt(mbuf);
  if (mbuf) {
    auto p = rte_pktmbuf_mtod(mbuf, uint8_t *);
    auto s = index.find(p);
    bool offsets = s && s->mbuf == mbuf;
    for (unsigned i = 0; i < rx->size; ++i) offsets &= index.find(p + i) == s;
    ZuCHECK(adopted && offsets && !(uintptr_t(p) & 511) &&
	!index.find(p - 1) && !index.find(p + rx->size) && !index.find(&native),
      "aligned Rx uses native headroom and every valid offset identifies its original mbuf");
    if (adopted) rx->free(p + 114);
    else rte_pktmbuf_free(mbuf);
  } else ZuCHECK(false, "native Rx slot available");
  ZuCHECK(!rte_mempool_in_use_count(rx->native()), "Rx packet returns to its native ingress pool");

  Zi::IOBufPoolAllocator<Zi::IOBufType::TxTCP> tx{nullptr, tcp};
  auto first = tx.allocBuf(nullptr, 32), second = tx.allocBuf(nullptr, 32);
  auto p = udp->alloc();
  auto firstSlot = first ? index.find(first->data()) : nullptr;
  auto udpSlot = index.find(p);
  ZuCHECK(first && second && p && !udp->alloc() && !tx.allocBuf(nullptr, 1) &&
      first->registered() && first->allocator() == &tx && first->skip == 37 &&
      first->size == 192 && first->length == 32 && !(uintptr_t(first->data_()) & 255) &&
      firstSlot && firstSlot->pool == tcp.ptr() && udpSlot && udpSlot->pool == udp.ptr(),
    "independent bounded Tx pools preserve allocator, offsets and requested geometry");
  if (p) udp->free(p);
}

static void testLeases()
{
  ZuTestScope(testLeases);
  ZmRef<BufPool> pool = new BufPool{Zi::IOBufType::TxUDP, 1, 128, 42, 0, 64};
  BufPool::Prepared backing;
  auto rc = pool->prepare(backing, SOCKET_ID_ANY);
  ZuCHECK(!rc, "allocate a single-slot native pool [", rc, "]");
  if (rc) return;
  auto native = backing.native();
  AddressIndex index;
  rc = index.init({&native, 1});
  ZuCHECK(!rc, "materialize single-slot indexing [", rc, "]");
  if (rc) return;
  pool->publish(ZuMv(backing));
  auto p = pool->alloc();
  auto slot = index.find(p);
  if (!p || !slot) {
    ZuCHECK(false, "acquire indexed Tx backing");
    if (p) pool->free(p);
    return;
  }
  memset(p, 0x5a, pool->size);
  bool submitted = slot->submit();
  ZuCHECK(submitted && !slot->retired() && !slot->submit(),
    "retain exactly one device submission at a time");
  if (!submitted) { pool->free(p); return; }
  ZmThread caller{[pool, p] { pool->free(p + 42); }};
  caller.join();
  ZuCHECK(!pool->alloc() && !pool->final() && !slot->retired() && p[42] == 0x5a,
    "off-owner caller return preserves driver-held bytes and prevents reuse");
  rte_pktmbuf_free(slot->mbuf);
  ZuCHECK(slot->retired() && !pool->alloc(), "only the PMD return proves native retirement");
  slot->releaseSubmission();
  auto returned = pool->alloc();
  ZuCHECK(returned == p && !pool->alloc(), "release of both leases recycles the same slot");
  if (!returned) return;
  submitted = slot->submit();
  ZuCHECK(submitted, "resubmit recovered backing");
  if (!submitted) { pool->free(returned); return; }
  rte_pktmbuf_free(slot->mbuf);
  slot->releaseSubmission();
  ZuCHECK(!pool->alloc() && !pool->final() && p[127] == 0x5a,
    "completed submission preserves the independent caller's occupied slot");
  pool->free(returned);
  index.reset();
  ZuCHECK(pool->final() && !pool->native() && !pool->alloc(),
    "all outstanding ownership ends before native backing finalizes");
}

static void testRollback()
{
  ZuTestScope(testRollback);
  BufPool valid{Zi::IOBufType::Rx, 2, 1514, 114, 0, 64};
  BufPool invalid{Zi::IOBufType::TxTCP, 3, 256, 128, 129, 64};
  ZtString<> name;
  {
    BufPool::Prepared first, second;
    auto rc = valid.prepare(first, SOCKET_ID_ANY);
    ZuCHECK(!rc, "prepare the valid member of a provisional set [", rc, "]");
    if (rc) return;
    name = first.native()->name;
    ZuCHECK(invalid.prepare(second, SOCKET_ID_ANY) == -EINVAL && !second.native(),
      "reject inconsistent geometry before publication");
  }
  ZuCHECK(!rte_mempool_lookup(name.ndata()) && !valid.alloc(),
    "rollback releases provisional native resources and preserves unallocated state");
  BufPool::Prepared retry;
  auto rc = valid.prepare(retry, SOCKET_ID_ANY);
  ZuCHECK(!rc, "retry provisioning after rollback [", rc, "]");
}

static void testDpdkPools()
{
  ZuTestScope(testDpdkPools);
  char dir[] = "/tmp/ZpktDpdkPoolTest.XXXXXX";
  auto made = mkdtemp(dir);
  ZuCHECK(made, "create isolated EAL runtime directory");
  if (!made) return;
  auto env = setenv("RUNTIME_DIRECTORY", dir, 1);
  ZuCHECK(!env, "configure isolated EAL runtime directory");
  if (!env) {
    EALParams params{};
    params.noPCI = params.noHuge = params.noShconf = params.noTelemetry = true;
    params.iova = IOVA::VA;
    EAL ownedEAL;
    auto eal = &ownedEAL;
    auto result = eal->init(params);
    ZuCHECK(!result.is<ZeException>() && eal->ready(), "initialize EAL explicitly");
    if (result.is<ZeException>()) ZiLogEvent(ZuMv(result.p<ZeException>()));
    else {
      ZuTestCall(testGeometry);
      ZuTestCall(testLeases);
      ZuTestCall(testRollback);
    }
  }
  ZtString<> runtime{dir};
  runtime << "/dpdk/rte";
  rmdir(runtime.ndata());
  runtime = dir;
  runtime << "/dpdk";
  rmdir(runtime.ndata());
  ZuCHECK(!rmdir(dir), "remove the isolated EAL runtime directory");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testDpdkPools);
}
