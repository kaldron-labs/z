//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZutcpIP.hh>

using namespace ZuTestUtil;

static Zutcp::IPPath path(bool v6)
{
  return {ZiIP{v6 ? "2001:db8::1" : "192.0.2.1"},
    ZiIP{v6 ? "2001:db8::2" : "192.0.2.2"},
    {2, 0, 0, 0, 0, 1}, {2, 0, 0, 0, 0, 2}, 1500};
}

// Independent direct-IP vectors: protocol 253 is a fixture value, not a
// production transport choice. There are no ports, UDP header or payload shim.
static constexpr uint8_t v4[] = {
  0x02,0,0,0,0,2, 0x02,0,0,0,0,1, 0x08,0x00,
  0x45,0x03,0,0x19,0,0,0x40,0,0x40,0xfd,0xb5,0xe1,
  0xc0,0,2,1,0xc0,0,2,2, 'a','b','c','d','e',
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};
static constexpr uint8_t v6[] = {
  0x02,0,0,0,0,2, 0x02,0,0,0,0,1, 0x86,0xdd,
  0x60,0x30,0,0,0,5,0xfd,0x40,
  0x20,1,0x0d,0xb8,0,0,0,0,0,0,0,0,0,0,0,1,
  0x20,1,0x0d,0xb8,0,0,0,0,0,0,0,0,0,0,0,2,
  'a','b','c','d','e',0
};

static void testVectors()
{
  ZuTestScope(testVectors);
  for (bool ipv6 : {false, true}) {
    auto route = path(ipv6);
    auto expected = ipv6 ? v6 : v4;
    unsigned size = ipv6 ? sizeof(v6) : sizeof(v4);
    uint8_t frame[1600];
    unsigned head = Zutcp::ipHead(route.local.type());
    memcpy(frame + head, "abcde", 5);
    unsigned wire = Zutcp::writeIP(frame, sizeof(frame), route, 253, 5, 3);
    ZuCHECK(wire == size && !memcmp(frame, expected, size), "independent wire vector");
    Zutcp::IPView view;
    bool read = Zutcp::readIP({frame, wire}, view);
    ZuCHECK(read && view.local == route.remote && view.remote == route.local &&
	view.payload.data() == frame + head && view.payload.length() == 5 && view.ecn == 3,
      "receive view borrows the original payload and reverses endpoint roles");
    bool bounds = true;
    for (unsigned n = 0; n < head + 5; ++n)
      bounds &= !Zutcp::readIP({frame, n}, view);
    ZuCHECK(bounds, "all truncated framing/payload extents are rejected");
    ZuCHECK(view.protocol == 253, "IP view reports the carried protocol without interpreting it");
    frame[head + 4] ^= 1;
    ZuCHECK(Zutcp::readIP({frame, wire}, view) && view.payload[4] == ('e' ^ 1),
      "IP framing borrows opaque payload; transport integrity is a separate obligation");
    if (!ipv6) {
      memcpy(frame, expected, size);
      frame[22] ^= 1;
      ZuCHECK(!Zutcp::readIP({frame, wire}, view), "IPv4 header checksum corruption");
      memcpy(frame, expected, size);
      frame[29] ^= 1;
      ZuCHECK(!Zutcp::readIP({frame, wire}, view), "IPv4 address checksum corruption");
    }
  }
}

static void testGeometry()
{
  ZuTestScope(testGeometry);
  uint8_t frame[1600], saved[1600];
  for (bool ipv6 : {false, true}) {
    auto route = path(ipv6);
    unsigned head = Zutcp::ipHead(route.local.type());
    bool valid = true;
    for (unsigned n = 0; n <= route.mtu - head + 14; ++n) {
      for (unsigned i = 0; i < n; ++i) frame[head + i] = (i + n) ^ (i>>3);
      auto wire = Zutcp::writeIP(frame, sizeof(frame), route, 253, n, n & 3);
      Zutcp::IPView view;
      valid &= wire && Zutcp::readIP({frame, wire}, view) &&
	view.payload.data() == frame + head && view.payload.length() == n &&
	view.ecn == (n & 3) && view.protocol == 253;
    }
    ZuCHECK(valid, "zero/odd/even payloads through full MTU preserve their original span");
    memset(frame, 0xa5, sizeof(frame));
    memcpy(saved, frame, sizeof(frame));
    ZuCHECK(!Zutcp::writeIP(frame, 20, route, 253, 5) &&
	!Zutcp::writeIP(frame, sizeof(frame), route, 253, route.mtu - head + 15) &&
	!Zutcp::writeIP(frame, sizeof(frame), route, 256, 5) &&
	!memcmp(frame, saved, sizeof(frame)), "failed framing leaves packet untouched");
    route.remote = path(!ipv6).remote;
    ZuCHECK(!Zutcp::writeIP(frame, sizeof(frame), route, 253, 5), "mixed address families");
  }
  ZuCHECK(!Zutcp::ipHead(ZiIPType::Null), "unset address has no framing geometry");
}

static void testOptions()
{
  ZuTestScope(testOptions);
  uint8_t frame[128]{};
  Zutcp::IPView view;
  memcpy(frame, v4, 34);
  memcpy(frame + 38, v4 + 34, 5);
  frame[14] = 0x46; frame[17] = 0x1d;
  frame[24] = 0xb4; frame[25] = 0xdd;
  ZuCHECK(Zutcp::readIP({frame, 60}, view) && view.payload.data() == frame + 38 &&
      view.payload.length() == 5 && view.protocol == 253,
    "IPv4 options change the payload offset without copying");
  frame[20] |= 0x20;
  frame[24] = 0x94; // valid header checksum after setting More Fragments
  ZuCHECK(!Zutcp::readIP({frame, 60}, view), "IPv4 fragments are not complete packets");
  for (unsigned extension : {0u, 43u, 44u, 60u}) {
    memcpy(frame, v6, 54);
    memset(frame + 54, 0, 8);
    memcpy(frame + 62, v6 + 54, 5);
    frame[19] = 13; frame[20] = extension; frame[54] = 253;
    ZuCHECK(Zutcp::readIP({frame, 67}, view) && view.payload.data() == frame + 62 &&
	view.payload.length() == 5 && view.protocol == 253,
      "IPv6 extension/atomic-fragment payload offset");
    frame[55] = 255;
    if (extension != 44)
      ZuCHECK(!Zutcp::readIP({frame, 67}, view), "extension length beyond packet");
    if (extension == 44) {
      frame[57] = 1;
      ZuCHECK(!Zutcp::readIP({frame, 67}, view), "IPv6 fragments require reassembly");
    }
  }

}

// Checksums computed independently from the RFC 9293 pseudo-header and segment.
static constexpr uint8_t tcpBase[] = {
  0x30,0x39,0x01,0xbb, 0x12,0x34,0x56,0x78, 0x90,0xab,0xcd,0xef,
  0x50,0x18,0x10,0x00, 0,0,0,0, 'a','b','c','d','e'
};

static void testTCPVectors()
{
  ZuTestScope(testTCPVectors);
  for (bool ipv6 : {false, true}) {
    auto route = path(ipv6);
    uint8_t frame[128];
    unsigned head = Zutcp::tcpHead(route.local.type());
    memcpy(frame + head, "abcde", 5);
    Zutcp::TCPHeader header{12345, 443, 0x12345678, 0x90abcdef, 4096, 0, 0x18};
    auto wire = Zutcp::writeTCP(frame, sizeof(frame), route, header, 5, 3);
    uint8_t expected[sizeof(tcpBase)];
    memcpy(expected, tcpBase, sizeof(expected));
    expected[16] = ipv6 ? 0x21 : 0xf8;
    expected[17] = ipv6 ? 0x50 : 0xc0;
    ZuCHECK(wire && !memcmp(frame + head - 20, expected, sizeof(expected)),
      "TCP header and odd payload match independent IPv4/IPv6 checksum vectors");
    Zutcp::IPView ip;
    Zutcp::TCPView tcp;
    ZuCHECK(Zutcp::readIP({frame, wire}, ip) && Zutcp::readTCP(ip, tcp) &&
	ip.protocol == IPPROTO_TCP && ip.ecn == 3 &&
	tcp.header.srcPort == 12345 && tcp.header.dstPort == 443 &&
	tcp.header.seq == 0x12345678 && tcp.header.ack == 0x90abcdef &&
	tcp.header.window == 4096 && tcp.header.flags == 0x18 && !tcp.header.urgent &&
	!tcp.options.length() && tcp.payload.data() == frame + head &&
	tcp.payload.length() == 5, "borrowed TCP view preserves header fields and payload");
    bool rejects = true;
    for (unsigned i = 0; i < sizeof(expected); ++i) {
      frame[head - 20 + i] ^= 1;
      rejects &= !Zutcp::readTCP(ip, tcp);
      frame[head - 20 + i] ^= 1;
    }
    ZuCHECK(rejects, "every TCP header/payload byte is covered by its checksum");
    ip.local = route.local;
    ZuCHECK(!Zutcp::readTCP(ip, tcp), "pseudo-header protects the IP destination");
    ip.local = route.remote;
    ip.remote = route.remote;
    ZuCHECK(!Zutcp::readTCP(ip, tcp), "pseudo-header protects the IP source");
    ip.remote = route.local;
    ip.protocol = 253;
    ZuCHECK(!Zutcp::readTCP(ip, tcp), "other IP protocols are not TCP segments");
    ip.protocol = IPPROTO_TCP;
    bool truncated = true;
    for (unsigned n = 0; n < sizeof(expected); ++n) {
      ip.payload = {frame + head - 20, n};
      truncated &= !Zutcp::readTCP(ip, tcp);
    }
    ZuCHECK(truncated && tcp.payload.data() == frame + head && tcp.payload.length() == 5,
      "truncated TCP segments fail without replacing the last successful view");
  }
}

static void testTCPGeometry()
{
  ZuTestScope(testTCPGeometry);
  uint8_t frame[1600], saved[1600];
  for (bool ipv6 : {false, true}) {
    auto route = path(ipv6);
    auto head = Zutcp::tcpHead(route.local.type());
    bool valid = true;
    Zutcp::TCPHeader header{65535, 1, 0xffffffff, 0, 65535, 7, 0x30};
    for (unsigned n = 0; n <= route.mtu - head + 14; ++n) {
      for (unsigned i = 0; i < n; ++i) frame[head + i] = (i + n) ^ (i>>3);
      auto wire = Zutcp::writeTCP(frame, sizeof(frame), route, header, n);
      Zutcp::IPView ip;
      Zutcp::TCPView tcp;
      valid &= wire && Zutcp::readIP({frame, wire}, ip) && Zutcp::readTCP(ip, tcp) &&
	tcp.payload.data() == frame + head && tcp.payload.length() == n &&
	tcp.header.seq == 0xffffffff && tcp.header.urgent == 7;
    }
    ZuCHECK(valid, "TCP zero/odd/even payloads through full family-specific MTU");
    memset(frame, 0xa5, sizeof(frame));
    memcpy(saved, frame, sizeof(frame));
    ZuCHECK(!Zutcp::writeTCP(frame, head + 4, route, header, 5) &&
	!Zutcp::writeTCP(frame, sizeof(frame), route, header, route.mtu - head + 15) &&
	!Zutcp::writeTCP(frame, sizeof(frame), route, header, ~0u) &&
	!memcmp(frame, saved, sizeof(frame)), "failed TCP framing leaves backing untouched");
    route.remote = path(!ipv6).remote;
    ZuCHECK(!Zutcp::writeTCP(frame, sizeof(frame), route, header, 0) &&
	!memcmp(frame, saved, sizeof(frame)), "mixed-family TCP write is rejected atomically");
  }
  ZuCHECK(!Zutcp::tcpHead(ZiIPType::Null), "unset family has no TCP framing geometry");
  ZuCHECK(Zutcp::tcpPayload(1500, ZiIPType::V4) == 1460 &&
      Zutcp::tcpPayload(1500, ZiIPType::V6) == 1440 &&
      !Zutcp::tcpPayload(40, ZiIPType::V4) &&
      !Zutcp::tcpPayload(1500, ZiIPType::Null), "TCP budget uses actual IP/TCP headers");
}

static void testTCPOptions()
{
  ZuTestScope(testTCPOptions);
  for (bool ipv6 : {false, true}) {
    auto route = path(ipv6);
    // Deliberately unaligned segment with an independent MSS option vector.
    uint8_t storage[30];
    auto segment = storage + 1;
    memcpy(segment, tcpBase, 20);
    segment[12] = 0x60;
    segment[16] = ipv6 ? 0x09 : 0xe1;
    segment[17] = ipv6 ? 0x94 : 0x04;
    memcpy(segment + 20, "\x02\x04\x05\xb4" "abcde", 9);
    Zutcp::IPView ip{route.remote, route.local, {segment, 29}, IPPROTO_TCP, 0};
    Zutcp::TCPView tcp;
    ZuCHECK(Zutcp::readTCP(ip, tcp) && tcp.options.data() == segment + 20 &&
	tcp.options.length() == 4 && tcp.payload.data() == segment + 24 &&
	tcp.payload.length() == 5, "TCP options are checksummed and borrowed in place");
    segment[23] ^= 1;
    ZuCHECK(!Zutcp::readTCP(ip, tcp), "TCP checksum covers options");
    segment[23] ^= 1;
    segment[21] = 3;
    tcp.header.options = {.flags = Zutcp::TCPOptions::TS, .tsval = 42};
    ++segment[23]; // The length decrement and MSS increment preserve the checksum sum.
    ZuCHECK(!Zutcp::readTCP(ip, tcp) && tcp.header.options.tsval == 42,
      "a checksum-valid malformed known option must fail before publishing header values");
    segment[21] = 4;
    --segment[23];
    segment[12] = 0x40;
    ZuCHECK(!Zutcp::readTCP(ip, tcp), "TCP data offset below base header");
    segment[12] = 0xf0;
    ZuCHECK(!Zutcp::readTCP(ip, tcp), "TCP data offset beyond received segment");
  }
}

static void testTCPOptionWire()
{
  ZuTestScope(testTCPOptionWire);
  const uint8_t expected[] = {8,10,1,2,3,4,0,0,0,0,0,0};
  for (bool ipv6 : {false, true}) {
    auto route = path(ipv6);
    Zutcp::TCPHeader header{.srcPort = 12345, .dstPort = 443, .seq = 7,
      .ack = 9, .window = 400, .flags = 0x18,
      .options = {.flags = Zutcp::TCPOptions::TS, .tsval = 0x01020304}};
    uint8_t frame[1600];
    memset(frame, 0xa5, sizeof(frame));
    unsigned base = Zutcp::tcpHead(route.local.type());
    unsigned head = Zutcp::tcpHead(route.local.type(), header);
    memcpy(frame + head, "abcde", 5);
    auto original = frame + head;
    unsigned wire = Zutcp::writeTCP(frame, sizeof(frame), route, header, 5);
    Zutcp::IPView ip;
    Zutcp::TCPView tcp;
    bool parsed = wire && Zutcp::readIP({frame, wire}, ip) && Zutcp::readTCP(ip, tcp);
    ZuCHECK(parsed && head == base + sizeof(expected) && frame[base - 8] == 0x80 &&
	!memcmp(frame + base, expected, sizeof(expected)) &&
	tcp.header.options.flags == Zutcp::TCPOptions::TS &&
	tcp.header.options.tsval == 0x01020304 && !tcp.header.options.tsecr &&
	tcp.payload.data() == original && tcp.payload.length() == 5 &&
	!memcmp(original, "abcde", 5),
      "actual option bytes, data offset and checksum must retain the original payload address");
    uint8_t saved[sizeof(frame)];
    memcpy(saved, frame, sizeof(frame));
    bool refused = !Zutcp::writeTCP(frame, head + 4, route, header, 5);
    header.options.sackCount = 4;
    for (unsigned i = 0; i < 4; ++i) header.options.sack[i] = {10*i, 10*i + 5};
    refused &= !Zutcp::tcpHead(route.local.type(), header) &&
      !Zutcp::writeTCP(frame, sizeof(frame), route, header, 5);
    ZuCHECK(refused && !memcmp(saved, frame, sizeof(frame)),
      "capacity or unrepresentable option geometry must fail before modifying the packet");
  }
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testVectors);
  ZuTestCall(testGeometry);
  ZuTestCall(testOptions);
  ZuTestCall(testTCPVectors);
  ZuTestCall(testTCPGeometry);
  ZuTestCall(testTCPOptions);
  ZuTestCall(testTCPOptionWire);
}
