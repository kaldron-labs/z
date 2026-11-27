//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <errno.h>
#include <linux/netlink.h>
#include <rdma/rdma_netlink.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <infiniband/mlx5dv.h>

#include "ZpktMlx.hh"

namespace Zpkt::Mlx {

static ZeException portError(const char *operation, int code)
{
  return ZeEXCEPT(Error, "Zpkt.Mlx", ([operation, code](auto &out) {
    out << operation << ": " << ZeError{code};
  }));
}

int matchPort(const nlmsghdr &header, Port &port, unsigned ifindex, unsigned device)
{
  if (header.nlmsg_len < NLMSG_HDRLEN) return EPROTO;
  auto data = static_cast<const uint8_t *>(NLMSG_DATA(&header));
  unsigned left = header.nlmsg_len - NLMSG_HDRLEN;
  unsigned netdev = 0, foundDevice = 0, number = 0;
  bool haveDevice = false;
  while (left >= NLA_HDRLEN) {
    auto attr = reinterpret_cast<const nlattr *>(data);
    unsigned step = NLA_ALIGN(attr->nla_len);
    if (attr->nla_len < NLA_HDRLEN || step > left) return EPROTO;
    switch (attr->nla_type & NLA_TYPE_MASK) {
      case RDMA_NLDEV_ATTR_NDEV_INDEX:
      case RDMA_NLDEV_ATTR_DEV_INDEX:
      case RDMA_NLDEV_ATTR_PORT_INDEX: {
	if (attr->nla_len != NLA_HDRLEN + sizeof(uint32_t)) return EPROTO;
	uint32_t value;
	memcpy(&value, data + NLA_HDRLEN, sizeof(value));
	switch (attr->nla_type & NLA_TYPE_MASK) {
	  case RDMA_NLDEV_ATTR_NDEV_INDEX: netdev = value; break;
	  case RDMA_NLDEV_ATTR_DEV_INDEX: foundDevice = value; haveDevice = true; break;
	  case RDMA_NLDEV_ATTR_PORT_INDEX: number = value; break;
	}
      } break;
    }
    data += step;
    left -= step;
  }
  if (left || !haveDevice || foundDevice != device || !number) return EPROTO;
  if (netdev != ifindex) return 0;
  if (port.number && (port.device != device || port.number != number)) return ENOTUNIQ;
  port = {device, number};
  return 0;
}

static int discover(int fd, unsigned seq, unsigned device, unsigned ifindex, Port &port)
{
  constexpr unsigned type = RDMA_NL_GET_TYPE(RDMA_NL_NLDEV, RDMA_NLDEV_CMD_PORT_GET);
  struct {
    nlmsghdr	header;
    nlattr	attr;
    uint32_t	device;
  } request{{sizeof(request), type, NLM_F_REQUEST | NLM_F_DUMP, seq, 0},
    {NLA_HDRLEN + sizeof(uint32_t), RDMA_NLDEV_ATTR_DEV_INDEX}, device};
  sockaddr_nl peer{};
  peer.nl_family = AF_NETLINK;
  if (::sendto(fd, &request, sizeof(request), 0,
      reinterpret_cast<sockaddr *>(&peer), sizeof(peer)) < 0) return errno;

  for (;;) {
    alignas(nlmsghdr) uint8_t data[32768];
    iovec iov{data, sizeof(data)};
    msghdr message{};
    message.msg_name = &peer;
    message.msg_namelen = sizeof(peer);
    message.msg_iov = &iov;
    message.msg_iovlen = 1;
    auto n = ::recvmsg(fd, &message, 0);
    if (n < 0) {
      if (errno == EINTR) continue;
      return errno;
    }
    if (!n || peer.nl_pid || (message.msg_flags & MSG_TRUNC)) return EPROTO;
    int left = int(n);
    auto header = reinterpret_cast<const nlmsghdr *>(data);
    for (; NLMSG_OK(header, left); header = NLMSG_NEXT(header, left)) {
      if (header->nlmsg_seq != seq || (header->nlmsg_flags & NLM_F_DUMP_INTR)) return EAGAIN;
      switch (header->nlmsg_type) {
	case NLMSG_DONE: {
	  int status = 0;
	  if (header->nlmsg_len >= NLMSG_LENGTH(sizeof(status)))
	    memcpy(&status, NLMSG_DATA(header), sizeof(status));
	  return status <= 0 ? -status : EPROTO;
	}
	case NLMSG_ERROR:
	  if (header->nlmsg_len < NLMSG_LENGTH(sizeof(nlmsgerr))) return EPROTO;
	  if (int status = static_cast<const nlmsgerr *>(NLMSG_DATA(header))->error)
	    return status < 0 ? -status : EPROTO;
	  break;
	case type:
	  if (int rc = matchPort(*header, port, ifindex, device)) return rc;
	  break;
	default:
	  return EPROTO;
      }
    }
    if (left) return EPROTO;
  }
}

ZuUnion<Port, ZeException> findPort(unsigned ifindex)
{
  if (!ifindex) return portError("resolve Ethernet interface to RDMA port", EINVAL);
  int fd = ::socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_RDMA);
  if (fd < 0) return portError("open RDMA netlink", errno);
  timeval timeout{2, 0};
  if (::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
    int rc = errno;
    ::close(fd);
    return portError("configure RDMA netlink timeout", rc);
  }
  int count = 0;
  auto devices = ibv_get_device_list(&count);
  if (!devices) {
    int rc = errno;
    ::close(fd);
    return portError("enumerate RDMA devices", rc);
  }
  Port port{};
  int rc = 0;
  for (int i = 0; i < count; ++i) {
    auto device = devices[i];
    int index = ibv_get_device_index(device);
    if (index < 0 || !mlx5dv_is_supported(device)) continue;
    rc = discover(fd, unsigned(i) + 1, unsigned(index), ifindex, port);
    if (rc) break;
  }
  ibv_free_device_list(devices);
  ::close(fd);
  if (rc || !port.number)
    return portError("resolve Ethernet interface to mlx5 RDMA port", rc ? rc : ENODEV);
  return port;
}

} // namespace Zpkt::Mlx
