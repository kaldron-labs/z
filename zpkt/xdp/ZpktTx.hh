//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Private AF_XDP checksum-offload discovery and native submission metadata.

#ifndef ZpktTx_HH
#define ZpktTx_HH

#ifndef ZpktXDPLib_HH
#include <zlib/ZpktXDPLib.hh>
#endif

#ifndef ZpktXDPGenlReplySize
#define ZpktXDPGenlReplySize 8192
#endif

#include <stdint.h>
#include <linux/if_xdp.h>

#include <zlib/ZuSpan.hh>

namespace Zpkt::Xdp {

// No payload traversal or checksum calculation: only native header offsets.
struct ZpktXDPAPI TxTCP {
  unsigned wire = 0;
  uint16_t start = 0;

  bool init(const uint8_t *, unsigned);
  void metadata(uint8_t *) const;
};

// Bounded native-endian scalar attributes from the kernel's generic-netlink reply.
ZpktXDPAPI uint64_t genlScalar(ZuBSpan, unsigned family, unsigned sequence,
  unsigned attribute, unsigned size);

// Cold netdev generic-netlink query. Missing/unsupported discovery means false.
ZpktXDPAPI bool tcpChksumAvailable(unsigned ifindex);

} // namespace Zpkt::Xdp
#endif /* ZpktTx_HH */
