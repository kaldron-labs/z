//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Resolve a Linux netdev to the mlx5 RDMA port in its namespace.

#ifndef ZpktMlx_HH
#define ZpktMlx_HH

#include <zlib/ZuUnion.hh>
#include <zlib/ZePlatform.hh>

struct nlmsghdr;

namespace Zpkt::Mlx {

struct Port {
  unsigned	device, number;
};

int matchPort(const nlmsghdr &, Port &, unsigned ifindex, unsigned device);
ZuUnion<Port, ZeException> findPort(unsigned ifindex);

} // namespace Zpkt::Mlx
#endif /* ZpktMlx_HH */
