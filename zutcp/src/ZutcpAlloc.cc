//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZmAssert.hh>

#include <zlib/ZutcpAlloc.hh>

namespace Zutcp {

PacketLease::~PacketLease() { reset(); }

void PacketLease::adopt(Zi::IOBufAllocator *allocator, uint8_t *data, unsigned size)
{
  ZmAssert_(allocator && data);
  if (m_data) {
    ZmAssert_(m_allocator == allocator);
    return;
  }
  m_allocator = allocator;
  m_data = data;
  m_size = size;
}

void PacketLease::reset()
{
  auto data = m_data;
  m_data = nullptr;
  if (data) m_allocator->free(data, m_size, true);
  m_allocator = nullptr;
  m_size = 0;
}

} // namespace Zutcp
