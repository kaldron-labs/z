//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>
#include <linux/genetlink.h>
#include <linux/netdev.h>
#include <linux/rtnetlink.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZtArray.hh>
#include <zlib/Zxdp.hh>
#include <zlib/ZxdpTx.hh>
#include <zlib/ZxdpUMem.hh>
#include <zlib/ZxdpPool.hh>

using namespace ZuTestUtil;
using namespace Zxdp::Xdp;

static void put16(uint8_t *p, unsigned n) { p[0] = n>>8; p[1] = n; }

static void testMetadata()
{
  ZuTestScopeRT(testMetadata);
  constexpr unsigned payloads[] = {0, 1, 7, 31, 256, 1460};
  constexpr unsigned options[] = {0, 12, 40};
  bool exact = true;
  for (unsigned family = 4; family <= 6; family += 2)
    for (unsigned tags = 0; tags < 3; ++tags)
      for (auto opt : options) for (auto payload : payloads)
	for (unsigned shift = 0; shift <= 4; shift += 4) {
	  alignas(64) uint8_t backing[64 + 4 + 2048], saved[2048];
	  memset(backing, 0xa5, sizeof(backing));
	  auto frame = backing + 64 + shift;
	  unsigned l2 = 14 + (tags<<2), l3 = family == 4 ? 20 + (tags<<2) : 40;
	  unsigned size = l3 + 20 + opt + payload;
	  unsigned wire = l2 + size;
	  if (wire < 60) wire = 60;
	  put16(frame + 12, tags ? 0x8100 : family == 4 ? 0x0800 : 0x86dd);
	  for (unsigned i = 0; i < tags; ++i) {
	    put16(frame + 14 + (i<<2), 1);
	    put16(frame + 16 + (i<<2), i + 1 < tags ? 0x8100 :
	      family == 4 ? 0x0800 : 0x86dd);
	  }
	  auto ip = frame + l2;
	  memset(ip, 0, l3 + 20 + opt);
	  ip[0] = family == 4 ? 0x40 | (l3>>2) : 0x60;
	  if (family == 4) { put16(ip + 2, size); ip[9] = 6; }
	  else { put16(ip + 4, size - 40); ip[6] = 6; }
	  auto tcp = ip + l3;
	  tcp[12] = ((20 + opt)>>2)<<4;
	  put16(tcp + 16, 0x1234); // caller's pseudo-header seed remains unchanged
	  memcpy(saved, frame, wire);
	  TxTCP layout;
	  bool parsed = layout.init(frame, wire);
	  exact &= parsed && layout.start == l2 + l3 && layout.wire == l2 + size;
	  if (!parsed) continue;
	  layout.metadata(frame);
	  xsk_tx_metadata metadata;
	  memcpy(&metadata, frame - sizeof(metadata), sizeof(metadata));
	  exact &= metadata.flags == XDP_TXMD_FLAGS_CHECKSUM &&
	    metadata.request.csum_start == l2 + l3 && metadata.request.csum_offset == 16 &&
	    !metadata.request.reserved && !metadata.request.launch_time &&
	    !memcmp(frame, saved, wire);
	  for (auto p = backing; p < frame - sizeof(metadata); ++p) exact &= *p == 0xa5;
	}
  ZuCheckRT(exact, log_("IPv4/IPv6 options, VLANs, odd payloads and padding produce "
    "native checksum offsets without changing frame bytes, including unaligned metadata"));
}

static void testBounds()
{
  ZuTestScope(testBounds);
  uint8_t frame[80]{};
  put16(frame + 12, 0x0800);
  frame[14] = 0x45; frame[23] = 6; frame[46] = 0x50;
  put16(frame + 16, 40);
  TxTCP layout;
  ZuCheck(!layout.init(nullptr, sizeof(frame)));
  ZuCheck(!layout.init(frame, 13));
  ZuCheck(!layout.init(frame, 53));
  ZuCheck(layout.init(frame, sizeof(frame)) && layout.wire == 54);
  frame[20] = 0x20; // IPv4 More Fragments
  ZuCheck(!layout.init(frame, sizeof(frame)));
  frame[20] = 0; frame[46] = 0x40;
  ZuCheck(!layout.init(frame, sizeof(frame)));
  frame[46] = 0x50; frame[23] = 17;
  ZuCheck(!layout.init(frame, sizeof(frame)));
}

static void testCapabilityReply()
{
  ZuTestScope(testCapabilityReply);
  struct Message {
    nlmsghdr header;
    genlmsghdr info;
    uint8_t attrs[32];
  } message{};
  message.header.nlmsg_type = 31;
  message.header.nlmsg_seq = 7;
  message.header.nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN) + RTA_SPACE(8);
  auto attr = reinterpret_cast<rtattr *>(message.attrs);
  attr->rta_type = NETDEV_A_DEV_XSK_FEATURES;
  attr->rta_len = RTA_LENGTH(8);
  uint64_t flags = NETDEV_XSK_FLAGS_TX_CHECKSUM | NETDEV_XSK_FLAGS_TX_TIMESTAMP;
  memcpy(RTA_DATA(attr), &flags, sizeof(flags));
  ZuBSpan bytes{reinterpret_cast<uint8_t *>(&message), message.header.nlmsg_len};
  ZuCheck(genlScalar(bytes, 31, 7, NETDEV_A_DEV_XSK_FEATURES, 8) == flags);
  ZuCheck(!genlScalar(bytes, 31, 8, NETDEV_A_DEV_XSK_FEATURES, 8));
  ZuCheck(!genlScalar(bytes, 32, 7, NETDEV_A_DEV_XSK_FEATURES, 8));
  ZuCheck(!genlScalar(bytes, 31, 7, NETDEV_A_DEV_XSK_FEATURES, 2));
  ZuCheck(!genlScalar(bytes, 31, 7, NETDEV_A_DEV_XSK_FEATURES, 16));
  ZuCheck(!genlScalar({bytes.data(), bytes.length() - 1}, 31, 7,
    NETDEV_A_DEV_XSK_FEATURES, 8));
  attr->rta_len = RTA_LENGTH(16);
  ZuCheck(!genlScalar(bytes, 31, 7, NETDEV_A_DEV_XSK_FEATURES, 8));
  message.header.nlmsg_type = GENL_ID_CTRL;
  message.header.nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN) + RTA_SPACE(2);
  attr->rta_type = CTRL_ATTR_FAMILY_ID;
  attr->rta_len = RTA_LENGTH(2);
  uint16_t family = 0x1234;
  memcpy(RTA_DATA(attr), &family, sizeof(family));
  bytes = {reinterpret_cast<uint8_t *>(&message), message.header.nlmsg_len};
  ZuCheck(genlScalar(bytes, GENL_ID_CTRL, 7, CTRL_ATTR_FAMILY_ID, 2) == family);
  ZuCheck(!tcpChksumAvailable(0));
  ZuCheck(!tcpChksumAvailable(unsigned(-1)));
}

static void testArena()
{
  ZuTestScopeRT(testArena);
  constexpr unsigned capacities[] = {512, 4096};
  auto device = Zxdp::instance(Zxdp::Type::Xdp);
  for (auto capacity : capacities) {
    auto rx = device->bufPool(Zi::IOBufType::Rx, 2, 1514);
    auto tx = device->bufPool(Zi::IOBufType::TxTCP, 2, capacity);
    Zi::IOBufPoolList pools;
    pools.pushNode(tx.ptr());
    ZtArray<ZmRef<Zi::IOBufPool>> refs;
    refs.push(tx);
    auto created = UMem::create(refs.cspan(), rx.ptr(), 1500, true);
    bool exact = created.is<ZmRef<UMem>>();
    ZuCheckRT(exact, log_("metadata request preserves valid pool capacity ", capacity));
    if (!exact) continue;
    auto arena = ZuMv(created).p<ZmRef<UMem>>();
    arena->publish(refs.cspan(), static_cast<BufPool *>(rx.ptr()));
    auto first = tx->alloc(), second = tx->alloc();
    exact = first && second && !tx->alloc() && tx->size == capacity;
    if (first && second) {
      memset(first, 0x3c, capacity); memset(second, 0x4d, capacity);
      exact &= !(uintptr_t(first) % tx->align) && !(uintptr_t(second) % tx->align);
      if (capacity == 512) {
	exact &= arena->layout().txMetadata == sizeof(xsk_tx_metadata) &&
	  second - first == 1024;
	TxTCP layout{64, 34};
	layout.metadata(first + 4);
	layout.metadata(second);
	for (unsigned i = 4; i < capacity; ++i) exact &= first[i] == 0x3c;
	for (unsigned i = 0; i < capacity; ++i) exact &= second[i] == 0x4d;
      } else exact &= !arena->layout().txMetadata && second - first == 4096;
    }
    if (first) tx->free(first);
    if (second) tx->free(second);
    arena->retire(pools, static_cast<BufPool *>(rx.ptr()));
    ZuCheckRT(exact, log_("private metadata prefix is aligned, isolated and allocation-free; "
      "a full-page pool retains its capacity with offload unavailable"));
  }
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testMetadata);
  ZuTestCall(testBounds);
  ZuTestCall(testCapabilityReply);
  ZuTestCall(testArena);
}
