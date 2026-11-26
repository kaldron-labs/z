//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmSemaphore.hh>
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
  TxReturns returns;
  BufPool::Prepared ingress, stream, datagram;
  auto rc = returns.init(2, SOCKET_ID_ANY);
  if (!rc) rc = rx->prepare(ingress, SOCKET_ID_ANY);
  if (!rc) rc = tcp->prepare(stream, SOCKET_ID_ANY, 0, &returns);
  if (!rc) rc = udp->prepare(datagram, SOCKET_ID_ANY, 0, &returns);
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
  constexpr unsigned mask = Zm::CacheLineSize - 1;
  auto room = [](unsigned size) { return ((size + mask) & ~mask) + RTE_PKTMBUF_HEADROOM; };
  ZuCHECK(rte_pktmbuf_data_room_size(rx->native()) == room(rx->size) &&
      rte_pktmbuf_data_room_size(tcp->native()) == room(tcp->size) &&
      rte_pktmbuf_data_room_size(udp->native()) == room(udp->size) &&
      !rx->native()->cache_size && !tcp->native()->cache_size && !udp->native()->cache_size,
    "native data rooms round independently and keep caches disabled");
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

static void testCacheLines()
{
  ZuTestScope(testCacheLines);
  constexpr unsigned line = Zm::CacheLineSize;
  TxReturns returns;
  auto rc = returns.init(1, SOCKET_ID_ANY);
  ZuCHECK(!rc, "allocate the shared Tx ready queue");
  if (rc) return;
  for (Zi::IOBufType::T type : {Zi::IOBufType::Rx, Zi::IOBufType::TxTCP, Zi::IOBufType::TxUDP})
    for (unsigned size : {line - 1, line, line + 1})
      for (unsigned request : {1U, line<<2}) {
	BufPool pool{type, 4, size, 0, 0, request};
	BufPool::Prepared prepared;
	rc = pool.prepare(prepared, SOCKET_ID_ANY, 0,
	  type == Zi::IOBufType::Rx ? nullptr : &returns);
	ZuCHECK(!rc, "prepare four slots with size ", size, " and alignment ", request);
	if (rc) continue;
	auto native = prepared.native();
	AddressIndex index;
	rc = index.init({&native, 1});
	ZuCHECK(!rc, "index cache-line-aligned slots");
	if (rc) continue;
	pool.publish(ZuMv(prepared));
	unsigned alignment = RTE_MAX(request, line);
	auto stride = native->header_size + native->elt_size + native->trailer_size;
	bool valid = !(stride % alignment) &&
	  rte_pktmbuf_data_room_size(native) - RTE_PKTMBUF_HEADROOM ==
	    ((size + line - 1) & ~(line - 1));
	uint8_t *slots[4]{};
	for (auto &p : slots) {
	  p = pool.alloc();
	  if (!p) { valid = false; continue; }
	  auto slot = index.find(p);
	  valid &= !(uintptr_t(p) & (alignment - 1)) && slot &&
	    index.find(p + size - 1) == slot && !index.find(p + size);
	}
	ZuCHECK(valid && !pool.alloc(),
	  "all slots have aligned bases, rounded physical stride and exact logical extents");
	for (auto p : slots) if (p) pool.free(p + size - 1);
	ZuCHECK(pool.available() == pool.count && pool.final(),
	  "return every slot through its last logical byte");
      }
}

static bool retire(TxReturns &returns, Slot *expected)
{
  auto pool = returns.shift();
  if (!pool) return false;
  void *returned = nullptr;
  auto rc = rte_mempool_get(pool, &returned);
  if (!rc) BufPool::slot(static_cast<rte_mbuf *>(returned))->release(Slot::Submitted);
  returns.settle(pool);
  return !rc && returned == expected->mbuf;
}

static void testLeases()
{
  ZuTestScope(testLeases);
  TxReturns returns;
  ZmRef<BufPool> pool = new BufPool{Zi::IOBufType::TxUDP, 1, 128, 42, 0, 64};
  BufPool::Prepared backing;
  auto rc = returns.init(1, SOCKET_ID_ANY);
  if (!rc) rc = pool->prepare(backing, SOCKET_ID_ANY, 0, &returns);
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
  ZuCHECK(submitted && rte_mbuf_refcnt_read(slot->mbuf) == 1 && !slot->submit() &&
      !returns.shift(), "retain one submission with the PMD as sole native owner");
  if (!submitted) { pool->free(p); return; }
  ZmThread caller{[pool, p] { pool->free(p + 42); }};
  caller.join();
  ZuCHECK(!pool->alloc() && !pool->final() && !returns.shift() && p[42] == 0x5a,
    "off-owner caller return preserves driver-held bytes and prevents reuse");
  rte_pktmbuf_free(slot->mbuf);
  ZuCHECK(slot->leases == Slot::Submitted && !pool->alloc(),
    "a PMD return keeps the submission lease until the Tx owner drains it");
  ZuCHECK(retire(returns, slot) && !returns.shift(), "native return identifies the exact mbuf");
  auto returned = pool->alloc();
  ZuCHECK(returned == p && !pool->alloc(), "release of both leases recycles the same slot");
  if (!returned) return;
  submitted = slot->submit();
  ZuCHECK(submitted, "resubmit recovered backing");
  if (!submitted) { pool->free(returned); return; }
  rte_pktmbuf_free(slot->mbuf);
  ZuCHECK(retire(returns, slot) && !pool->alloc() && !pool->final() && p[127] == 0x5a,
    "completed submission preserves the independent caller's occupied slot");
  BufPool::resetTx(slot->mbuf);
  submitted = slot->submit();
  ZuCHECK(submitted, "same retained allocation can submit again after retirement");
  if (!submitted) { pool->free(returned); return; }
  rte_pktmbuf_free(slot->mbuf);
  ZmSemaphore release;
  ZmThread concurrent{[pool, p, &release] { release.wait(); pool->free(p); }};
  release.post();
  bool retired = retire(returns, slot);
  concurrent.join();
  auto unsent = pool->alloc();
  ZuCHECK(retired && unsent == p && !pool->alloc() && !returns.shift(),
    "concurrent caller return and retirement recycle one slot");
  if (unsent) pool->free(unsent);
  ZuCHECK(!returns.shift() && !rte_mempool_avail_count(native),
    "unsent free recycles only to the application free list");
  index.reset();
  ZuCHECK(pool->final() && !pool->native() && !pool->alloc(),
    "all outstanding ownership ends before native backing finalizes");
}

static void testRollback()
{
  ZuTestScope(testRollback);
  TxReturns returns;
  auto rc = returns.init(2, SOCKET_ID_ANY);
  ZuCHECK(!rc, "allocate the ready queue for provisional Tx pools");
  if (rc) return;
  BufPool valid{Zi::IOBufType::TxTCP, 2, 1514, 114, 0, 64};
  BufPool invalid{Zi::IOBufType::TxTCP, 3, 256, 128, 129, 64};
  ZtString<> name;
  {
    BufPool::Prepared first, second;
    rc = valid.prepare(first, SOCKET_ID_ANY, 0, &returns);
    ZuCHECK(!rc, "prepare the valid member of a provisional set [", rc, "]");
    if (rc) return;
    name = first.native()->name;
    ZuCHECK(invalid.prepare(second, SOCKET_ID_ANY, 0, &returns) == -EINVAL && !second.native(),
      "reject inconsistent geometry before publication");
  }
  ZuCHECK(!rte_mempool_lookup(name.ndata()) && !valid.alloc(),
    "rollback releases provisional native resources and preserves unallocated state");
  BufPool::Prepared retry;
  rc = valid.prepare(retry, SOCKET_ID_ANY, 0, &returns);
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
    try {
      eal->init(params);
      ZuCHECK(eal->ready(), "initialize EAL explicitly");
      ZuTestCall(testGeometry);
      ZuTestCall(testCacheLines);
      ZuTestCall(testLeases);
      ZuTestCall(testRollback);
    } catch (ZeException &e) {
      ZiLogEvent(ZuMv(e));
      ZuCHECK(false, "initialize EAL explicitly");
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
