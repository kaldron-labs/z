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

#include <zlib/ZuArray.hh>

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

IPPath MxPlugin::multicastPath(ZiIP local, ZiIP group, const ZiCxnOptions &options) const
{
  if (options.multicast() && !options.mif().wildcard()) local = options.mif();
  if (local.type() != group.type() || local.wildcard() || local.multicast()) return {};

  IPPath path{.local = local, .remote = group, .mtu = m_mtu};
  memcpy(path.localMAC, m_xdp->mac().data(), sizeof(path.localMAC));
  if (group.v6()) {
    path.remoteMAC[0] = path.remoteMAC[1] = 0x33;
    memcpy(path.remoteMAC + 2, group.in6Addr().s6_addr + 12, 4);
  } else {
    uint32_t ip = ZuBE(group.inAddr().s_addr);
    path.remoteMAC[0] = 0x01;
    path.remoteMAC[2] = 0x5e;
    path.remoteMAC[3] = (ip >> 16) & 0x7f;
    path.remoteMAC[4] = uint8_t(ip >> 8);
    path.remoteMAC[5] = uint8_t(ip);
  }
  return path;
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
  if (path.remote && !path.remote.multicast()) {
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
  auto ifindex = m_owner->m_params.routingIfindex;
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
  uint8_t *frame, unsigned offset, unsigned length, const ZiSockAddr &peer, uint8_t tos, ZiRxBurst *burst)
{
  auto cxn = mxCxn;
  if (burst) burst->boundary(cxn);
  auto stub = rxBuf();
  if (!stub) return false;
  auto &io = cxn->rxContext();
  auto owner = stub->owner;
  stub->data__ = uintptr_t(frame) | ZiIOBuf::Registered;
  stub->size = offset + length;
  stub->skip = offset;
  stub->length = length;
  io.addr = peer;
  io.tos = tos;
  io.flags = 0;
  io.burst = burst;
  while (!io.completed() && !io()) { }
  io.burst = nullptr;
  if (io.completed()) {
    if (burst) burst->flush();
    if (io.disconnected()) cxn->disconnect();
  } else if (!io.buf || io.buf.ptr() == stub) {
    if (burst) burst->flush();
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
    if (dest.ip().multicast()) {
      auto path = m_owner->multicastPath(m_info.local.ip(), dest.ip(), m_info.options);
      if (!path.local) sent(cxn, -1);
      else if (!sendPath(cxn, dest, path)) return;
      continue;
    }
    auto generation = m_owner->udpRouteGeneration();
    if (auto path = findPath(dest.ip(), generation)) {
      if (!sendPath(cxn, dest, *path)) return;
      continue;
    }
    m_owner->rxRun([endpoint = this, cxn, head = io.buf.ptr(), dest, generation] {
      if (endpoint->retiring) return;
      auto owner = endpoint->m_owner;
      auto local = endpoint->m_info.local.ip();
      auto route = owner->resolve(dest.ip(), local.multicast() ? ZiIP{} : local);
      owner->txRun([endpoint, cxn, head, dest, generation, route = ZuMv(route)]() mutable {
	endpoint->routed(cxn, head, dest, generation, ZuMv(route));
      });
    });
    return;
  }
  m_sending = false;
}

void MxPlugin::UdpCxn::routed(
  ZiConnection *cxn, ZiIOBuf *head, const ZiSockAddr &dest, uint64_t generation,
  ZuUnion<IPPath, ZeException> route)
{
  auto &io = cxn->txContext();
  if (cxn->txUp() && io.closed()) { cxn->close(); return; }
  if (cxn->txUp() && !io.completed() && io.buf == head && destination(io) == dest &&
      (m_owner->m_udpRouteFD < 0 || generation == m_owner->udpRouteGeneration())) {
    if (route.is<IPPath>()) {
      m_pathIndex = m_nextPath;
      m_paths[m_pathIndex] = ZuMv(route.p<IPPath>());
      m_nextPath = (m_nextPath + 1) % NPaths;
      if (m_pathCount < NPaths) ++m_pathCount;
      if (!sendPath(cxn, dest, m_paths[m_pathIndex])) return;
    } else {
      m_owner->reportError(ZuMv(route.p<ZeException>()));
      sent(cxn, -1);
    }
  }
  sendNext(cxn);
}

// Return false while the current head owns a future wakeup.
bool MxPlugin::UdpCxn::sendPath(ZiConnection *cxn, const ZiSockAddr &dest, const IPPath &path)
{
  auto &io = cxn->txContext();
  int result = submit(io, dest, path);
  if (result == Zi::NotReady) {
    m_parked = io.buf.ptr();
    // Admission may have synchronously retired the last submission.
    m_owner->waitTx(this);
    if (!txWaitPrev) backoff(Zm::now() + m_owner->txBackoff());
    return false;
  }
  sent(cxn, result);
  return true;
}

void MxPlugin::UdpCxn::backoff(ZuTime deadline)
{
  m_owner->mx()->add(&m_retryTimer, deadline, ZmScheduler::Update,
    [this, deadline](auto &&arm) {
      return arm([this, deadline] {
	if (!m_parked) return;
	// The scheduler coalesces near deadlines within its timer quantum.
	if (Zm::now() < deadline) backoff(deadline);
	else retryTx();
      });
    }, m_owner->txThread());
}

void MxPlugin::UdpCxn::retryTx()
{
  if (m_parked) resumeTx(m_owner->m_xdp->ready() ? Zi::OK : Zi::IOError);
}

void MxPlugin::UdpCxn::resumeTx(Zu::IO::Result result)
{
  if (!m_parked) return;
  auto cxn = mxCxn;
  if (!cxn || !cxn->txUp() || cxn->plugin() != this) return;
  auto &io = cxn->txContext();
  if (io.closed()) { cxn->close(); return; }
  if (io.buf.ptr() != m_parked) m_parked = nullptr;
  else if (result == Zi::IOError) sent(cxn, result);
  sendNext(cxn);
}

void MxPlugin::UdpCxn::cancelTx()
{
  m_owner->cancelTxWait(this);
  m_owner->mx()->del(&m_retryTimer);
  m_parked = nullptr;
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
  auto writable = m_owner->txReady(frame);
  if (writable == Zi::NotReady) {
    m_owner->reconcileTx();
    writable = m_owner->txReady(frame);
  }
  if (writable != Zi::OK) return writable;
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
  TxRetry retry{.spin = !m_parked};
  auto result = m_owner->submitPacket(frame, wire, false, nullptr, &retry);
  if (result == Zi::OK) m_owner->submittedPackets(1);
  if (loop) {
    if (result == Zi::OK)
      m_owner->rxRun([owner = m_owner, loop, wire] {
	IPView ip;
	if (owner->ready() && readIP({loop, wire}, ip)) owner->receivedUDP(loop, wire, ip, nullptr);
	else owner->m_ingress->free(loop);
      });
    else m_owner->m_ingress->free(loop);
  }
  return result == Zi::OK ? int(length) : int(result);
}

void MxPlugin::UdpCxn::sent(ZiConnection *cxn, int length)
{
  m_parked = nullptr;
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
    for (unsigned i = 0; i < ZutcpUDPRouteRxBudget; ++i) {
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
	(options.mifIndex() && options.mifIndex() != m_params.routingIfindex)))) {
    if (fail) fail(false);
    return;
  }

  IPPath path{};
  if (remote) {
    if (remote.multicast()) {
      path = multicastPath(local, remote, options);
      if (!path.local) { if (fail) fail(false); return; }
    } else {
      auto route = resolve(remote, local.multicast() ? ZiIP{} : local);
      if (route.is<ZeException>()) {
        reportError(ZuMv(route.p<ZeException>()));
        if (fail) fail(true);
        return;
      }
      path = ZuMv(route.p<IPPath>());
    }
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
  if (!publishUDP(endpoint, connected, ZuMv(info))) endpoint->close(false);
}

void MxPlugin::receivedUDP(uint8_t *frame, unsigned length, const IPView &ip, ZiRxBurst *burst)
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
      if (match && match->receive(frame, offset, udp.payload.length(), peer, ip.tos, burst)) return;
    } else {
      ZuArray<ZmRef<UdpCxn>, ZutcpUDPMcastMatchMax> matches;
      {
	auto i = m_udpBindings.citer(key);
	while (auto node = i()) {
	  auto endpoint = static_cast<UdpCxn *>(node);
	  if (!endpoint->accepts(ip.local, peer)) continue;
	  new (matches.push()) ZmRef{endpoint};
	  if (matches.length() == matches.N) break;
	}
      }
      for (unsigned j = 0, n = matches.length(); j < n; ++j) {
	auto endpoint = matches[j].ptr();
	if (j + 1 == n) {
	  if (endpoint->receive(frame, offset, udp.payload.length(), peer, ip.tos, burst)) return;
	} else if (endpoint->rxBuf()) {
	  if (auto copy = m_ingress->alloc()) {
	    memcpy(copy, frame, length);
	    if (!endpoint->receive(copy, offset, udp.payload.length(), peer, ip.tos, burst))
	      m_ingress->free(copy);
	  }
	}
      }
    }
  }
  if (burst) burst->flush();
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
  if (endpoint) endpoint->cancelTx();
  cxn->txUp() = 0;
  cxn->txContext().complete();
  if (endpoint)
    rxRun([endpoint = ZuMv(endpoint), peer] { endpoint->close(peer); });
}

} // Zutcp
