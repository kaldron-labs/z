//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z User Management library main header

#include <zlib/ZumLib.hh>

#include <zlib/ZmVHeap.hh>

// Zum's asynchronous state varies materially in size.  Start at the smallest
// size class and use the standard jumbo threshold; larger objects still work
// but are not retained in the recycling allocator.
using ZumObjectHeap = ZmVHeap<
  "Zum.Object", 0, ZmVHeap_DefltMax, alignof(max_align_t)>;

void *ZumObjectAlloc::operator new(size_t size)
{
  return ZumObjectHeap::valloc(size);
}

void ZumObjectAlloc::operator delete(void *ptr)
{
  ZumObjectHeap::vfree(ptr);
}

void ZumObjectAlloc::operator delete(void *ptr, size_t)
{
  ZumObjectHeap::vfree(ptr);
}

ZumExtern const char ZumLib[] = "@(#) Z User Management Library v" Z_VERNAME;
