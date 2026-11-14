//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmThread.hh>

#include <zlib/ZtlsHMAC.hh>
#include <zlib/ZtlsMD.hh>
#include <zlib/ZtlsPico.hh>
#include <zlib/ZtlsRandom.hh>

using namespace ZuTestUtil;

static bool bufferHooks(bool tx)
{
  uint8_t small[1]; // Force the first reservation to use the installed hook.
  ptls_buffer_t buffer;
  if (tx) ptls_buffer_init_tx(&buffer, small, sizeof(small));
  else ptls_buffer_init_rx(&buffer, small, sizeof(small));
  ZuGuard dispose{[&buffer]() { ptls_buffer_dispose(&buffer); }};
  buffer.base[0] = 0x5a;
  buffer.off = 1;
  if (ptls_buffer_reserve(&buffer, 1, tx ? 1 : 0) ||
      !buffer.is_allocated || buffer.base == small ||
      buffer.base[0] != 0x5a || buffer.off != 1)
    return false;
  auto capacity = buffer.capacity;
  if (ptls_buffer_reserve(&buffer, capacity, tx ? 1 : 0) ||
      buffer.capacity <= capacity || buffer.base[0] != 0x5a || buffer.off != 1)
    return false;
  return true;
}

struct Hooks {
  decltype(ptls_buffer_alloc) alloc;
  decltype(ptls_buffer_free) free;
};

static void standaloneCrypto(const Hooks &original)
{
  ZuTestScope(standaloneCrypto);
  Ztls::Random random;
  uint8_t bytes[Ztls::MD<>::Size];
  ZuCheck(random.init());
  ZuCheck(random.random(bytes));
  Ztls::MD<> md;
  md.update("standalone digest");
  md.finish(bytes);
  Ztls::HMAC<> hmac;
  hmac.start("standalone key");
  hmac.update("standalone MAC");
  hmac.finish(bytes);
  ZuCheck(ptls_buffer_alloc == original.alloc);
  ZuCheck(ptls_buffer_free == original.free);
}

static void concurrentProtocolInit(const Hooks &original)
{
  ZuTestScope(concurrentProtocolInit);
  ZmSemaphore ready, go;
  bool firstOK = false, secondOK = false;
  auto initialize = [&ready, &go](bool &ok) {
    ready.post();
    go.wait();
    ok = Ztls::Pico::init();
  };
  ZmThread first{[&initialize, &firstOK]() { initialize(firstOK); }};
  ZmThread second{[&initialize, &secondOK]() { initialize(secondOK); }};
  bool started = bool(first) && bool(second);
  if (first) ready.wait();
  if (second) ready.wait();
  go.post();
  go.post();
  if (first) first.join();
  if (second) second.join();
  ZuCheck(started);
  ZuCheck(firstOK && secondOK);
  ZuCheck(ptls_buffer_alloc != original.alloc);
  ZuCheck(ptls_buffer_free != original.free);
  auto installedAlloc = ptls_buffer_alloc;
  auto installedFree = ptls_buffer_free;
  ZuCheck(Ztls::Pico::init());
  Ztls::Pico::install();
  ZuCheck(ptls_buffer_alloc == installedAlloc);
  ZuCheck(ptls_buffer_free == installedFree);
}

static void installedHooks()
{
  ZuTestScope(installedHooks);
  Ztls::Pico::reset_stats();
  ZuCheck(bufferHooks(false));
  ZuCheck(bufferHooks(true));
  auto stats = Ztls::Pico::stats();
  ZuCheck(stats.internal_alloc != 0);
  ZuCheck(stats.internal_free != 0);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  Hooks original{ptls_buffer_alloc, ptls_buffer_free};
  ZuTestCall(standaloneCrypto, original);
  ZuTestCall(concurrentProtocolInit, original);
  ZuTestCall(installedHooks);
  return 0;
}
