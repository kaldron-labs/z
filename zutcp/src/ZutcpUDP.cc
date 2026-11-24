//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <errno.h>
#include <linux/rtnetlink.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <zlib/ZutcpUDP.hh>

namespace Zutcp {

static ZiIP udpWildcard(ZiIPType::T family)
{
  return family == ZiIPType::V6 ? ZiIP{in6addr_any} : ZiIP{in_addr{}};
}

static unsigned udpKey(ZiIPType::T family, uint16_t port)
{
  return (unsigned(family)<<16) | port;
}

static ZiIP udpSource(ZiIP local, ZiIP destination, const ZiCxnOptions &options)
{
  if (destination.multicast() && options.multicast() &&
      options.mif() && !options.mif().wildcard()) return options.mif();
  return local.multicast() ? ZiIP{} : local;
}

static ZeException membershipError(int error)
{
  return ZeEXCEPT(Error, "Zutcp", ([error = ZeError{error}](auto &s) {
    s << "UDP multicast membership: " << error;
  }));
}

MxPlugin::UdpCxn::UdpCxn(MxPlugin *owner, const ZiCxnInfo &info, IPPath path) :
  m_owner{owner}, m_info{info}
{
  portKey = udpKey(info.local.ip().type(), info.local.port());
  if (path.remote) {
    m_paths[0] = ZuMv(path);
    m_pathCount = m_nextPath = 1;
  }
}

MxPlugin::UdpCxn::~UdpCxn()
{
  if (m_membershipFD >= 0) ::close(m_membershipFD);
}

ZuUnion<void, ZeException> MxPlugin::UdpCxn::join()
{
  if (!m_info.options.multicast() || !m_info.options.mreqs().length()) return {};
  auto v6 = m_info.local.ip().v6();
  m_membershipFD = ::socket(v6 ? AF_INET6 : AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if (m_membershipFD < 0) return membershipError(errno);
  auto ifindex = m_owner->m_xdp->ifindex();
  for (const auto &membership : m_info.options.mreqs()) {
    int result;
    if (v6) {
      if (membership.ifIndex() && membership.ifIndex() != ifindex)
	return membershipError(EINVAL);
      ipv6_mreq req{membership.addr().in6Addr(), ifindex};
      result = ::setsockopt(m_membershipFD, IPPROTO_IPV6, IPV6_JOIN_GROUP, &req, sizeof(req));
    } else {
      auto mif = membership.mif();
      ip_mreqn req{membership.addr().inAddr(), mif ? mif.inAddr() : in_addr{}, int(ifindex)};
      result = ::setsockopt(m_membershipFD, IPPROTO_IP, IP_ADD_MEMBERSHIP, &req, sizeof(req));
    }
    if (result < 0) return membershipError(errno);
  }
  return {};
}

bool MxPlugin::UdpCxn::accepts(ZiIP local, const ZiSockAddr &peer) const
{
  if (retiring || !mxCxn || !mxCxn->rxUp() ||
      (!m_info.local.ip().wildcard() && m_info.local.ip() != local) ||
      (m_info.remote && m_info.remote != peer)) return false;
  if (!local.multicast()) return true;
  if (!m_info.options.multicast()) return false;
  for (const auto &membership : m_info.options.mreqs())
    if (membership.addr() == local) return true;
  return false;
}

ZiIOBuf *MxPlugin::UdpCxn::rxBuf() const
{
  auto cxn = mxCxn;
  if (!cxn || !cxn->rxUp()) return nullptr;
  auto &io = cxn->rxContext();
  auto stub = io.buf.ptr();
  if (io.completed() || !stub || stub->allocator() != m_info.rxAllocator ||
      !stub->registered() || stub->data_()) return nullptr;
  return stub;
}

bool MxPlugin::UdpCxn::receive(
  uint8_t *frame, unsigned offset, unsigned length, const ZiSockAddr &peer, uint8_t tos)
{
  auto stub = rxBuf();
  if (!stub) return false;
  auto cxn = mxCxn;
  auto &io = cxn->rxContext();
  auto owner = stub->owner;
  stub->data__ = uintptr_t(frame) | ZiIOBuf::Registered;
  stub->size = offset + length;
  stub->skip = offset;
  stub->length = length;
  io.addr = peer;
  io.tos = tos;
  io.flags = 0;
  while (!io.completed() && !io()) { }
  if (io.disconnected()) cxn->disconnect();
  else if (!io.completed() && (!io.buf || io.buf.ptr() == stub)) {
    // Preserve a retained datagram and arm fresh metadata for the next packet.
    io.buf = m_info.rxAllocator->allocBuf(owner, 0, m_info.mps);
    if (!io.buf) io.complete();
  }
  return true;
}

ZiSockAddr MxPlugin::UdpCxn::destination(const ZiTxContext &io) const
{
  return io.addr ? io.addr : m_info.remote;
}

void MxPlugin::UdpCxn::send(ZiConnection *cxn)
{
  if (cxn->txContext().closed()) { cxn->close(); return; }
  if (m_sending) return;
  m_sending = true;
  sendNext(cxn);
}

void MxPlugin::openUDPRouteMonitor()
{
  // Open before publishing Up so connected UDP setup is covered as well.
  m_udpRouteGeneration = m_udpRouteCheck = 0;
  int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC, NETLINK_ROUTE);
  if (fd < 0) return;
  sockaddr_nl addr{};
  addr.nl_family = AF_NETLINK;
  addr.nl_groups = RTMGRP_LINK | RTMGRP_NEIGH | RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR |
    RTMGRP_IPV4_ROUTE | RTMGRP_IPV6_ROUTE | RTMGRP_IPV4_RULE |
    (1U << (RTNLGRP_IPV6_RULE - 1));
  int group = RTNLGRP_NEXTHOP;
  if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) ||
      setsockopt(fd, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP, &group, sizeof(group))) {
    ::close(fd);
    return;
  }
  m_udpRouteFD = fd;
}

uint64_t MxPlugin::udpRouteGeneration()
{
  // One coarse vDSO clock read per send. Service the shared subscription
  // on demand, at most once per 10ms of active UDP output.
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC_COARSE, &ts);
  uint64_t now = uint64_t(ts.tv_sec) * 1000000000 + ts.tv_nsec;
  if (now < m_udpRouteCheck) return m_udpRouteGeneration;
  m_udpRouteCheck = now + 10000000;
  bool changed = m_udpRouteFD < 0;
  if (!changed) {
    // Every subscribed message invalidates all paths, including an overrun.
    // Discard whole datagrams without copying or parsing the route payload.
    for (unsigned i = 0; i < 16; ++i) {
      char byte;
      if (::recv(m_udpRouteFD, &byte, 1, MSG_DONTWAIT) < 0) {
	if (errno != EAGAIN && errno != EWOULDBLOCK) {
	  changed = true;
	  if (errno != EINTR && errno != ENOBUFS) {
	    ::close(m_udpRouteFD);
	    m_udpRouteFD = -1;
	  }
	}
	break;
      }
      changed = true;
    }
  }
  if (changed) ++m_udpRouteGeneration;
  return m_udpRouteGeneration;
}

const IPPath *MxPlugin::UdpCxn::findPath(const ZiIP &peer, uint64_t generation)
{
  if (m_generation != generation) {
    m_generation = generation;
    m_pathCount = m_nextPath = 0;
  }
  if (!m_pathCount) return nullptr;
  if (m_paths[m_pathIndex].remote == peer) return &m_paths[m_pathIndex];
  for (unsigned i = 0; i < m_pathCount; ++i) {
    if (m_paths[i].remote == peer) {
      m_pathIndex = i;
      return &m_paths[i];
    }
  }
  return nullptr;
}

void MxPlugin::UdpCxn::sendNext(ZiConnection *cxn)
{
  auto &io = cxn->txContext();
  while (cxn->txUp() && !io.completed()) {
    auto dest = destination(io);
    if (!dest || !dest.port() || dest.ip().wildcard() ||
	dest.ip().type() != m_info.local.ip().type()) {
      sent(cxn, -1);
      continue;
    }
    auto generation = m_owner->udpRouteGeneration();
    if (auto path = findPath(dest.ip(), generation)) {
      sent(cxn, submit(io, dest, *path));
      continue;
    }
    m_owner->rxRun([endpoint = ZmRef{this}, cxn = ZmRef{cxn}, dest, generation]() mutable {
      if (endpoint->retiring) return;
      auto owner = endpoint->m_owner;
      auto source = udpSource(endpoint->m_info.local.ip(), dest.ip(), endpoint->m_info.options);
      auto route = resolveUDPPath(owner->m_xdp->ifindex(), {source, dest.ip(), {}, {}, 0});
      owner->txRun([endpoint = ZuMv(endpoint), cxn = ZuMv(cxn),
	  dest, generation, route = ZuMv(route)]() mutable {
	endpoint->routed(cxn, dest, generation, ZuMv(route));
      });
    });
    return;
  }
  m_sending = false;
}

void MxPlugin::UdpCxn::routed(
  ZiConnection *cxn, const ZiSockAddr &dest, uint64_t generation,
  ZuUnion<IPPath, ZeException> route)
{
  auto &io = cxn->txContext();
  if (cxn->txUp() && io.closed()) { cxn->close(); return; }
  if (cxn->txUp() && !io.completed() && destination(io) == dest &&
      (m_owner->m_udpRouteFD < 0 || generation == m_owner->udpRouteGeneration())) {
    if (route.is<IPPath>()) {
      m_pathIndex = m_nextPath;
      m_paths[m_pathIndex] = ZuMv(route.p<IPPath>());
      m_nextPath = (m_nextPath + 1) % NPaths;
      if (m_pathCount < NPaths) ++m_pathCount;
      sent(cxn, submit(io, dest, m_paths[m_pathIndex]));
    } else {
      m_owner->reportError(ZuMv(route.p<ZeException>()));
      sent(cxn, -1);
    }
  }
  sendNext(cxn);
}

int MxPlugin::UdpCxn::submit(
  const ZiTxContext &io, const ZiSockAddr &dest, const IPPath &path)
{
  // UDP is atomic: a nonzero cursor has no separate network-header reserve.
  // Reject it before framing so refusal preserves the complete producer view.
  if (!io.buf || io.offset || io.buf->skip > io.buf->size ||
      io.buf->length > io.buf->size - io.buf->skip) return -1;
  auto buf = io.buf.ptr();
  auto allocator = buf->allocator();
  if (!buf->registered() || buf->builtin() || !buf->data_() ||
      allocator->mx != m_owner->mx() || allocator->type != Zi::IOBufType::TxUDP ||
      !allocator->pool || allocator->pool->type != Zi::IOBufType::TxUDP)
    return -1;
  unsigned length = buf->length;
  unsigned head = udpHead(dest.ip().type());
  unsigned offset = buf->skip;
  if (!head || offset < head) return -1;
  auto frame = buf->data_() + offset - head;
  unsigned capacity = buf->size - (offset - head);
  if (!m_owner->txReady(frame)) return -1;
  // Header framing uses provider headroom. Zi still owns the unchanged producer
  // descriptor and caller lease; the backend acquires its independent lease.
  bool multicast = dest.ip().multicast();
  uint8_t hops = multicast ? (m_info.options.ttl() ? uint8_t(m_info.options.ttl()) : 1) : 64;
  uint8_t tos = io.tos.is<uint8_t>() ? io.tos.p<uint8_t>() : 0;
  unsigned wire = writeUDP(frame, capacity, path,
    m_info.local.port(), dest.port(), length, tos, hops);
  if (!wire) return -1;
  uint8_t *loop = nullptr;
  if (multicast && (!m_info.options.multicast() || m_info.options.loopBack()) &&
      wire <= m_owner->m_ingress->size) {
    loop = m_owner->m_ingress->alloc();
    if (loop) memcpy(loop, frame, wire);
  }
  bool sent = m_owner->submitPacket(frame, wire, false);
  if (sent) m_owner->submittedPacket();
  if (loop) {
    if (sent)
      m_owner->rxRun([owner = m_owner, loop, wire] {
	IPView ip;
	if (owner->ready() && readIP({loop, wire}, ip)) owner->receivedUDP(loop, wire, ip);
	else owner->m_ingress->free(loop);
      });
    else m_owner->m_ingress->free(loop);
  }
  return sent ? int(length) : -1;
}

void MxPlugin::UdpCxn::sent(ZiConnection *cxn, int length)
{
  auto &io = cxn->txContext();
  auto original = io.buf;
  if (length >= 0) {
    auto offset = io.offset;
    if (cxn->executedSend(unsigned(length)) && io.buf == original && io.offset == offset)
      io.complete(); // A datagram is atomic. Retrying the same cursor would duplicate it.
    return;
  }
  bool finish = io.disconnected();
  io.length = length;
  while (!io.completed() && !io.closed() && !io()) { }
  if (finish || io.closed() || io.disconnected()) cxn->close();
  else if (!io.completed() && io.buf == original.ptr()) io.complete();
}

void MxPlugin::UdpCxn::close(bool peer)
{
  if (retiring) return;
  m_owner->m_udpBindings.delNode(this);
  if (m_membershipFD >= 0) {
    ::close(m_membershipFD);
    m_membershipFD = -1;
  }
  if (mxCxn) mxCxn->rxUp() = 0;
  m_owner->closingCxn(*this);
  m_owner->retireUDP(this, peer);
}

void MxPlugin::UdpCxn::releaseCxnRx()
{
  m_owner->releaseCxn(*this);
  m_owner->cxnRetired();
}

void MxPlugin::udp(
  ZiConnectFn connected, ZiFailFn fail, ZiIPType::T family, ZiIP local, uint16_t localPort,
  ZiIP remote, uint16_t remotePort, Zi::IOBufAllocator *allocator, ZiCxnOptions options)
{
  ZmAssert_(rxInvoked());
  if (!ready() || !connected || !options.udp() || !allocator ||
      allocator->pool != m_ingress.ptr() ||
      (family != ZiIPType::V4 && family != ZiIPType::V6) ||
      (local && local.type() != family) ||
      (remote && (remote.type() != family || !remotePort || remote.wildcard())) ||
      (options.multicast() && (!options.multicastValid(family) || options.ttl() > 255 ||
	(options.mifIndex() && options.mifIndex() != m_xdp->ifindex())))) {
    if (fail) fail(false);
    return;
  }

  IPPath path{};
  if (remote) {
    auto source = udpSource(local, remote, options);
    auto route = resolveUDPPath(m_xdp->ifindex(), {source, remote, {}, {}, 0});
    if (route.is<ZeException>()) {
      reportError(ZuMv(route.p<ZeException>()));
      if (fail) fail(true);
      return;
    }
    path = ZuMv(route.p<IPPath>());
    if (!local || local.wildcard()) local = path.local;
  } else if (!local) local = udpWildcard(family);

  auto available = [this, family, &local, &options](uint16_t port) {
    auto i = m_udpBindings.citer(udpKey(family, port));
    while (auto node = i()) {
      auto &info = static_cast<UdpCxn *>(node)->info();
      auto ip = info.local.ip();
      if ((local == ip || local.wildcard() || ip.wildcard()) &&
	  !(options.multicast() && info.options.multicast())) return false;
    }
    return true;
  };
  if (!localPort) {
    for (unsigned tries = ClientPorts::Count; tries; --tries) {
      auto candidate = uint16_t(m_nextUdpPort++);
      if (m_nextUdpPort == ClientPorts::End) m_nextUdpPort = ClientPorts::First;
      if (available(candidate)) { localPort = candidate; break; }
    }
  }
  if (!localPort || !available(localPort)) { if (fail) fail(true); return; }

  unsigned head = udpHead(family) - 14;
  unsigned mps = remote ? (path.mtu > head ? path.mtu - head : 0) : ~uint32_t(0);
  ZiCxnInfo info{.type = ZiCxnType::UDP, .socket = Zi::nullSocket(), .options = ZuMv(options),
    .local = ZiSockAddr{local, localPort}, .remote = remote ? ZiSockAddr{remote, remotePort} :
      ZiSockAddr{}, .mps = mps, .rxAllocator = allocator};
  ZmRef<UdpCxn> endpoint = new UdpCxn{this, info, ZuMv(path)};
  auto joined = endpoint->join();
  if (joined.is<ZeException>()) {
    reportError(ZuMv(joined.p<ZeException>()));
    if (fail) fail(false);
    return;
  }
  if (!reserveCxn()) { if (fail) fail(false); return; }
  admitCxn(*endpoint);
  establishedCxn(*endpoint);
  addCxn(endpoint);
  ZmREF(endpoint.ptr());
  m_udpBindings.addNode(endpoint.ptr());
  if (!publishUDP(endpoint, connected, ZuMv(info))) endpoint->close(false);
}

void MxPlugin::receivedUDP(uint8_t *frame, unsigned length, const IPView &ip)
{
  UDPView udp;
  if (readUDP(ip, udp)) {
    ZiSockAddr peer{ip.remote, udp.srcPort};
    auto offset = unsigned(udp.payload.data() - frame);
    auto key = udpKey(ip.local.type(), udp.dstPort);
    if (!ip.local.multicast()) {
      ZmRef<UdpCxn> match;
      {
	auto i = m_udpBindings.citer(key);
	while (auto node = i()) {
	  auto endpoint = static_cast<UdpCxn *>(node);
	  if (!endpoint->accepts(ip.local, peer)) continue;
	  if (!match || endpoint->info().local.ip() == ip.local) match = endpoint;
	  if (endpoint->info().local.ip() == ip.local && endpoint->info().remote) break;
	}
      }
      if (match && match->receive(frame, offset, udp.payload.length(), peer, ip.tos)) return;
    } else {
      ZmRef<UdpCxn> first;
      ZtArray<ZmRef<UdpCxn>, ZtArrayHeapID<"Zutcp.UDP.RxMatches">> matches;
      {
	auto i = m_udpBindings.citer(key);
	while (auto node = i()) {
	  auto endpoint = static_cast<UdpCxn *>(node);
	  if (!endpoint->accepts(ip.local, peer)) continue;
	  if (!first) first = endpoint;
	  else new (matches.push()) ZmRef{endpoint};
	}
      }
      for (unsigned j = 0, n = first ? 1 + matches.length() : 0; j < n; ++j) {
	auto endpoint = j ? matches[j - 1].ptr() : first.ptr();
	if (j + 1 == n) {
	  if (endpoint->receive(frame, offset, udp.payload.length(), peer, ip.tos)) return;
	} else if (endpoint->rxBuf()) {
	  if (auto copy = m_ingress->alloc()) {
	    memcpy(copy, frame, length);
	    if (!endpoint->receive(copy, offset, udp.payload.length(), peer, ip.tos))
	      m_ingress->free(copy);
	  }
	}
      }
    }
  }
  if (m_params.packet)
    m_params.packet(new Zi::IOBufStub<ZiIOBuf, ZiIOBuf_HeapID>{
      frame, length, &m_packetRx.p<1>(), this, length, false, true});
  else m_ingress->free(frame);
}

void MxPlugin::sendUDP(ZiConnection *cxn)
{
  static_cast<UdpCxn *>(cxn->plugin())->send(cxn);
}

void MxPlugin::closeUDP(ZiConnection *cxn, bool peer)
{
  if (!cxn->txUp()) return;
  ZmRef<UdpCxn> endpoint = static_cast<UdpCxn *>(cxn->plugin());
  cxn->txUp() = 0;
  cxn->txContext().complete();
  if (endpoint)
    rxRun([endpoint = ZuMv(endpoint), peer] { endpoint->close(peer); });
}

} // Zutcp
