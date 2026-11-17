//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zutcp.hh>

using namespace ZuTestUtil;

struct RetainedBuf : Zi::IOBuf {
  using Zi::IOBuf::IOBuf;
  ~RetainedBuf() { ++*static_cast<unsigned *>(owner); }
};

static void testConnectionRetention()
{
  ZuTestScope(testConnectionRetention);
  ZiMultiplex mx;
  struct App : Zutcp::Hub<App> { };
  App hub;
  ZuCHECK(hub.init(Zutcp::HubParams{&mx}.rxRetainFramesPerCxn(2)),
    "initialize the configured per-connection retention bound");
  unsigned retired = 0;
  auto allocator = mx.allocator<Zi::IOBufType::App, RetainedBuf, 64>();
  auto packet = [&allocator, &retired] {
    auto buf = allocator->allocBuf(&retired, 1);
    return buf;
  };
  Zi::IOQueue first, second;
  auto oldest = packet();
  hub.retainRx(oldest, first);
  auto newest = packet();
  hub.retainRx(newest, first);
  auto independent = packet();
  hub.retainRx(independent, second);
  ZuCHECK(first.count_() == 2 && second.count_() == 1 &&
      first.headPtr() == newest.ptr() && first.tailPtr() == oldest.ptr(),
    "each list has its own count, newest head and oldest tail");
  auto next = packet();
  unsigned refs = oldest->refCount();
  hub.retainRx(next, first);
  ZuCHECK(first.count_() == 2 && first.headPtr() == next.ptr() &&
      first.tailPtr() == newest.ptr() && oldest->refCount() + 1 == refs &&
      second.headPtr() == independent.ptr() && second.count_() == 1 && !retired,
    "overflow releases only the oldest list reference and preserves application aliases");
  oldest = nullptr;
  ZuCHECK(retired == 1, "the evicted buffer retires when its remaining alias releases");
  newest = next = independent = nullptr;
  first.clean();
  ZuCHECK(retired == 3 && second.count_() == 1,
    "draining one connection releases its retained buffers without draining another");
  second.clean();
  ZuCHECK(retired == 4, "teardown releases the last connection's retained buffer");
  hub.final();
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testConnectionRetention);
}
