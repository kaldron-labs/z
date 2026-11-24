//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <net/if.h>
#include <linux/if_link.h>
#include <linux/if_arp.h>

#include <zlib/ZuString.hh>
#include <zlib/ZpktNif.hh>

namespace Zpkt {

template <ZuString Operation>
static ZeException failed(ZeError error)
{
  return ZeEXCEPT(Error, "Zpkt", ([error](auto &out) {
    out << Operation << ": " << error;
  }));
}

static ZeException malformed(ZeError *native)
{
  ZeError error{EPROTO};
  if (native) *native = error;
  return failed<"malformed or incomplete rtnetlink link response">(error);
}

static ZuUnion<NifInfo, ZeException> parseLink(const nlmsghdr *header, ZeError *native)
{
  if (header->nlmsg_len < NLMSG_LENGTH(sizeof(ifinfomsg))) return malformed(native);
  auto info = static_cast<const ifinfomsg *>(NLMSG_DATA(header));
  if (info->ifi_type != ARPHRD_ETHER) {
    ZeError error{EAFNOSUPPORT};
    if (native) *native = error;
    return failed<"packet interface must provide Ethernet framing">(error);
  }
  NifInfo state;
  state.ifindex = info->ifi_index;
  bool mac = false;
  int length = IFLA_PAYLOAD(header);
  auto attr = IFLA_RTA(info);
  for (; RTA_OK(attr, length); attr = RTA_NEXT(attr, length)) {
    auto type = attr->rta_type & NLA_TYPE_MASK;
    auto value = static_cast<const uint8_t *>(RTA_DATA(attr));
    auto size = RTA_PAYLOAD(attr);
    switch (type) {
      case IFLA_MTU:
      case IFLA_NUM_RX_QUEUES:
	if (size != 4) return malformed(native);
	memcpy(type == IFLA_MTU ? &state.mtu : &state.rxQueues, value, 4);
	break;
      case IFLA_ADDRESS:
	if (size != 6) return malformed(native);
	memcpy(state.mac, value, 6);
	mac = true;
	break;
    }
  }
  if (length || !mac || !state.mtu || !state.rxQueues) return malformed(native);
  return state;
}

ZuUnion<int, ZeException> nifOpen()
{
  int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
  if (fd < 0) return failed<"rtnetlink socket">(ZeLastError);
  timeval timeout{2, 0};
  if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
    auto error = ZeLastError;
    close(fd);
    return failed<"rtnetlink receive timeout">(error);
  }
  return fd;
}

ZuUnion<void, ZeException> nifTransact(
  nlmsghdr &request, NifReply *reply, bool *rejected, ZeError *native, int fd)
{
  if (rejected) *rejected = false;
  if (native) *native = ZeOK;
  bool owned = fd < 0;
  if (fd < 0) {
    auto opened = nifOpen();
    if (opened.is<ZeException>()) return ZuMv(opened).p<ZeException>();
    fd = opened.p<int>();
  }
  auto fail = [fd, owned, native](ZeException exception, ZeError error = ZeOK) {
    if (native && error) *native = error;
    if (owned) close(fd);
    return exception;
  };
  sockaddr_nl peer{};
  peer.nl_family = AF_NETLINK;
  if (sendto(fd, &request, request.nlmsg_len, 0,
      reinterpret_cast<sockaddr *>(&peer), sizeof(peer)) != request.nlmsg_len) {
    auto error = ZeLastError;
    return fail(failed<"rtnetlink request">(error), error);
  }
  ZtArray<uint8_t, ZtArrayHeapID<"Zpkt.Netlink">> buffer;
  for (;;) {
    // Query the next datagram's exact extent. Link attributes have no fixed
    // userspace maximum; retain no arbitrary response-capacity limit.
    auto extent = recv(fd, nullptr, 0, MSG_PEEK | MSG_TRUNC);
    if (extent < 0) {
      if (errno == EINTR) continue;
      auto error = ZeLastError;
      return fail(failed<"rtnetlink response length">(error), error);
    }
    if (!extent) return fail(malformed(native));
    buffer.length(extent);
    iovec iov{buffer.data(), size_t(buffer.length())};
    msghdr message{};
    message.msg_name = &peer;
    message.msg_namelen = sizeof(peer);
    message.msg_iov = &iov;
    message.msg_iovlen = 1;
    auto n = recvmsg(fd, &message, 0);
    if (n < 0) {
      if (errno == EINTR) continue;
      auto error = ZeLastError;
      return fail(failed<"rtnetlink response">(error), error);
    }
    if (!n || (message.msg_flags & MSG_TRUNC) || peer.nl_pid)
      return fail(malformed(native));
    int remaining = n;
    auto header = reinterpret_cast<const nlmsghdr *>(buffer.data());
    for (; NLMSG_OK(header, remaining); header = NLMSG_NEXT(header, remaining)) {
      if (header->nlmsg_seq != request.nlmsg_seq) continue;
      if (header->nlmsg_type == NLMSG_ERROR) {
	if (header->nlmsg_len < NLMSG_LENGTH(sizeof(nlmsgerr))) return fail(malformed(native));
	auto result = static_cast<const nlmsgerr *>(NLMSG_DATA(header))->error;
	if (result) {
	  if (rejected) *rejected = true;
	  ZeError error{-result};
	  return fail(failed<"rtnetlink link operation">(error), error);
	}
	if (!reply) { if (owned) close(fd); return {}; }
      } else if (reply && header->nlmsg_type == RTM_NEWLINK) {
	reply->length(header->nlmsg_len);
	memcpy(reply->data(), header, header->nlmsg_len);
	if (owned) close(fd);
	return {};
      }
    }
    if (remaining) return fail(malformed(native));
  }
}

static ZuUnion<NifInfo, ZeException> query(unsigned ifindex)
{
  struct {
    nlmsghdr header{};
    ifinfomsg info{};
  } request;
  request.header.nlmsg_len = NLMSG_LENGTH(sizeof(ifinfomsg));
  request.header.nlmsg_type = RTM_GETLINK;
  request.header.nlmsg_flags = NLM_F_REQUEST;
  request.header.nlmsg_seq = 1;
  request.info.ifi_family = AF_UNSPEC;
  request.info.ifi_index = ifindex;
  NifReply reply;
  auto result = nifTransact(request.header, &reply);
  if (result.is<ZeException>()) return ZuMv(result).p<ZeException>();
  return parseLink(reinterpret_cast<const nlmsghdr *>(reply.data()), nullptr);
}

ZuUnion<NifInfo, ZeException> nifQuery(ZuCSpan name)
{
  if (!name || name.length() >= IF_NAMESIZE || memchr(name.data(), 0, name.length()))
    return ZeEXCEPT(Error, "Zpkt", "invalid packet interface name");
  char text[IF_NAMESIZE];
  memcpy(text, name.data(), name.length());
  text[name.length()] = 0;
  auto index = if_nametoindex(text);
  if (!index) {
    auto error = ZeLastError;
    return ZeEXCEPT(Error, "Zpkt", ([name = ZeString{name}, error](auto &out) {
      out << "resolve packet interface " << name << ": " << error;
    }));
  }
  return query(index);
}

} // namespace Zpkt
