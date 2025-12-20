//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

/* test program */

#include <iostream>

#include <zlib/ZuBox.hh>

#include <zlib/ZmAlloc.hh>
#include <zlib/ZmVHeap.hh>

void test()
{
  auto self = ZmSelf();
  std::cout << "stack: " << ZuBoxPtr(self->stackAddr()).hex() << " +" << ZuBoxed(self->stackSize()).hex() << '\n';
  unsigned n = ZmStackAvail();
  std::cout << "stack available: " << ZuBoxed(n).hex() << '\n';
  auto ptr = ZmAlloc(uint8_t, n/3);
  std::cout << "ZmAlloc(" << ZuBoxed(n/3).hex() << "): " << ZuBoxPtr(ptr.ptr).hex() << '\n';
  std::cout << "stack available: " << ZuBoxed(ZmStackAvail()).hex() << '\n';
  auto ptr2 = ZmAlloc(uint8_t, n);
  std::cout << "ZmAlloc(" << ZuBoxed(n).hex() << "): " << ZuBoxPtr(ptr2.ptr).hex() << '\n';
  std::cout << "stack available: " << ZuBoxed(ZmStackAvail()).hex() << '\n';
  // uint8_t *ptr = static_cast<uint8_t *>(ZmAlloc(n/3));
  // uint8_t *ptr2 = static_cast<uint8_t *>(ZmAlloc(n));
}

int main(int argc, char **argv)
{
  test();
}
