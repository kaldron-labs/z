//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Interface geometry and direct IP carriage over Ethernet.

#ifndef ZutcpIP_HH
#define ZutcpIP_HH

#ifndef ZutcpLib_HH
#include <zlib/ZutcpLib.hh>
#endif

#include <stdint.h>

#include <zlib/ZuSpan.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZiIP.hh>

namespace Zutcp {

// Linux owns interface configuration. Query before creating Zi adapters so
// their framing geometry is stable; validate again against the running backend.
ZutcpAPI unsigned interfaceMTU(unsigned interface, ZtString<> &error);

struct IPPath {
  ZiIP		local, remote;
  uint8_t	localMAC[6], remoteMAC[6];
  unsigned	mtu;
};

// Cold setup on the control caller: read the kernel's route constrained to this
// interface and its static next-hop neighbor. Preserve an explicit local IP;
// otherwise use the kernel-selected source. Publish only on success. No ARP/ND.
ZutcpAPI bool resolveIPPath(unsigned interface, IPPath &, ZtString<> &error);

struct IPView {
  ZiIP		local, remote;
  ZuBSpan	payload;
  unsigned	protocol, ecn;
};

// The payload is already at frame + ipHead(type). Write only framing and
// Ethernet padding, without moving payload or allocating backing. Return the
// complete wire length, or zero without changing the frame on invalid geometry.
inline unsigned ipHead(ZiIPType::T type) {
  switch (type) {
    case ZiIPType::V4: return 14 + 20;
    case ZiIPType::V6: return 14 + 40;
    default: return 0;
  }
}
ZutcpAPI unsigned writeIP(
  uint8_t *frame, unsigned capacity, const IPPath &, unsigned protocol,
  unsigned payload, unsigned ecn = 0);

// This layer validates IP framing only; payload integrity belongs to the transport.
// No UDP header, transport shim or payload checksum is added.
// Output borrows the input packet. IPv4 options and common IPv6 extension
// headers are traversed in place; fragmented datagrams need IP reassembly and
// are not admitted as complete transport packets.
ZutcpAPI bool readIP(ZuBSpan frame, IPView &);

struct TCPHeader {
  uint16_t	srcPort = 0, dstPort = 0;
  uint32_t	seq = 0, ack = 0;
  uint16_t	window = 0, urgent = 0;
  uint8_t	flags = 0;
};

struct TCPView {
  TCPHeader	header;
  ZuBSpan	options, payload;
};

inline unsigned tcpHead(ZiIPType::T type) {
  unsigned head = ipHead(type);
  return head ? head + 20 : 0;
}

inline unsigned tcpPayload(unsigned mtu, ZiIPType::T type) {
  unsigned head = tcpHead(type);
  return head && mtu > head - 14 ? mtu - (head - 14) : 0;
}

// Construct a base TCP header over payload already at frame + tcpHead(type).
// Includes IP framing and the mandatory TCP pseudo-header checksum. No payload
// move or allocation; invalid geometry leaves the frame unchanged. Sequence,
// ACK, flags and window semantics belong to the connection, not this codec.
ZutcpAPI unsigned writeTCP(
  uint8_t *frame, unsigned capacity, const IPPath &, const TCPHeader &,
  unsigned payload, unsigned ecn = 0);

// Consume the original IP payload, including any TCP options in its checksum.
// The returned options/payload borrow that storage. Publish only on success.
ZutcpAPI bool readTCP(const IPView &, TCPView &);

} // namespace Zutcp

#endif /* ZutcpIP_HH */
