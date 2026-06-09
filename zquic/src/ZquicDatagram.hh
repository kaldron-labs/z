//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC datagram ownership and endpoint diagnostics

#ifndef ZquicDatagram_HH
#define ZquicDatagram_HH

#ifndef ZquicLib_HH
#include <zlib/ZquicLib.hh>
#endif

#include <zlib/ZmAtomic.hh>

#include <zlib/ZquicBuf.hh>
#include <zlib/ZquicSock.hh>

namespace Zquic {

struct Datagram {
  ZmRef<ZiIOBuf>	buf;
  ZiSockAddr		addr;

  Datagram() = default;
  Datagram(ZmRef<ZiIOBuf> buf_, ZiSockAddr addr_) :
    buf{ZuMv(buf_)}, addr{ZuMv(addr_)} { }
};

struct EndpointDiag {
  ZmAtomic<uint64_t>	packetRxBufAllocs = 0;
  ZmAtomic<uint64_t>	packetTxBufAllocs = 0;
  ZmAtomic<uint64_t>	endpointCxnAllocs = 0;
  ZmAtomic<uint64_t>	datagramsRx = 0;
  ZmAtomic<uint64_t>	datagramsTx = 0;
  ZmAtomic<uint64_t>	datagramDrops = 0;
  ZmAtomic<uint64_t>	bytesRx = 0;
  ZmAtomic<uint64_t>	bytesTx = 0;
  ZmAtomic<uint64_t>	openFailures = 0;
  ZmAtomic<uint64_t>	ioErrors = 0;
};

} // namespace Zquic

#endif /* ZquicDatagram_HH */
