//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <arpa/inet.h>
#include <fcntl.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <poll.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <iostream>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmTime.hh>
#include <zlib/ZmThread.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZpktEnv.hh>

#include "ZpktTestCf.hh"

using namespace ZuTestUtil;

enum { Depth = 8, FrameSize = 1518, EtherType = 0x88b5 };

struct Frame {
  unsigned	length = 0;
  uint8_t	bytes[FrameSize]{};
};

class Peer {
public:
  ~Peer() {
    if (m_udp >= 0) ::close(m_udp);
    if (m_fd >= 0) ::close(m_fd);
  }

  bool open(const char *interface, const char *netns) {
    if (strchr(netns, '/')) return false;
    ZtString<> path;
    path << "/run/netns/" << netns;
    int home = ::open("/proc/self/ns/net", O_RDONLY | O_CLOEXEC);
    if (home < 0) return false;
    int peer = ::open(path.data(), O_RDONLY | O_CLOEXEC);
    struct stat homeInfo{}, peerInfo{};
    bool entered = peer >= 0 && !fstat(home, &homeInfo) && !fstat(peer, &peerInfo) &&
      (homeInfo.st_dev != peerInfo.st_dev || homeInfo.st_ino != peerInfo.st_ino) &&
      !setns(peer, CLONE_NEWNET);
    if (peer >= 0) ::close(peer);
    if (!entered) { ::close(home); return false; }
    bool ok = bind(interface);
    if (setns(home, CLONE_NEWNET) < 0) abort();
    ::close(home);
    return ok;
  }

  const uint8_t *mac() const { return m_mac; }
  in_addr ip() const { return m_ip; }

  bool send(const sockaddr_in &address, char byte) const {
    return sendto(m_udp, &byte, 1, 0,
      reinterpret_cast<const sockaddr *>(&address), sizeof(address)) == 1;
  }

  bool send(const Frame &frame) const {
    return sendto(m_fd, frame.bytes, frame.length, 0,
      reinterpret_cast<const sockaddr *>(&m_address), sizeof(m_address)) == frame.length;
  }

  bool expect(const Frame &frame) const {
    auto end = Zm::now() + ZuTime{3};
    while (Zm::now() < end) {
      pollfd fd{m_fd, POLLIN, 0};
      if (poll(&fd, 1, 50) <= 0) continue;
      uint8_t bytes[2048];
      iovec iov{bytes, sizeof(bytes)};
      alignas(cmsghdr) uint8_t control[CMSG_SPACE(sizeof(tpacket_auxdata))];
      msghdr message{};
      message.msg_iov = &iov;
      message.msg_iovlen = 1;
      message.msg_control = control;
      message.msg_controllen = sizeof(control);
      auto n = recvmsg(m_fd, &message, MSG_DONTWAIT);
      if (n < ETH_HLEN || (message.msg_flags & (MSG_TRUNC | MSG_CTRUNC))) continue;
      for (auto header = CMSG_FIRSTHDR(&message); header;
	  header = CMSG_NXTHDR(&message, header)) {
	if (header->cmsg_level != SOL_PACKET || header->cmsg_type != PACKET_AUXDATA ||
	    header->cmsg_len < CMSG_LEN(sizeof(tpacket_auxdata))) continue;
	tpacket_auxdata aux;
	memcpy(&aux, CMSG_DATA(header), sizeof(aux));
	if ((aux.tp_status & TP_STATUS_VLAN_VALID) && n <= ssize_t(sizeof(bytes)) - 4) {
	  memmove(bytes + 16, bytes + 12, n - 12);
	  uint16_t tpid = aux.tp_status & TP_STATUS_VLAN_TPID_VALID ?
	    aux.tp_vlan_tpid : ETH_P_8021Q;
	  bytes[12] = uint8_t(tpid >> 8);
	  bytes[13] = uint8_t(tpid);
	  bytes[14] = uint8_t(aux.tp_vlan_tci >> 8);
	  bytes[15] = uint8_t(aux.tp_vlan_tci);
	  n += 4;
	}
      }
      if (n == frame.length && !memcmp(bytes, frame.bytes, n)) return true;
    }
    return false;
  }

private:
  bool bind(const char *interface) {
    m_address.sll_family = AF_PACKET;
    m_address.sll_ifindex = int(if_nametoindex(interface));
    m_address.sll_protocol = htons(ETH_P_ALL);
    if (!m_address.sll_ifindex) return false;
    m_fd = socket(AF_PACKET, SOCK_RAW | SOCK_CLOEXEC, htons(ETH_P_ALL));
    if (m_fd < 0) return false;
    int on = 1;
    if (setsockopt(m_fd, SOL_PACKET, PACKET_IGNORE_OUTGOING, &on, sizeof(on)) < 0 ||
	setsockopt(m_fd, SOL_PACKET, PACKET_AUXDATA, &on, sizeof(on)) < 0 ||
	::bind(m_fd, reinterpret_cast<const sockaddr *>(&m_address), sizeof(m_address)) < 0)
      return false;
    sockaddr_ll address{};
    socklen_t len = sizeof(address);
    if (getsockname(m_fd, reinterpret_cast<sockaddr *>(&address), &len) < 0 ||
	address.sll_halen != ETH_ALEN) return false;
    memcpy(m_mac, address.sll_addr, ETH_ALEN);
    m_udp = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (m_udp < 0 || setsockopt(m_udp, SOL_SOCKET, SO_BINDTODEVICE,
	interface, strlen(interface) + 1) < 0) return false;
    ifreq request{};
    if (strlen(interface) >= sizeof(request.ifr_name)) return false;
    memcpy(request.ifr_name, interface, strlen(interface) + 1);
    if (ioctl(m_udp, SIOCGIFADDR, &request) < 0) return false;
    auto ip = reinterpret_cast<sockaddr_in *>(&request.ifr_addr);
    m_ip = ip->sin_addr;
    ip->sin_port = 0;
    return !::bind(m_udp, reinterpret_cast<sockaddr *>(ip), sizeof(*ip));
  }

  int		m_fd = -1, m_udp = -1;
  in_addr	m_ip{};
  sockaddr_ll	m_address{};
  uint8_t	m_mac[ETH_ALEN]{};
};

class UdpProbe {
public:
  ~UdpProbe() { if (m_fd >= 0) ::close(m_fd); }

  bool open(const char *interface) {
    m_fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (m_fd < 0) return false;
    ifreq request{};
    if (strlen(interface) >= sizeof(request.ifr_name)) return false;
    memcpy(request.ifr_name, interface, strlen(interface) + 1);
    if (ioctl(m_fd, SIOCGIFADDR, &request) < 0) return false;
    memcpy(&m_address, &request.ifr_addr, sizeof(m_address));
    m_address.sin_port = 0;
    socklen_t size = sizeof(m_address);
    return !::bind(m_fd, reinterpret_cast<sockaddr *>(&m_address), size) &&
      !getsockname(m_fd, reinterpret_cast<sockaddr *>(&m_address), &size);
  }

  const sockaddr_in &address() const { return m_address; }

  bool expect(int value) const {
    pollfd fd{m_fd, POLLIN, 0};
    int n = poll(&fd, 1, value < 0 ? 250 : 3000);
    if (value < 0) return !n;
    char byte = 0;
    return n > 0 && recv(m_fd, &byte, 1, MSG_DONTWAIT) == 1 && byte == value;
  }

  bool selfTest() const {
    char byte = 'L';
    return sendto(m_fd, &byte, 1, 0, reinterpret_cast<const sockaddr *>(&m_address),
      sizeof(m_address)) == 1 && expect(byte);
  }

private:
  int		m_fd = -1;
  sockaddr_in	m_address{};
};

static Frame frame(const uint8_t *dst, const uint8_t *src, unsigned size, bool tagged)
{
  Frame result;
  result.length = size;
  memset(result.bytes, size & 0xff, size);
  memcpy(result.bytes, dst, ETH_ALEN);
  memcpy(result.bytes + ETH_ALEN, src, ETH_ALEN);
  unsigned offset = 12;
  if (tagged) {
    result.bytes[12] = 0x81;
    result.bytes[13] = 0;
    result.bytes[14] = 0;
    result.bytes[15] = 7;
    offset += 4;
  }
  result.bytes[offset] = EtherType >> 8;
  result.bytes[offset + 1] = EtherType & 0xff;
  return result;
}

static Frame arp(const uint8_t *source, in_addr sender, in_addr target)
{
  constexpr uint8_t broadcast[] = {255, 255, 255, 255, 255, 255};
  auto out = frame(broadcast, source, 60, false);
  out.bytes[12] = 0x08;
  out.bytes[13] = 0x06;
  auto payload = out.bytes + ETH_HLEN;
  memset(payload, 0, out.length - ETH_HLEN);
  constexpr uint8_t header[] = {0, 1, 0x08, 0, 6, 4, 0, 1};
  memcpy(payload, header, sizeof(header));
  memcpy(payload + 8, source, ETH_ALEN);
  memcpy(payload + 14, &sender, sizeof(sender));
  memcpy(payload + 24, &target, sizeof(target));
  return out;
}

static Frame solicitation(const uint8_t *source)
{
  constexpr uint8_t multicast[] = {0x33, 0x33, 0xff, 0, 0, 2};
  constexpr unsigned IPv6 = 40, Icmp = 32;
  auto out = frame(multicast, source, ETH_HLEN + IPv6 + Icmp, false);
  out.bytes[12] = 0x86;
  out.bytes[13] = 0xdd;
  auto ip = out.bytes + ETH_HLEN;
  memset(ip, 0, IPv6 + Icmp);
  ip[0] = 0x60;
  ip[5] = Icmp;
  ip[6] = IPPROTO_ICMPV6;
  ip[7] = 255;
  inet_pton(AF_INET6, "fe80::1", ip + 8);
  inet_pton(AF_INET6, "ff02::1:ff00:2", ip + 24);
  auto icmp = ip + IPv6;
  icmp[0] = 135; // Neighbor Solicitation
  inet_pton(AF_INET6, "fe80::2", icmp + 8);
  icmp[24] = 1; // source link layer address
  icmp[25] = 1;
  memcpy(icmp + 26, source, ETH_ALEN);
  unsigned sum = Icmp + IPPROTO_ICMPV6;
  for (unsigned i = 8; i < IPv6 + Icmp; i += 2)
    sum += (unsigned(ip[i]) << 8) | ip[i + 1];
  while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
  sum = ~sum;
  icmp[2] = uint8_t(sum >> 8);
  icmp[3] = uint8_t(sum);
  return out;
}

static uint64_t ns(clockid_t clock)
{
  timespec time{};
  if (clock_gettime(clock, &time) < 0) return 0;
  return uint64_t(time.tv_sec) * 1000000000 + uint64_t(time.tv_nsec);
}

struct Fixture {
  ZiMultiplex		mx;
  ZmRef<Zpkt::Env>	env = new Zpkt::Env;
  ZmRef<Zpkt::Nif>	nif;
  ZmRef<Zi::IOBufPool>	ingress, tx;
  Zi::IOBufPoolList	pools;
  ZmSemaphore		done;
  Frame			expected;
  bool			received = false;

  ~Fixture() {
    if (nif && nif->state() != ZmEngineState::Stopped) nif->stop();
    if (nif) nif->final();
    if (mx.running()) mx.stop();
  }

  bool init(const char *name) {
    auto cf = ZpktTest::object();
    ZpktTest::value(cf.ptr(), "module", ZPKT_MLX_MODULE);
    auto entry = ZpktTest::object(ZpktTest::object(cf.ptr(), "interfaces"), name);
    ZpktTest::value(entry, "rxDepth", Depth);
    ZpktTest::value(entry, "txDepth", Depth);
    try { env->init(cf.ptr()); }
    catch (const ZeException &err) { log(err); return false; }
    auto opened = env->nif(&mx, name);
    if (opened.is<ZeException>()) { log(opened.p<ZeException>()); return false; }
    nif = opened.p<Zpkt::Nif *>();
    if (nif->mtu() != 1500 || nif->rxQueues() != 1) return false;
    ingress = nif->bufPool(Zi::IOBufType::Rx, Depth + 1, FrameSize);
    tx = nif->bufPool(Zi::IOBufType::TxUDP, Depth, FrameSize);
    pools.pushNode(tx.ptr());
    auto allocated = nif->alloc(ingress.ptr(), pools);
    if (allocated.is<ZeException>()) { log(allocated.p<ZeException>()); return false; }
    return mx.start();
  }

  bool start() {
    if (nif->start([this](uint8_t *data, unsigned length, ZiRxBurst *) {
      if (length == expected.length && !memcmp(data, expected.bytes, length)) {
	received = true;
	nif->wake();
      }
      ingress->free(data);
    })) return nif->zeroCopy() && !nif->tcpChksumOffload();
    if (auto error = nif->error()) log(error);
    return false;
  }

  bool receive(const Peer &peer, Frame input) {
    expected = input;
    received = false;
    mx.rxRun([this] { nif->rx(); done.post(); });
    bool sent = peer.send(input);
    bool timedOut = !sent || done.timedwait(Zm::now() + ZuTime{3});
    if (timedOut) { nif->wake(); done.wait(); }
    return !timedOut && received;
  }

  bool pressure(const Peer &peer, const Frame &input) {
    ZmSemaphore arrived;
    uint8_t *held[Depth + 1]{};
    unsigned count = 0;
    if (!nif->start([this, &input, &arrived, &held, &count](
	uint8_t *data, unsigned length, ZiRxBurst *) {
      if (length == input.length && !memcmp(data, input.bytes, length)) {
	if (count < Depth + 1) held[count++] = data;
	else ingress->free(data);
	arrived.post();
      } else {
	ingress->free(data);
      }
    })) return false;

    bool ok = true;
    for (unsigned i = 0; i < Depth + 1; ++i) ok &= peer.send(input);
    // The Rx owner intentionally starts after the entire test burst is on wire.
    Zm::sleep(ZuTime{0, 50000000});
    mx.rxRun([this] { nif->rx(); done.post(); });
    auto deadline = Zm::now() + ZuTime{3};
    for (unsigned i = 0; i < Depth && ok; ++i) ok = !arrived.timedwait(deadline);
    if (ok) ok = arrived.timedwait(Zm::now() + ZuTime{0, 100000000}) != 0;
    if (ok) {
      // Consume the one spare backing slot and verify that nothing is posted.
      ok = peer.send(input) && !arrived.timedwait(Zm::now() + ZuTime{2});
      if (ok) ok = peer.send(input) &&
	arrived.timedwait(Zm::now() + ZuTime{0, 100000000}) != 0;
    }
    if (ok) {
      ingress->free(held[0]);
      held[0] = nullptr;
      ok = false;
      for (unsigned i = 0; i < 20 && !ok; ++i) {
	if (!peer.send(input)) break;
	ok = !arrived.timedwait(Zm::now() + ZuTime{0, 50000000});
      }
    }
    bool stopped;
    if (ok) {
      ZmAtomic<unsigned> sending{1};
      ZmSemaphore first;
      ZmThread producer{[&peer, &input, &sending, &first] {
	peer.send(input);
	first.post();
	while (sending) {
	  peer.send(input);
	  Zm::sleep(ZuTime{0, 100000});
	}
      }};
      first.wait();
      stopped = nif->stop();
      sending = 0;
      producer.join();
    } else {
      stopped = nif->stop();
    }
    done.wait();
    ZuTestMgr::indent();
    std::cout << "# mlx Rx burst offered " << Depth + 1 << " against ring depth " << Depth
      << ", retained " << count << " slots including spare backing\n";
    for (auto data : held) if (data) ingress->free(data);
    return ok && stopped;
  }

  bool latency(const Peer &peer, Frame output, Frame input) {
    if (!start()) return false;
    auto packet = tx->alloc();
    bool ok = bool(packet);
    enum { Samples = 16 };
    uint64_t total = 0, cpu = ns(CLOCK_PROCESS_CPUTIME_ID);
    for (unsigned i = 0; ok && i < Samples; ++i) {
      output.bytes[output.length - 1] = uint8_t(i);
      input.bytes[input.length - 1] = uint8_t(i);
      memcpy(packet, output.bytes, output.length);
      auto begin = ns(CLOCK_MONOTONIC);
      ok = onTx([this, packet, length = output.length] {
	return nif->send(packet, length) == Zi::OK;
      }) && peer.expect(output) && receive(peer, input);
      total += ns(CLOCK_MONOTONIC) - begin;
      if (ok) ok = onTx([this] {
	auto end = Zm::now() + ZuTime{1};
	while (Zm::now() < end)
	  if (nif->pollTx()) return true;
	return false;
      });
    }
    cpu = ns(CLOCK_PROCESS_CPUTIME_ID) - cpu;
    if (packet) tx->free(packet);
    ok = nif->stop() && ok;
    if (ok) {
      ZuTestMgr::indent();
      std::cout << "# mlx single-frame wire round trip mean ns " << total/Samples
	<< ", process CPU ns per exchange " << cpu/Samples
	<< " (" << Samples << " samples, includes fixture scheduling)\n";
    }
    return ok;
  }

  template <typename Fn> bool onTx(Fn fn) {
    return ZmBlock<bool>{}([this, &fn](auto complete) {
      mx.txRun([&fn, complete]() mutable { complete(fn()); });
    });
  }
};

static void packets(const char *device, const char *peerName, const char *peerNS)
{
  ZuTestScope(packets);
  Peer peer;
  if (!peer.open(peerName, peerNS)) { ZuCHECK(false, "open cabled peer"); return; }
  UdpProbe udp, unrelated;
  if (!udp.open(device) || !unrelated.open("lo")) {
    ZuCHECK(false, "bind host UDP probes");
    return;
  }
  ZuCHECK(peer.send(udp.address(), 'B') && udp.expect('B'), "kernel ingress before open");
  Fixture fixture;
  if (!fixture.init(device)) {
    ZuCHECK(false, "open and allocate mlx packet service");
    return;
  }
  ZuCHECK(peer.send(udp.address(), 'O') && udp.expect(-1),
    "open claims ingress before activation");
  if (!fixture.start()) {
    ZuCHECK(false, "open and start registered mlx packet service");
    return;
  }

  ZuCHECK(peer.send(udp.address(), 'R') && udp.expect(-1) && unrelated.selfTest(),
    "running mlx suppresses host copies while other interfaces still deliver");
  auto mac = fixture.nif->mac().data();
  uint8_t other[] = {0x02, 0x01, 0, 0, 0, 0x7f};
  uint8_t multicast[] = {0x01, 0, 0x5e, 0, 0, 0x31};
  uint8_t broadcast[] = {255, 255, 255, 255, 255, 255};
  ZuCHECK(fixture.receive(peer, frame(mac, peer.mac(), 60, false)), "minimum Ethernet frame");
  ZuCHECK(fixture.receive(peer, frame(mac, peer.mac(), FrameSize, true)), "full MTU VLAN bytes");
  ZuCHECK(fixture.receive(peer, frame(other, peer.mac(), 60, false)), "other unicast");
  ZuCHECK(fixture.receive(peer, frame(multicast, peer.mac(), 60, false)), "multicast");
  ZuCHECK(fixture.receive(peer, frame(broadcast, peer.mac(), 60, false)), "broadcast");
  ZuCHECK(fixture.receive(peer, arp(peer.mac(), peer.ip(), udp.address().sin_addr)), "ARP");
  ZuCHECK(fixture.receive(peer, solicitation(peer.mac())), "IPv6 Neighbor Solicitation");

  auto output = frame(peer.mac(), mac, FrameSize, true);
  auto packet = fixture.tx->alloc();
  if (!packet) { ZuCHECK(false, "allocate physical Tx slot"); return; }
  memcpy(packet, output.bytes, output.length);
  bool sent = fixture.onTx([&fixture, &output, packet] {
    return fixture.nif->send(packet, output.length) == Zi::OK &&
      fixture.nif->txReady(packet) == Zi::NotReady;
  });
  ZuCHECK(sent && peer.expect(output), "peer receives exact tagged bytes from registered backing");
  ZuCHECK(fixture.onTx([&fixture, packet] {
    auto end = Zm::now() + ZuTime{1};
    while (Zm::now() < end)
      if (fixture.nif->pollTx()) return fixture.nif->txReady(packet) == Zi::OK;
    return false;
  }), "isolated hardware send retires with no subsequent submission");
  ZuCHECK(fixture.nif->stop() && fixture.start() &&
      fixture.onTx([&fixture, packet, &output] {
	return fixture.nif->send(packet, output.length) == Zi::OK;
      }) && peer.expect(output),
    "restart transmits again from retained backing");
  fixture.tx->free(packet);
  ZuCHECK(fixture.nif->stop(), "stop drains outstanding hardware submission");
  ZuCHECK(fixture.latency(peer, frame(peer.mac(), mac, 60, false),
      frame(mac, peer.mac(), 60, false)), "single-frame wire latency and CPU baseline");
  ZuCHECK(fixture.pressure(peer, frame(mac, peer.mac(), 128, false)),
    "Rx ring overflow, pool exhaustion, off-owner replenishment and stop during traffic");
  ZuCHECK(peer.send(udp.address(), 'S') && udp.expect(-1), "ingress stays claimed after stop");
  fixture.nif->final();
  ZuCHECK(!fixture.nif->env() && peer.send(udp.address(), 'F') && udp.expect('F'),
    "kernel ingress resumes after finalization");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  auto device = getenv("ZPKT_MLX_IFACE");
  auto peer = getenv("ZPKT_MLX_PEER_IFACE");
  auto ns = getenv("ZPKT_MLX_PEER_NETNS");
  if (!device && !peer && !ns) {
    std::cout << "1..0 # SKIP Mellanox wire fixture is not configured\n";
    return 0;
  }
  if (!device || !*device || !peer || !*peer || !ns || !*ns) {
    std::cout << "1..1\nnot ok 1 - incomplete Mellanox wire fixture\n";
    return 1;
  }
  ZiLog::init("zpktmlx_test");
  ZiLog::start();
  ZuTestMain();
  ZuTestCall(packets, device, peer, ns);
  ZiLog::stop();
}
