//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuBox.hh>
#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmAlloc.hh>
#include <zlib/ZmVHeap.hh>

using namespace ZuTestUtil;

template <auto &ZuTest_scope>
void test()
{
  auto self = ZmSelf();
  ZuCheck(self);
  log("stack: ", ZuBoxPtr(self->stackAddr()).hex(), " +", ZuBoxed(self->stackSize()).hex());
  unsigned n = ZmStackAvail();
  log("stack available: ", ZuBoxed(n).hex());
  auto ptr = ZmAlloc(uint8_t, n/3);
  ZuCheck(ptr.ptr);
  log("ZmAlloc(", ZuBoxed(n/3).hex(), "): ", ZuBoxPtr(ptr.ptr).hex());
  log("stack available: ", ZuBoxed(ZmStackAvail()).hex());
  auto ptr2 = ZmAlloc(uint8_t, n);
  ZuCheck(ptr2.ptr);
  log("ZmAlloc(", ZuBoxed(n).hex(), "): ", ZuBoxPtr(ptr2.ptr).hex());
  log("stack available: ", ZuBoxed(ZmStackAvail()).hex());
  // uint8_t *ptr = static_cast<uint8_t *>(ZmAlloc(n/3));
  // uint8_t *ptr2 = static_cast<uint8_t *>(ZmAlloc(n));
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  test<ZuTest_scope>();
}
