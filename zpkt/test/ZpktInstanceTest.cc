//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZiMultiplex.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZiIOBuf.hh>

#include <zlib/Zpkt.hh>
#include <zlib/ZpktPool.hh>

using namespace ZuTestUtil;

static void testPools()
{
  ZuTestScope(testPools);
  ZmRef<Zi::IOBufPool> rx = new Zpkt::Xdp::BufPool{Zi::IOBufType::Rx, 2, 1514};
  ZmRef<Zi::IOBufPool> tx = new Zpkt::Xdp::BufPool{Zi::IOBufType::TxTCP, 1, 128};
  ZuCHECK(rx->count == 2 && rx->size == 1514 && tx->count == 1 && tx->size == 128,
    "private pools preserve geometry without opening a kernel interface");
  ZuCHECK(!rx->alloc() && !tx->alloc(), "unbacked pools cannot allocate packets");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testPools);
}
