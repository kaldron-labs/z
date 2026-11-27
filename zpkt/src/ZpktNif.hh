//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Private kernel-interface query and rtnetlink transport.

#ifndef ZpktNif_HH
#define ZpktNif_HH

#ifndef ZpktLib_HH
#include <zlib/ZpktLib.hh>
#endif

#include <linux/if_ether.h>
#include <linux/rtnetlink.h>

#include <zlib/ZtArray.hh>
#include <zlib/Zpkt.hh>

namespace Zpkt {

// VLAN tags extend the Ethernet header outside the L3 MTU.
inline bool frameFitsMTU(const uint8_t *data, unsigned length, unsigned mtu)
{
  unsigned header = ETH_HLEN;
  if (length < header) return false;
  for (;;) {
    unsigned type = (unsigned(data[header - 2]) << 8) | data[header - 1];
    if (type != ETH_P_8021Q && type != ETH_P_8021AD) return length - header <= mtu;
    if (length - header < 4) return false;
    header += 4;
  }
}

using NifReply = ZtArray<uint8_t, ZtArrayHeapID<"Zpkt.Nif.Reply">>;
// Last close of the returned socket retires the netdev ingress guard.
ZpktAPI int nifClaimIngress(unsigned ifindex); // owner fd or negative errno
ZpktAPI ZuUnion<int, ZeException> nifOpen();
ZpktAPI ZuUnion<NifInfo, ZeException> nifQuery(ZuCSpan name);
ZpktAPI ZuUnion<void, ZeException> nifTransact(
  nlmsghdr &request, NifReply *reply = nullptr,
  bool *rejected = nullptr, ZeError *native = nullptr, int fd = -1);

} // namespace Zpkt
#endif /* ZpktNif_HH */
