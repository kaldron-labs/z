//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <errno.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <zlib/ZutcpIP.hh>

namespace Zutcp {

unsigned interfaceMTU(unsigned interface, ZtString<> &error)
{
  error = {};
  ifreq request{};
  if (!if_indextoname(interface, request.ifr_name)) {
    error = "interface name query: "; error << strerror(errno); return 0;
  }
  int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if (fd < 0) { error = "interface query socket: "; error << strerror(errno); return 0; }
  auto result = ioctl(fd, SIOCGIFMTU, &request);
  auto saved = errno;
  close(fd);
  if (result < 0) { error = "interface MTU query: "; error << strerror(saved); return 0; }
  if (request.ifr_mtu <= 0) { error = "interface has no usable MTU"; return 0; }
  return unsigned(request.ifr_mtu);
}

// Targeted request/reply only: no multicast subscription or routing-table copy.
// Called during cold control setup; a missing kernel reply has a bounded wait.
static int queryRoute(int fd, nlmsghdr *request, uint8_t *reply, unsigned size,
  ZtString<> &error)
{
  sockaddr_nl kernel{};
  kernel.nl_family = AF_NETLINK;
  if (sendto(fd, request, request->nlmsg_len, 0,
      reinterpret_cast<sockaddr *>(&kernel), sizeof(kernel)) != int(request->nlmsg_len)) {
    error = "route request: "; error << strerror(errno); return -1;
  }
  iovec vector{reply, size};
  msghdr message{};
  message.msg_name = &kernel;
  message.msg_namelen = sizeof(kernel);
  message.msg_iov = &vector;
  message.msg_iovlen = 1;
  int n;
  do { n = recvmsg(fd, &message, MSG_TRUNC); } while (n < 0 && errno == EINTR);
  if (n < 0) { error = "route reply: "; error << strerror(errno); return -1; }
  auto header = reinterpret_cast<nlmsghdr *>(reply);
  if (unsigned(n) > size || !NLMSG_OK(header, n) || kernel.nl_pid ||
      header->nlmsg_seq != request->nlmsg_seq) {
    error = "invalid route reply"; return -1;
  }
  if (header->nlmsg_type == NLMSG_ERROR) {
    if (header->nlmsg_len < NLMSG_LENGTH(sizeof(nlmsgerr))) {
      error = "truncated route error"; return -1;
    }
    auto result = reinterpret_cast<nlmsgerr *>(NLMSG_DATA(header));
    error = "kernel route/neighbor lookup: "; error << strerror(-result->error); return -1;
  }
  return n;
}

static void routeAttr(nlmsghdr *request, unsigned type, const void *data, unsigned size)
{
  auto attr = reinterpret_cast<rtattr *>(
    reinterpret_cast<uint8_t *>(request) + NLMSG_ALIGN(request->nlmsg_len));
  attr->rta_type = type;
  attr->rta_len = RTA_LENGTH(size);
  memcpy(RTA_DATA(attr), data, size);
  request->nlmsg_len = NLMSG_ALIGN(request->nlmsg_len) + RTA_SPACE(size);
}

static bool resolvePath(int fd, unsigned interface, IPPath &path, ZtString<> &error)
{
  auto family = path.remote.type() == ZiIPType::V4 ? AF_INET : AF_INET6;
  unsigned bytes = family == AF_INET ? sizeof(in_addr) : sizeof(in6_addr);
  const void *destination = family == AF_INET ?
    static_cast<const void *>(&path.remote.inAddr()) : &path.remote.in6Addr();
  // Each request has one protocol address plus one interface attribute at most.
  alignas(nlmsghdr) uint8_t request[NLMSG_SPACE(sizeof(rtmsg)) + RTA_SPACE(16) + RTA_SPACE(4)]{};
  alignas(nlmsghdr) uint8_t reply[4096];
  auto header = reinterpret_cast<nlmsghdr *>(request);
  header->nlmsg_len = NLMSG_LENGTH(sizeof(rtmsg));
  header->nlmsg_type = RTM_GETROUTE;
  header->nlmsg_flags = NLM_F_REQUEST;
  header->nlmsg_seq = 1;
  auto route = reinterpret_cast<rtmsg *>(NLMSG_DATA(header));
  route->rtm_family = family;
  route->rtm_dst_len = bytes<<3;
  routeAttr(header, RTA_DST, destination, bytes);
  routeAttr(header, RTA_OIF, &interface, sizeof(interface));
  if (queryRoute(fd, header, reply, sizeof(reply), error) < 0) return false;
  auto answer = reinterpret_cast<nlmsghdr *>(reply);
  if (answer->nlmsg_type != RTM_NEWROUTE || answer->nlmsg_len < NLMSG_LENGTH(sizeof(rtmsg))) {
    error = "missing route result"; return false;
  }
  route = reinterpret_cast<rtmsg *>(NLMSG_DATA(answer));
  if (route->rtm_family != family || route->rtm_type != RTN_UNICAST) {
    error = "route is not unicast on the configured interface"; return false;
  }
  uint8_t nextHop[16];
  memcpy(nextHop, destination, bytes);
  ZiIP source;
  unsigned output = 0;
  int remaining = RTM_PAYLOAD(answer);
  for (auto attr = RTM_RTA(route); RTA_OK(attr, remaining); attr = RTA_NEXT(attr, remaining)) {
    unsigned n = RTA_PAYLOAD(attr);
    auto data = RTA_DATA(attr);
    switch (attr->rta_type) {
      case RTA_OIF:
	if (n == sizeof(output)) memcpy(&output, data, n);
	break;
      case RTA_GATEWAY:
	if (n == bytes) memcpy(nextHop, data, n);
	break;
      case RTA_PREFSRC:
	if (n == bytes) {
	  if (family == AF_INET) {
	    in_addr ip; memcpy(&ip, data, bytes); source = ip;
	  } else {
	    in6_addr ip; memcpy(&ip, data, bytes); source = ip;
	  }
	}
	break;
      case RTA_METRICS: {
	int length = n;
	for (auto metric = static_cast<rtattr *>(data); RTA_OK(metric, length);
	    metric = RTA_NEXT(metric, length)) {
	  if (metric->rta_type != RTAX_MTU || RTA_PAYLOAD(metric) != sizeof(unsigned)) continue;
	  unsigned mtu;
	  memcpy(&mtu, RTA_DATA(metric), sizeof(mtu));
	  if (mtu && mtu < path.mtu) path.mtu = mtu;
	}
      } break;
    }
  }
  if (output != interface) { error = "route selected a different interface"; return false; }
  if (path.local.type() == ZiIPType::Null || path.local.wildcard()) {
    if (!source || source.wildcard()) {
      error = "route has no usable source address"; return false;
    }
    path.local = source;
  }
  memset(request, 0, sizeof(request));
  header->nlmsg_len = NLMSG_LENGTH(sizeof(ndmsg));
  header->nlmsg_type = RTM_GETNEIGH;
  header->nlmsg_flags = NLM_F_REQUEST;
  header->nlmsg_seq = 2;
  auto neighbor = reinterpret_cast<ndmsg *>(NLMSG_DATA(header));
  neighbor->ndm_family = family;
  neighbor->ndm_ifindex = interface;
  routeAttr(header, NDA_DST, nextHop, bytes);
  if (queryRoute(fd, header, reply, sizeof(reply), error) < 0) return false;
  answer = reinterpret_cast<nlmsghdr *>(reply);
  if (answer->nlmsg_type != RTM_NEWNEIGH || answer->nlmsg_len < NLMSG_LENGTH(sizeof(ndmsg))) {
    error = "missing neighbor result"; return false;
  }
  neighbor = reinterpret_cast<ndmsg *>(NLMSG_DATA(answer));
  if (neighbor->ndm_family != family || neighbor->ndm_ifindex != int(interface) ||
      !(neighbor->ndm_state & (NUD_PERMANENT | NUD_NOARP))) {
    error = "next hop has no static neighbor"; return false;
  }
  bool found = false;
  remaining = NLMSG_PAYLOAD(answer, sizeof(ndmsg));
  auto attr = reinterpret_cast<rtattr *>(
    reinterpret_cast<uint8_t *>(neighbor) + NLMSG_ALIGN(sizeof(ndmsg)));
  for (; RTA_OK(attr, remaining); attr = RTA_NEXT(attr, remaining))
    if (attr->rta_type == NDA_LLADDR && RTA_PAYLOAD(attr) == sizeof(path.remoteMAC)) {
      memcpy(path.remoteMAC, RTA_DATA(attr), sizeof(path.remoteMAC));
      found = true;
    }
  if (!found) error = "static neighbor has no Ethernet address";
  return found;
}

bool resolveIPPath(unsigned interface, IPPath &path, ZtString<> &error)
{
  error = {};
  if ((path.remote.type() != ZiIPType::V4 && path.remote.type() != ZiIPType::V6) ||
      path.remote.wildcard() ||
      (path.local.type() != ZiIPType::Null && path.local.type() != path.remote.type())) {
    error = "invalid route address family"; return false;
  }
  IPPath found = path;
  found.mtu = interfaceMTU(interface, error);
  if (!found.mtu) return false;
  ifreq link{};
  if (!if_indextoname(interface, link.ifr_name)) {
    error = "interface name query: "; error << strerror(errno); return false;
  }
  int query = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if (query < 0) { error = "interface query socket: "; error << strerror(errno); return false; }
  bool ethernet = !ioctl(query, SIOCGIFHWADDR, &link) &&
    link.ifr_hwaddr.sa_family == ARPHRD_ETHER;
  close(query);
  if (!ethernet) { error = "interface has no Ethernet address"; return false; }
  memcpy(found.localMAC, link.ifr_hwaddr.sa_data, sizeof(found.localMAC));
  int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
  if (fd < 0) { error = "route query socket: "; error << strerror(errno); return false; }
  timeval timeout{1, 0};
  bool ok = !setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  if (!ok) { error = "route query timeout: "; error << strerror(errno); }
  if (ok) ok = resolvePath(fd, interface, found, error);
  close(fd);
  if (ok) path = found;
  return ok;
}

static unsigned get16(const uint8_t *p) { return (unsigned(p[0])<<8) | p[1]; }
static void put16(uint8_t *p, unsigned n) { p[0] = n>>8; p[1] = n; }

// Internet checksum accumulation. Byte loads permit arbitrary Rx alignment.
static unsigned sum(const uint8_t *p, unsigned n, unsigned value = 0)
{
  while (n >= 2) { value += get16(p); p += 2; n -= 2; }
  if (n) value += unsigned(*p)<<8;
  return value;
}
static unsigned checksum(unsigned value)
{
  value = (value & 0xffff) + (value>>16);
  value = (value & 0xffff) + (value>>16);
  return value ^ 0xffff;
}

static uint32_t get32(const uint8_t *p) {
  return (uint32_t(get16(p))<<16) | get16(p + 2);
}
static void put32(uint8_t *p, uint32_t n) {
  put16(p, n>>16); put16(p + 2, n);
}

static unsigned tcpSum(const ZiIP &local, const ZiIP &remote, unsigned length)
{
  unsigned value = IPPROTO_TCP + length;
  if (local.type() == ZiIPType::V4) {
    value = sum(reinterpret_cast<const uint8_t *>(&local.inAddr()), 4, value);
    return sum(reinterpret_cast<const uint8_t *>(&remote.inAddr()), 4, value);
  }
  value = sum(reinterpret_cast<const uint8_t *>(&local.in6Addr()), 16, value);
  return sum(reinterpret_cast<const uint8_t *>(&remote.in6Addr()), 16, value);
}

unsigned writeTCP(
  uint8_t *frame, unsigned capacity, const IPPath &path, const TCPHeader &header,
  unsigned payload, unsigned ecn)
{
  unsigned head = tcpHead(path.local.type(), header);
  unsigned base = ipHead(path.local.type());
  if (!head || head < base || payload > 65535 - (head - base)) return 0;
  unsigned options = head - base - 20;
  unsigned length = payload + 20 + options;
  unsigned wire = writeIP(frame, capacity, path, IPPROTO_TCP, length, ecn);
  if (!wire) return 0;
  auto tcp = frame + ipHead(path.local.type());
  put16(tcp, header.srcPort);
  put16(tcp + 2, header.dstPort);
  put32(tcp + 4, header.seq);
  put32(tcp + 8, header.ack);
  tcp[12] = uint8_t(((20 + options)>>2)<<4);
  tcp[13] = header.flags;
  put16(tcp + 14, header.window);
  put16(tcp + 16, 0);
  put16(tcp + 18, header.urgent);
  if (header.options.encode(tcp + 20, options, header.flags & 0x02) != int(options)) return 0;
  put16(tcp + 16, checksum(sum(tcp, length,
    tcpSum(path.local, path.remote, length))));
  return wire;
}

bool readTCP(const IPView &ip, TCPView &view)
{
  unsigned length = ip.payload.length();
  auto type = ip.local.type();
  if (ip.protocol != IPPROTO_TCP || length < 20 || length > 65535 ||
      (type != ZiIPType::V4 && type != ZiIPType::V6) || type != ip.remote.type())
    return false;
  auto tcp = ip.payload.data();
  unsigned head = (tcp[12]>>4)<<2;
  if (head < 20 || head > length ||
      checksum(sum(tcp, length, tcpSum(ip.local, ip.remote, length)))) return false;
  TCPHeader header{
    uint16_t(get16(tcp)), uint16_t(get16(tcp + 2)), get32(tcp + 4), get32(tcp + 8),
    uint16_t(get16(tcp + 14)), uint16_t(get16(tcp + 18)), tcp[13]};
  if (header.options.decode({tcp + 20, head - 20}, header.flags & 0x02) < 0) return false;
  view.header = header;
  view.options = {tcp + 20, head - 20};
  view.payload = {tcp + head, length - head};
  return true;
}

unsigned writeIP(
  uint8_t *frame, unsigned capacity, const IPPath &path, unsigned protocol,
  unsigned payload, unsigned ecn)
{
  auto type = path.local.type();
  unsigned head = ipHead(type);
  if (!frame || !head || type != path.remote.type() || protocol > 255 ||
      payload > 65535 || (type == ZiIPType::V4 && payload > 65535 - 20) ||
      path.mtu < head - 14 || payload > path.mtu - (head - 14)) return 0;
  unsigned length = head + payload, wire = length < 60 ? 60 : length;
  if (wire > capacity) return 0;
  memcpy(frame, path.remoteMAC, 6);
  memcpy(frame + 6, path.localMAC, 6);
  auto ip = frame + 14;
  if (type == ZiIPType::V4) {
    put16(frame + 12, 0x0800);
    memset(ip, 0, 20);
    ip[0] = 0x45;
    ip[1] = ecn & 3;
    put16(ip + 2, 20 + payload);
    put16(ip + 6, 0x4000); // no fragmentation; producers obey the running MTU
    ip[8] = 64;
    ip[9] = protocol;
    memcpy(ip + 12, &path.local.inAddr(), 4);
    memcpy(ip + 16, &path.remote.inAddr(), 4);
    put16(ip + 10, checksum(sum(ip, 20)));
  } else {
    put16(frame + 12, 0x86dd);
    memset(ip, 0, 40);
    ip[0] = 0x60;
    ip[1] = (ecn & 3)<<4;
    put16(ip + 4, payload);
    ip[6] = protocol;
    ip[7] = 64;
    memcpy(ip + 8, &path.local.in6Addr(), 16);
    memcpy(ip + 24, &path.remote.in6Addr(), 16);
  }
  if (wire > length) memset(frame + length, 0, wire - length);
  return wire;
}

bool readIP(ZuBSpan frame, IPView &view)
{
  auto packet = frame.data();
  if (frame.length() < 14) return false;
  auto ip = packet + 14;
  unsigned length = frame.length() - 14, offset, protocol, ecn;
  ZiIP local, remote;
  switch (get16(packet + 12)) {
    case 0x0800: {
      if (length < 20 || (ip[0]>>4) != 4) return false;
      offset = (ip[0] & 15)<<2;
      unsigned total = get16(ip + 2);
      if (offset < 20 || offset > length || total > length || total < offset ||
          (get16(ip + 6) & 0x3fff) || checksum(sum(ip, offset))) return false;
      length = total;
      in_addr source, destination;
      memcpy(&source, ip + 12, 4);
      memcpy(&destination, ip + 16, 4);
      remote = source; local = destination;
      protocol = ip[9];
      ecn = ip[1] & 3;
    } break;
    case 0x86dd: {
      if (length < 40 || (ip[0]>>4) != 6) return false;
      unsigned total = 40 + get16(ip + 4);
      if (total > length) return false;
      length = total;
      offset = 40;
      protocol = ip[6];
      while (protocol == IPPROTO_HOPOPTS || protocol == IPPROTO_DSTOPTS ||
          protocol == IPPROTO_ROUTING || protocol == IPPROTO_FRAGMENT) {
        if (offset + 8 > length) return false;
        auto ext = ip + offset;
        unsigned size;
        if (protocol == IPPROTO_FRAGMENT) {
          if (get16(ext + 2) & 0xfff9) return false; // only an atomic fragment
          size = 8;
        } else {
          if (protocol == IPPROTO_ROUTING && ext[3]) return false;
          size = (unsigned(ext[1]) + 1)<<3;
        }
        if (size > length - offset) return false;
        protocol = ext[0];
        offset += size;
      }
      in6_addr source, destination;
      memcpy(&source, ip + 8, 16);
      memcpy(&destination, ip + 24, 16);
      remote = source; local = destination;
      ecn = (ip[1]>>4) & 3;
    } break;
    default: return false;
  }
  view.local = local;
  view.remote = remote;
  view.payload = {ip + offset, length - offset};
  view.protocol = protocol;
  view.ecn = ecn;
  return true;
}

} // namespace Zutcp
