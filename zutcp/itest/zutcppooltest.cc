//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuTime.hh>
#include <zlib/ZmAssert.hh>
#include <zlib/ZmTime.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZutcpAlloc.hh>
#include <zlib/Zxdp.hh>

using namespace ZuTestUtil;

static bool closeInstance(ZmRef<Zxdp::Instance> &instance, ZtString<> &error)
{
  auto limit = Zm::now() + ZuTime{10};
  do {
    if (instance->close(error)) { instance = nullptr; return true; }
    // Fixture main only; expiry never authorizes retirement of live backing.
    poll(nullptr, 0, 1);
  } while (Zm::now() < limit);
  return false;
}

static void testProvider()
{
  ZuTestScopeRT(testProvider);
  enum { PacketSize = 1514, Head = 42, Tail = 8, Payload = 42, RxCount = 8 };
  // Provider ownership is deliberately independent of backend ownership. Zi's
  // adapter owns the provider; the raw receive callback exchanges addresses only.
  ZiMultiplex mx;
  ZmRef<Zutcp::Allocator> provider = new Zutcp::Allocator{2, PacketSize, Head, Tail};
  ZmRef<Zutcp::Allocator> control = new Zutcp::Allocator{1, 128};
  provider->pool.next = &control->pool;
  Zi::IOBufPluginAllocator<Zi::IOBufType::TxTCP> allocator{&mx, provider};
  Zi::IOBufPluginAllocator<Zi::IOBufType::TxTCP> ctlAllocator{&mx, control};
  ZuCheckRT(allocator.size == PacketSize && allocator.headRoom == Head &&
      allocator.tailRoom == Tail && !allocator.allocBuf(nullptr, Payload),
    log_("provider publishes fixed geometry before backing is activated"));
  Zxdp::BufPool rx{RxCount, PacketSize};
  unsigned completed = 0;
  uint8_t *canonical = nullptr;
  ZtString<> error;
  auto instance = Zxdp::Instance::create({
    .interface = unsigned(strtoul(getenv("ZXDP_TEST_IFINDEX"), nullptr, 10)),
    .txPools = &provider->pool, .rxPool = &rx,
    .rx = [&rx](uint8_t *p, unsigned) { rx.free(p); }}, error);
  bool ready = instance && instance->ready();
  ZuCheckRT(ready, log_("raw instance activates caller-owned Zutcp provider pools: ", error));
  if (ready) {
    auto buf = allocator.allocBuf(&mx, Payload);
    auto other = allocator.allocBuf(&mx, Payload);
    auto ctl = ctlAllocator.allocBuf(&mx, 64);
    ZuCheckRT(buf && other && ctl && !allocator.allocBuf(&mx, Payload) &&
        !allocator.allocBuf(&mx, allocator.capacity() + 1),
      log_("application exhaustion does not consume control backing or grow the reserve"));
    if (buf && other && ctl) {
      canonical = buf->data_();
      auto payload = buf->data();
      bool geometry = buf->registered() && buf->owner == &mx && buf->skip == Head &&
        buf->length == Payload && payload == canonical + Head &&
        provider->pool.owns(canonical) && provider->pool.used() == 2;
      memset(canonical, 0, Head + Payload);
      memset(canonical, 0xff, 6);
      memcpy(canonical + 6, instance->mac(), 6);
      canonical[12] = 0x88;
      canonical[13] = 0xb5;
      for (unsigned i = 0; i < Payload; ++i) payload[i] = i ^ 0xa7;
      buf->rewind(Head);
      ZuCheckRT(geometry && buf->data() == canonical && buf->length == Head + Payload,
        log_("Zi payload and prepended wire headers retain the same raw pool backing"));
      uint8_t retainedBytes[64];
      memcpy(retainedBytes, canonical, sizeof(retainedBytes));
      memcpy(ctl->data(), retainedBytes, sizeof(retainedBytes));
      auto heldOrigin = ctl->data_();
      bool held = instance->send(ctl->data(), ctl->length);
      ZuCheckRT(held && !control->pool.writable(heldOrigin),
        log_("a caller allocation remains unwritable through its kernel submission"));
      bool accepted = instance->send(buf->data(), buf->length);
      if (accepted) {
        buf->data__ = 0;
        buf->size = buf->skip = buf->length = 0;
        provider->free(canonical);
      }
      ZuCheckRT(accepted && !buf->data_() && !buf->registered(),
        log_("accepted raw submission permits metadata to detach backing"));
      buf = nullptr;
      ZuCheckRT(provider->pool.used() == 2 && !allocator.allocBuf(&mx, Payload),
        log_("metadata destruction does not recycle kernel-owned backing"));
      auto deadline = Zm::now() + ZuTime{10};
      while (accepted && completed < 2 && Zm::now() < deadline) {
        completed += instance->pollTx();
        if (completed < 2) poll(nullptr, 0, 1);
      }
      auto recycled = allocator.allocBuf(&mx, Payload);
      ZuCheckRT(completed == 2 && recycled && recycled->data_() == canonical &&
          provider->pool.used() == 2 && control->pool.used() == 1 &&
          ctl->data_() == heldOrigin && control->pool.writable(heldOrigin) &&
          !memcmp(ctl->data(), retainedBytes, 64),
        log_("CQ recycles the originating pool slot without a metadata callback"));
      ZuCheckRT(instance->send(ctl->data(), ctl->length),
        log_("retained backing can be resubmitted unchanged after CQ retirement"));
      other = nullptr;
      // Close below while recycled remains application-owned, then release it through
      // the still-live Zi provider. No connection object is needed for that free.
      bool closed = closeInstance(instance, error);
      ZuCheckRT(closed, log_("kernel retirement completes independently of the application: ", error));
      ZmAssert_(closed);
      ZuCheckRT(provider->pool.used() == 1 && recycled && recycled->data_() == canonical &&
          control->pool.used() == 1 && ctl->data_() == heldOrigin &&
          !memcmp(ctl->data(), retainedBytes, 64),
        log_("kernel close preserves caller-retained original backing and bytes"));
      recycled = nullptr;
      ctl = nullptr;
      ZuCheckRT(!provider->pool.used() && !control->pool.used() && !provider->alloc(),
        log_("final Zi release returns exactly once to the retired originating pool"));
    }
  }
  if (instance) ZmAssert_(closeInstance(instance, error));
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  if (geteuid() || !getenv("ZXDP_TEST_IFINDEX")) {
    std::cout << "1..0 # SKIP requires privileged fixture-owned interface\n";
    return 0;
  }
  ZuTestMain();
  ZuTestCall(testProvider);
}
