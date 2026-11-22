//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <linux/genetlink.h>
#include <linux/netdev.h>
#include <linux/rtnetlink.h>

#include <zlib/ZxdpTx.hh>

namespace Zxdp::Xdp {

static unsigned get16(const uint8_t *p) { return (unsigned(p[0])<<8) | p[1]; }

bool TxTCP::init(const uint8_t *packet, unsigned length)
{
  if (!packet || length < 14) return false;
  unsigned l2 = 14, type = get16(packet + 12);
  for (unsigned i = 0; i < 2 && (type == 0x8100 || type == 0x88a8); ++i) {
    if (length - l2 < 4) return false;
    type = get16(packet + l2 + 2);
    l2 += 4;
  }
  auto ip = packet + l2;
  unsigned l3, size;
  if (type == 0x0800) {
    if (length - l2 < 20 || ip[0]>>4 != 4 || ip[9] != 6 ||
	(get16(ip + 6) & 0x3fff)) return false;
    l3 = unsigned(ip[0] & 15)<<2;
    size = get16(ip + 2);
    if (l3 < 20) return false;
  } else if (type == 0x86dd) {
    if (length - l2 < 40 || ip[0]>>4 != 6 || ip[6] != 6) return false;
    l3 = 40;
    size = 40 + get16(ip + 4);
  } else return false;
  if (size > length - l2 || size < l3 || size - l3 < 20) return false;
  auto tcp = ip + l3;
  unsigned head = unsigned(tcp[12]>>4)<<2;
  if (head < 20 || head > size - l3) return false;
  start = l2 + l3;
  wire = l2 + size; // exclude software Ethernet padding from the checksum extent
  return true;
}

void TxTCP::metadata(uint8_t *packet) const
{
  xsk_tx_metadata value{};
  value.flags = XDP_TXMD_FLAGS_CHECKSUM;
  value.request.csum_start = start;
  value.request.csum_offset = 16;
  // Retained frames can begin at a four-byte offset within aligned backing.
  // The UAPI area is byte-addressed; avoid imposing struct alignment on it.
  memcpy(packet - sizeof(value), &value, sizeof(value));
}

// A single scalar attribute from a unicast generic-netlink transaction.
// rtattr and nlattr share the UAPI length/type header and alignment.
static uint64_t request(int fd, unsigned family, unsigned command, unsigned sequence,
    unsigned input, const void *data, unsigned bytes, unsigned output, unsigned size)
{
  struct Message {
    nlmsghdr header;
    genlmsghdr info;
    uint8_t attrs[32];
  } message{};
  message.header.nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN) + RTA_SPACE(bytes);
  message.header.nlmsg_type = family;
  message.header.nlmsg_flags = NLM_F_REQUEST;
  message.header.nlmsg_seq = sequence;
  message.info.cmd = command;
  message.info.version = 1;
  auto attr = reinterpret_cast<rtattr *>(message.attrs);
  attr->rta_type = input;
  attr->rta_len = RTA_LENGTH(bytes);
  memcpy(RTA_DATA(attr), data, bytes);
  sockaddr_nl peer{};
  peer.nl_family = AF_NETLINK;
  int n;
  do n = sendto(fd, &message, message.header.nlmsg_len, 0,
    reinterpret_cast<sockaddr *>(&peer), sizeof(peer));
  while (n < 0 && errno == EINTR);
  if (n != int(message.header.nlmsg_len)) return 0;
  alignas(nlmsghdr) uint8_t reply[8192];
  do n = recv(fd, reply, sizeof(reply), MSG_TRUNC);
  while (n < 0 && errno == EINTR);
  if (n <= 0 || unsigned(n) > sizeof(reply)) return 0;
  return genlScalar({reply, unsigned(n)}, family, sequence, output, size);
}

uint64_t genlScalar(ZuBSpan packet, unsigned family, unsigned sequence,
    unsigned output, unsigned size)
{
  if (size != sizeof(uint16_t) && size != sizeof(uint64_t)) return 0;
  int n = packet.length();
  auto header = reinterpret_cast<const nlmsghdr *>(packet.data());
  for (; NLMSG_OK(header, n); header = NLMSG_NEXT(header, n)) {
    if (header->nlmsg_seq != sequence) continue;
    if (header->nlmsg_type != family || header->nlmsg_len < NLMSG_LENGTH(GENL_HDRLEN))
      return 0;
    int remaining = header->nlmsg_len - NLMSG_LENGTH(GENL_HDRLEN);
    auto value = reinterpret_cast<const rtattr *>(
      static_cast<const uint8_t *>(NLMSG_DATA(header)) + GENL_HDRLEN);
    for (; RTA_OK(value, remaining); value = RTA_NEXT(value, remaining)) {
      if ((value->rta_type & NLA_TYPE_MASK) != output) continue;
      if (RTA_PAYLOAD(value) != size) return 0;
      if (size == sizeof(uint16_t)) {
	uint16_t result;
	memcpy(&result, RTA_DATA(value), sizeof(result));
	return result;
      }
      uint64_t result;
      memcpy(&result, RTA_DATA(value), sizeof(result));
      return result;
    }
  }
  return 0;
}

bool tcpChksumAvailable(unsigned ifindex)
{
  if (!ifindex) return false;
  int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_GENERIC);
  if (fd < 0) return false;
  timeval timeout{2, 0};
  bool available = false;
  if (!setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout))) {
    auto family = request(fd, GENL_ID_CTRL, CTRL_CMD_GETFAMILY, 1,
      CTRL_ATTR_FAMILY_NAME, NETDEV_FAMILY_NAME, sizeof(NETDEV_FAMILY_NAME),
      CTRL_ATTR_FAMILY_ID, sizeof(uint16_t));
    if (family) {
      uint32_t index = ifindex;
      auto features = request(fd, family, NETDEV_CMD_DEV_GET, 2,
	NETDEV_A_DEV_IFINDEX, &index, sizeof(index), NETDEV_A_DEV_XSK_FEATURES,
	sizeof(uint64_t));
      available = features & NETDEV_XSK_FLAGS_TX_CHECKSUM;
    }
  }
  close(fd);
  return available;
}

} // namespace Zxdp::Xdp
