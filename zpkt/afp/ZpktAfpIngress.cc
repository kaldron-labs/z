//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <net/if.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netlink.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <zlib/ZmAssert.hh>
#include "ZpktAfpNif.hh"

namespace Zpkt::Afp {

namespace {
struct Batch {
  alignas(nlmsghdr) uint8_t data[512]{};
  unsigned length = 0;
  nlmsghdr *msg = nullptr;

  void message(uint16_t type, uint16_t flags, unsigned seq, bool marker = false) {
    ZmAssert_(length + NLMSG_SPACE(sizeof(nfgenmsg)) <= sizeof(data));
    msg = reinterpret_cast<nlmsghdr *>(&data[length]);
    msg->nlmsg_type = type;
    msg->nlmsg_flags = NLM_F_REQUEST | flags;
    msg->nlmsg_seq = seq;
    msg->nlmsg_len = NLMSG_LENGTH(sizeof(nfgenmsg));
    auto gen = static_cast<nfgenmsg *>(NLMSG_DATA(msg));
    gen->nfgen_family = marker ? AF_UNSPEC : NFPROTO_NETDEV;
    gen->version = NFNETLINK_V0;
    gen->res_id = marker ? htons(NFNL_SUBSYS_NFTABLES) : 0;
    length += NLMSG_ALIGN(msg->nlmsg_len);
  }

  nlattr *attr(unsigned type, const void *value, unsigned size) {
    unsigned extent = NLA_ALIGN(NLA_HDRLEN + size);
    ZmAssert_(length + extent <= sizeof(data));
    auto a = reinterpret_cast<nlattr *>(&data[length]);
    a->nla_type = type;
    a->nla_len = NLA_HDRLEN + size;
    if (size) memcpy(&data[length + NLA_HDRLEN], value, size);
    msg->nlmsg_len += extent;
    length += extent;
    return a;
  }
  void number(unsigned type, uint32_t value) {
    value = htonl(value);
    attr(type, &value, sizeof(value));
  }
  void string(unsigned type, const char *value) {
    attr(type, value, strlen(value) + 1);
  }
};

int install(int fd, unsigned ifindex, const char *device)
{
  char table[32];
  snprintf(table, sizeof(table), "zpkt_afp_%u", ifindex);
  constexpr unsigned subsystem = NFNL_SUBSYS_NFTABLES << 8;
  constexpr unsigned create = NLM_F_CREATE | NLM_F_EXCL;
  Batch batch;
  batch.message(NFNL_MSG_BATCH_BEGIN, 0, 1, true);
  batch.message(subsystem | NFT_MSG_NEWTABLE, create, 2);
  batch.string(NFTA_TABLE_NAME, table);
  batch.number(NFTA_TABLE_FLAGS, NFT_TABLE_F_OWNER);
  batch.message(subsystem | NFT_MSG_NEWCHAIN, create | NLM_F_ACK, 3);
  batch.string(NFTA_CHAIN_TABLE, table);
  batch.string(NFTA_CHAIN_NAME, "ingress");
  batch.string(NFTA_CHAIN_TYPE, "filter");
  batch.number(NFTA_CHAIN_POLICY, NF_DROP);
  unsigned offset = batch.length;
  auto hook = batch.attr(NFTA_CHAIN_HOOK | NLA_F_NESTED, nullptr, 0);
  batch.number(NFTA_HOOK_HOOKNUM, NF_NETDEV_INGRESS);
  batch.number(NFTA_HOOK_PRIORITY, INT_MIN);
  batch.string(NFTA_HOOK_DEV, device);
  hook->nla_len = batch.length - offset;
  batch.message(NFNL_MSG_BATCH_END, 0, 4, true);

  timeval timeout{2, 0};
  if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
    return errno;
  sockaddr_nl kernel{};
  kernel.nl_family = AF_NETLINK;
  if (sendto(fd, batch.data, batch.length, 0,
      reinterpret_cast<sockaddr *>(&kernel), sizeof(kernel)) < 0) return errno;

  // nfnetlink sends errors before the requested chain ACK after commit/abort.
  // A private unicast socket has no other requests or multicast subscriptions.
  struct { nlmsghdr header; nlmsgerr ack; } reply;
  socklen_t addrLen = sizeof(kernel);
  ssize_t n;
  do {
    n = recvfrom(fd, &reply, sizeof(reply), 0,
      reinterpret_cast<sockaddr *>(&kernel), &addrLen);
  } while (n < 0 && errno == EINTR);
  if (n < 0) return errno;
  if (n < int(sizeof(reply)) || addrLen != sizeof(kernel) || kernel.nl_pid ||
      reply.header.nlmsg_len < sizeof(reply) || reply.header.nlmsg_type != NLMSG_ERROR)
    return EPROTO;
  if (reply.ack.error) return -reply.ack.error;
  if (reply.header.nlmsg_seq != 3) return EPROTO;
  return if_nametoindex(device) == ifindex ? 0 : ENODEV;
}
} // namespace

int Nif::openIngress(unsigned ifindex)
{
  char device[IF_NAMESIZE];
  if (!if_indextoname(ifindex, device)) return -errno;
  int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_NETFILTER);
  if (fd < 0) return -errno;
  if (int err = install(fd, ifindex, device)) {
    close(fd);
    return -err;
  }
  return fd;
}

} // namespace Zpkt::Afp
