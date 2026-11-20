//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmThread.hh>

#include <zlib/ZtlsCxn.hh>
#include <zlib/ZtlsHMAC.hh>
#include <zlib/ZtlsMD.hh>
#include <zlib/ZtlsRandom.hh>

using namespace ZuTestUtil;

static bool growRecordBuffer(bool tx)
{
  uint8_t	small[1]; // Force the first reservation to grow the record buffer.
  Ztls_::Buffer buffer;
  if (tx)
    Ztls_::buffer_init_tx(&buffer, small, sizeof(small));
  else
    Ztls_::buffer_init_rx(&buffer, small, sizeof(small));
  ZuGuard dispose{[&buffer]() { Ztls_::buffer_dispose(&buffer); }};
  buffer.base[0] = 0x5a;
  buffer.off = 1;
  if (Ztls_::buffer_reserve(&buffer, 1, tx ? 1 : 0) || !buffer.isAllocated ||
    buffer.base == small || buffer.base[0] != 0x5a || buffer.off != 1)
    return false;
  auto capacity = buffer.capacity;
  if (Ztls_::buffer_reserve(&buffer, capacity, tx ? 1 : 0) || buffer.capacity <= capacity ||
    buffer.base[0] != 0x5a || buffer.off != 1)
    return false;
  return true;
}

static void standaloneCrypto()
{
  ZuTestScope(standaloneCrypto);
  Ztls::Random random;
  uint8_t      bytes[Ztls::MD<>::Size];
  ZuCheck(random.init());
  ZuCheck(random.random(bytes));
  Ztls::MD<> md;
  md.update("standalone digest");
  md.finish(bytes);
  Ztls::HMAC<> hmac;
  hmac.start("standalone key");
  hmac.update("standalone MAC");
  hmac.finish(bytes);
}

static void concurrentProtocolInit()
{
  ZuTestScope(concurrentProtocolInit);
  ZmSemaphore ready, go;
  bool	      firstOK = false, secondOK = false;
  auto	      initialize = [&ready, &go](bool &ok)
  {
    ready.post();
    go.wait();
    try {
      Ztls::init();
      ok = true;
    } catch (...) {
      ok = false;
    }
  };
  ZmThread first{[&initialize, &firstOK]() { initialize(firstOK); }};
  ZmThread second{[&initialize, &secondOK]() { initialize(secondOK); }};
  bool	   started = bool(first) && bool(second);
  if (first) ready.wait();
  if (second) ready.wait();
  go.post();
  go.post();
  if (first) first.join();
  if (second) second.join();
  ZuCheck(started);
  ZuCheck(firstOK && secondOK);
  Ztls::init();
}

static void recordBuffers()
{
  ZuTestScope(recordBuffers);
  ZuCheck(growRecordBuffer(false));
  ZuCheck(growRecordBuffer(true));
}

static void connectionAlignment()
{
  ZuTestScope(connectionAlignment);
  Ztls::Cxn connection;
  auto	    rx = reinterpret_cast<uintptr_t>(&connection.state);
  auto	    tx = reinterpret_cast<uintptr_t>(&connection.txTraffic);
  ZuCheck(rx % Zm::CacheLineSize == 0);
  ZuCheck(tx % Zm::CacheLineSize == 0);
  ZuCheck(tx > rx && tx - rx >= Zm::CacheLineSize);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(standaloneCrypto);
  ZuTestCall(concurrentProtocolInit);
  ZuTestCall(recordBuffers);
  ZuTestCall(connectionAlignment);
  return 0;
}
