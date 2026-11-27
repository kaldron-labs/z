//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/filter.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <poll.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <iostream>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmTime.hh>
#include <zlib/ZiLog.hh>
#include <zlib/Zutcp.hh>

#include "ZpktTestCf.hh"

using namespace ZuTestUtil;

static constexpr char Request[] = "Zutcp mlx request";
static constexpr char Reply[] = "Linux kernel reply via mlx";
static constexpr char LocalIP[] = "198.18.17.1";
static constexpr char PeerIP[] = "198.18.17.2";

class FD {
public:
  FD() = default;
  explicit FD(int fd) : m_fd{fd} { }
  FD(const FD &) = delete;
  FD &operator =(const FD &) = delete;
  ~FD() { if (m_fd >= 0) ::close(m_fd); }

  int fd() const { return m_fd; }
  void fd(int value) { ZmAssert_(m_fd < 0); m_fd = value; }
  explicit operator bool() const { return m_fd >= 0; }

private:
  int m_fd = -1;
};

static bool timeout(int fd)
{
  timeval limit{8, 0};
  return !setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &limit, sizeof(limit)) &&
    !setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &limit, sizeof(limit));
}

static unsigned bindPeer(FD &socketFD, int type, const char *name)
{
  socketFD.fd(socket(AF_INET, type | SOCK_CLOEXEC, 0));
  if (!socketFD) return 0;
  int fd = socketFD.fd();
  sockaddr_in address{};
  address.sin_family = AF_INET;
  inet_pton(AF_INET, PeerIP, &address.sin_addr);
  if (!timeout(fd) || setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, name, strlen(name) + 1) ||
      bind(fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address))) return 0;
  socklen_t size = sizeof(address);
  if (getsockname(fd, reinterpret_cast<sockaddr *>(&address), &size)) return 0;
  return ntohs(address.sin_port);
}

class Peer {
public:
  bool init(const char *name, const char *ns)
  {
    if (strchr(ns, '/')) return false;
    ZtString<> path;
    path << "/run/netns/" << ns;
    FD home{open("/proc/self/ns/net", O_RDONLY | O_CLOEXEC)};
    FD peer{open(path.data(), O_RDONLY | O_CLOEXEC)};
    struct stat homeInfo{}, peerInfo{};
    if (!home || !peer || fstat(home.fd(), &homeInfo) || fstat(peer.fd(), &peerInfo) ||
	(homeInfo.st_dev == peerInfo.st_dev && homeInfo.st_ino == peerInfo.st_ino) ||
	setns(peer.fd(), CLONE_NEWNET)) return false;
    bool ok = openPeer(name);
    ZmAssert_(!setns(home.fd(), CLONE_NEWNET));
    return ok;
  }

  unsigned tcpPort() const { return m_tcpPort; }
  unsigned udpPort() const { return m_udpPort; }
  int acceptTCP() const { return accept4(m_tcp.fd(), nullptr, nullptr, SOCK_CLOEXEC); }

  bool tcpReply(int fd) const
  {
    char buf[sizeof(Request) - 1];
    return recv(fd, buf, sizeof(buf), MSG_WAITALL) == sizeof(buf) &&
      !memcmp(buf, Request, sizeof(buf)) &&
      send(fd, Reply, sizeof(Reply) - 1, MSG_NOSIGNAL) == sizeof(Reply) - 1;
  }

  bool udpReply() const
  {
    sockaddr_in address{};
    socklen_t size = sizeof(address);
    char buf[128];
    auto n = recvfrom(m_udp.fd(), buf, sizeof(buf), 0,
      reinterpret_cast<sockaddr *>(&address), &size);
    return n == sizeof(Request) - 1 && !memcmp(buf, Request, n) &&
      sendto(m_udp.fd(), Reply, sizeof(Reply) - 1, MSG_NOSIGNAL,
	reinterpret_cast<const sockaddr *>(&address), size) == sizeof(Reply) - 1;
  }

  // Observe exact copies of the request while TCP's receive socket drops them.
  bool retransmitted() const
  {
    bool first = false;
    uint32_t sequence = 0;
    auto end = Zm::now(8);
    in_addr src{}, dst{};
    inet_pton(AF_INET, LocalIP, &src);
    inet_pton(AF_INET, PeerIP, &dst);
    while (Zm::now() < end) {
      pollfd fd{m_wire.fd(), POLLIN, 0};
      if (poll(&fd, 1, 50) <= 0) continue;
      uint8_t packet[2048];
      auto n = recv(fd.fd, packet, sizeof(packet), MSG_DONTWAIT);
      if (n < ETH_HLEN + 40 || packet[12] != (ETH_P_IP>>8) ||
	  packet[13] != (ETH_P_IP & 0xff)) continue;
      auto ip = packet + ETH_HLEN;
      unsigned hlen = (ip[0] & 15)<<2, length = (unsigned(ip[2])<<8) | ip[3];
      if (ip[0]>>4 != 4 || ip[9] != IPPROTO_TCP || hlen < 20 ||
	  length < hlen + 20 || length > unsigned(n) - ETH_HLEN ||
	  memcmp(ip + 12, &src, 4) || memcmp(ip + 16, &dst, 4)) continue;
      auto tcp = ip + hlen;
      unsigned off = (tcp[12]>>4)<<2;
      if (((unsigned(tcp[2])<<8) | tcp[3]) != m_tcpPort || off < 20 ||
	  length != hlen + off + sizeof(Request) - 1 ||
	  memcmp(tcp + off, Request, sizeof(Request) - 1)) continue;
      uint32_t seq;
      memcpy(&seq, tcp + 4, sizeof(seq));
      if (first && seq == sequence) return true;
      first = true;
      sequence = seq;
    }
    return false;
  }

private:
  bool openPeer(const char *interface)
  {
    auto index = if_nametoindex(interface);
    if (!index) return false;
    m_tcpPort = bindPeer(m_tcp, SOCK_STREAM, interface);
    m_udpPort = bindPeer(m_udp, SOCK_DGRAM, interface);
    if (!m_tcpPort || !m_udpPort || listen(m_tcp.fd(), 2)) return false;
    m_wire.fd(socket(AF_PACKET, SOCK_RAW | SOCK_CLOEXEC, htons(ETH_P_IP)));
    if (!m_wire) return false;
    sockaddr_ll address{};
    address.sll_family = AF_PACKET;
    address.sll_protocol = htons(ETH_P_IP);
    address.sll_ifindex = int(index);
    return !bind(m_wire.fd(), reinterpret_cast<const sockaddr *>(&address), sizeof(address));
  }

  FD		m_tcp, m_udp, m_wire;
  unsigned	m_tcpPort = 0, m_udpPort = 0;
};

template <bool UDP> struct Input : ZiIOBuf { using ZiIOBuf::ZiIOBuf; };

struct Exchange;

class Connection : public ZiConnection, public ZmNHeap<Connection> {
friend inline ZuStringT<"Zutcp.MlxTest.Connection"> ZmNHeapID(Connection *);
public:
  Connection(ZiMultiplex *mx, const ZiCxnInfo &info, Exchange *exchange) :
    ZiConnection{mx, info}, m_exchange{exchange} { }
  void connected(ZiRxContext &) override;
  void disconnected(bool) override;
  void write();

private:
  bool received(ZiRxContext &);
  Exchange	*m_exchange;
  unsigned	m_rxBytes = 0;
};

struct Exchange {
  Zi::IOBufAllocator	*rx, *tx;
  bool			udp;
  ZmRef<ZiIOBuf>	retained;
  uint8_t		*backing = nullptr;
  ZmRef<Connection>	cxn;
  ZmSemaphore		opened, input, output;
  ZmAtomic<unsigned>	errors{0};

  ZiConnectFn connect(ZiMultiplex *mx) {
    return ZiConnectFn{[this, mx](const ZiCxnInfo &info) -> ZiConnection * {
      cxn = new Connection{mx, info, this};
      return cxn.ptr();
    }};
  }
  ZiFailFn failed() {
    return ZiFailFn{[this](bool) { ++errors; opened.post(); }};
  }
};

void Connection::connected(ZiRxContext &io)
{
  auto state = m_exchange;
  auto buf = state->rx->allocBuf(this);
  if (!buf || info().rxAllocator != state->rx || !Zi::nullSocket(info().socket) ||
      (info().type == ZiCxnType::UDP) != state->udp) {
    ++state->errors;
  } else {
    io.init(ZiRxFn{this, ZmFnPtr<&Connection::received>{}}, ZuMv(buf));
  }
  state->opened.post();
}

void Connection::disconnected(bool) { }

bool Connection::received(ZiRxContext &io)
{
  auto state = m_exchange;
  constexpr unsigned length = sizeof(Reply) - 1;
  if (!state->udp && (!io.buf || !io.buf->length)) return true;
  if (!io.buf || io.buf->allocator() != state->rx || !io.buf->registered() ||
      io.buf->length > length - m_rxBytes ||
      (state->udp && (io.buf->length != length || !dynamic_cast<Input<true> *>(io.buf.ptr()))) ||
      (!state->udp && !dynamic_cast<Input<false> *>(io.buf.ptr())) ||
      memcmp(io.buf->data(), Reply + m_rxBytes, io.buf->length)) {
    ++state->errors;
    state->input.post();
    io.disconnect();
    return true;
  }
  m_rxBytes += io.buf->length;
  io.buf = state->rx->allocBuf(this);
  if (!io.buf) { ++state->errors; io.disconnect(); }
  if (m_rxBytes == length || state->errors) state->input.post();
  return true;
}

void Connection::write()
{
  send(ZiTxFn{[this](ZiTxContext &io) {
    auto state = m_exchange;
    auto buf = state->tx->allocBuf(this);
    if (!buf || !buf->registered() || !buf->append(ZuBSpan{Request, sizeof(Request) - 1})) {
      ++state->errors;
      io.complete();
      state->output.post();
      return false;
    }
    state->backing = buf->data_();
    state->retained = buf;
    io.init(ZiTxFn{[state](ZiTxContext &io) {
      if (io.buf.ptr() != state->retained.ptr() || io.length != sizeof(Request) - 1 ||
	  (state->udp && io.buf->data_() != state->backing))
	++state->errors;
      io.complete();
      state->output.post();
      return true;
    }}, ZuMv(buf));
    return true;
  }});
}

static bool wait(ZmSemaphore &sem) { return !sem.timedwait(Zm::now(8)); }

static bool openNative(Exchange &state, ZiMultiplex &mx, unsigned port)
{
  if (state.udp) {
    mx.udp(state.connect(&mx), state.failed(), ZiIP{LocalIP}, 0, ZiIP{PeerIP}, port,
      state.rx, ZiCxnOptions{}.udp(true));
  } else {
    mx.connect(state.connect(&mx), state.failed(), ZiIP{LocalIP}, 0, ZiIP{PeerIP}, port,
      state.rx);
  }
  return wait(state.opened) && state.cxn && !state.errors;
}

static bool sendTCP(Exchange &state, ZiMultiplex &mx, const Peer &peer, bool loss)
{
  if (!openNative(state, mx, peer.tcpPort())) return false;
  FD socket{peer.acceptTCP()};
  if (!socket || !timeout(socket.fd())) return false;

  // This filter affects only the fixture's accepted TCP socket. Packet
  // capture still observes the data on the cabled peer before the drop.
  if (loss) {
    sock_filter drop[]{{BPF_RET | BPF_K, 0, 0, 0}};
    sock_fprog filter{1, drop};
    if (setsockopt(socket.fd(), SOL_SOCKET, SO_ATTACH_FILTER, &filter, sizeof(filter)))
      return false;
  }
  state.cxn->write();
  if (loss) {
    if (!peer.retransmitted() || !wait(state.output) || state.errors) return false;
    bool retained = ZmBlock<bool>{}([&mx, &state](auto done) {
      mx.txRun([&state, done]() mutable {
	// The held probe and the unacknowledged source are the only slots
	// in this application pool. The source metadata must be detached.
	done(state.retained && !state.retained->data_() && !state.tx->allocBuf());
      });
    });
    if (!retained) return false;
    int ignored = 0;
    if (setsockopt(socket.fd(), SOL_SOCKET, SO_DETACH_FILTER, &ignored, sizeof(ignored)))
      return false;
  }
  if (!peer.tcpReply(socket.fd()) || !wait(state.input) || (!loss && !wait(state.output)))
    return false;
  return !state.errors;
}

static bool sendUDP(Exchange &state, ZiMultiplex &mx, const Peer &peer)
{
  if (!openNative(state, mx, peer.udpPort())) return false;
  state.cxn->write();
  if (!peer.udpReply() || !wait(state.input) || !wait(state.output)) return false;
  return !state.errors;
}

static void wire(const char *device, const char *peerName, const char *peerNS)
{
  ZuTestScope(wire);
  Peer peer;
  if (!peer.init(peerName, peerNS)) {
    ZuCHECK(false, "open TCP and UDP kernel sockets in configured peer namespace");
    return;
  }
  ZmRef<Zpkt::Env> env = new Zpkt::Env;
  ZmRef<Zutcp::MxPlugin> transport = new Zutcp::MxPlugin;
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(4).thread(4, [](auto &t) { t.isolated(true); });
  }), transport};
  auto cf = ZpktTest::object();
  ZpktTest::value(cf.ptr(), "module", ZUTCP_MLX_MODULE);
  auto entry = ZpktTest::object(ZpktTest::object(cf.ptr(), "interfaces"), device);
  ZpktTest::value(entry, "rxDepth", 64);
  ZpktTest::value(entry, "txDepth", 64);
  ZmRef<Zpkt::Nif> ref;
  Zpkt::Nif *nif = nullptr;
  try {
    env->init(cf.ptr());
    auto opened = env->nif(&mx, device);
    if (opened.is<ZeException>()) throw ZuMv(opened).p<ZeException>();
    ref = opened.p<Zpkt::Nif *>();
    nif = ref.ptr();
    transport->init(&mx, env, ZuMv(ref), {});
  } catch (const ZeException &error) {
    if (ref) ref->final();
    log(error);
    ZuCHECK(false, "open and install physical mlx device on Zutcp");
    return;
  }
  auto tcpRx = mx.allocator<Zi::IOBufType::Rx, Input<false>>();
  auto udpRx = mx.allocator<Zi::IOBufType::Rx, Input<true>>();
  auto tcpTx = mx.allocator<Zi::IOBufType::TxTCP>(2);
  auto udpTx = mx.allocator<Zi::IOBufType::TxUDP>(16);
  if (!tcpRx || !udpRx || !tcpTx || !udpTx || tcpRx->pool != udpRx->pool) {
    ZuCHECK(false, "register typed TCP and UDP packet allocators sharing ingress");
    return;
  }
  auto allocated = mx.alloc();
  if (allocated.is<ZeException>()) {
    log(allocated.p<ZeException>());
    ZuCHECK(false, "provision physical packet pools");
    return;
  }
  auto hold = tcpTx->allocBuf();
  if (!hold || !hold->registered() || !hold->append(ZuBSpan{Request, sizeof(Request) - 1})) {
    ZuCHECK(false, "hold registered Tx backing across restart");
    return;
  }
  auto backing = hold->data_();
  for (unsigned cycle = 0; cycle < 2; ++cycle) {
    Exchange tcp{tcpRx, tcpTx, false}, udp{udpRx, udpTx, true};
    if (!mx.start()) {
      if (auto err = nif->error()) log(err);
      ZuCHECK(false, "start native mlx transport");
      break;
    }
    ZuCHECK(nif->zeroCopy() && !nif->tcpChksumOffload(),
      "active mlx service uses registered DMA and caller checksums");
    bool tcpOK = sendTCP(tcp, mx, peer, !cycle);
    bool udpOK = tcpOK && sendUDP(udp, mx, peer);
    ZuCHECK(tcpOK, cycle ? "TCP request and reply through kernel peer after restart" :
      "exact TCP request and reply after observed wire retransmission");
    ZuCHECK(udpOK, "exact UDP datagram and reply through kernel peer");
    if (tcpOK && tcp.cxn->up()) tcp.cxn->close();
    if (udpOK && udp.cxn->up()) udp.cxn->close();
    bool stopped = mx.stop();
    ZuCHECK((stopped && hold->data_() == backing &&
      hold->cspan() == ZuBSpan{Request, sizeof(Request) - 1}),
      "drain transport and preserve allocated packet addresses");
    auto recycled = stopped ? tcpTx->allocBuf() : nullptr;
    ZuCHECK(tcpOK && tcp.retained && !tcp.retained->data_() &&
      recycled && recycled->data_() == tcp.backing,
      "the original TCP backing returns to its pool after transport drain");
    if (!stopped || !udpOK) break;
  }
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
  ZiLog::init("zutcpmlxtest");
  ZiLog::start();
  ZuTestMain();
  ZuTestCall(wire, device, peer, ns);
  ZiLog::stop();
}
