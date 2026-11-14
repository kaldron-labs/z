//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>
#include <net/if.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmAssert.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmTime.hh>
#include <zlib/ZutcpIP.hh>
#include <zlib/ZutcpMx.hh>

using namespace ZuTestUtil;

static bool mac(unsigned index, uint8_t *out)
{
  ifreq request{};
  if (!if_indextoname(index, request.ifr_name)) return false;
  int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if (fd < 0) return false;
  bool ok = !ioctl(fd, SIOCGIFHWADDR, &request);
  close(fd);
  if (ok) memcpy(out, request.ifr_hwaddr.sa_data, 6);
  return ok;
}

static void testRoute(bool v6)
{
  ZuTestScopeRT(testRoute);
  unsigned index = strtoul(getenv("ZXDP_RX_TEST_PEER_IFINDEX"), nullptr, 10);
  unsigned other = strtoul(getenv("ZXDP_RX_TEST_IFINDEX"), nullptr, 10);
  ZiIP source{v6 ? "2001:db8::2" : "198.18.0.2"};
  ZiIP destination{v6 ? "2001:db8::1" : "198.18.0.1"};
  ZiIP wildcard{v6 ? "::" : "0.0.0.0"};
  Zutcp::IPPath path{wildcard, destination, {}, {}, 0};
  uint8_t localMAC[6], remoteMAC[6];
  bool addresses = mac(index, localMAC) && mac(other, remoteMAC);
  ZtString<> error;
  bool found = Zutcp::resolveIPPath(index, path, error);
  ZuCheckRT(addresses && found && path.local == source &&
      path.remote == destination && path.mtu == 1500 &&
      !memcmp(path.localMAC, localMAC, 6) && !memcmp(path.remoteMAC, remoteMAC, 6),
    log_("kernel selects source/interface and static next-hop MAC, IPv", v6 ? 6 : 4,
      ": ", error));
  if (!found) return;
  auto explicitPath = path;
  explicitPath.local = ZiIP{v6 ? "2001:db8::99" : "198.18.0.99"};
  auto originalLocal = explicitPath.local;
  ZuCheckRT(Zutcp::resolveIPPath(index, explicitPath, error) &&
      explicitPath.local == originalLocal,
    log_("explicit userspace source is preserved without a kernel socket bind: ", error));
  auto gateway = path;
  gateway.remote = ZiIP{v6 ? "2001:db8::5" : "198.18.0.5"};
  ZuCheckRT(Zutcp::resolveIPPath(index, gateway, error) && gateway.mtu == 1400 &&
      !memcmp(gateway.remoteMAC, remoteMAC, 6),
    log_("gateway route uses next-hop neighbor and its route MTU: ", error));
  auto missing = path;
  missing.remote = ZiIP{v6 ? "2001:db8::3" : "198.18.0.3"};
  auto before = missing;
  ZuCheckRT(!Zutcp::resolveIPPath(index, missing, error) && !!error &&
      missing.local == before.local && missing.remote == before.remote &&
      missing.mtu == before.mtu && !memcmp(missing.remoteMAC, before.remoteMAC, 6),
    log_("missing static neighbor fails without publishing partial path state"));
  auto dynamic = path;
  dynamic.remote = ZiIP{v6 ? "2001:db8::4" : "198.18.0.4"};
  ZuCheckRT(!Zutcp::resolveIPPath(index, dynamic, error) && !!error,
    log_("dynamic neighbor does not satisfy configured static-neighbor policy"));
  auto wrong = path;
  ZuCheckRT(!Zutcp::resolveIPPath(other, wrong, error) && !!error,
    log_("lookup cannot select an interface outside the configured scope"));
  auto noRoute = path;
  noRoute.remote = ZiIP{v6 ? "2001:db8:ffff::1" : "203.0.113.1"};
  ZuCheckRT(!Zutcp::resolveIPPath(index, noRoute, error) && !!error,
    log_("absent kernel route fails setup explicitly"));
}

static uint8_t pattern(unsigned i, unsigned n) { return (i + n) ^ (i>>3); }

// Independent peer codec. Linux supplies IP framing; the fixture constructs and
// checks TCP bytes without calling the Zutcp TCP writer/reader.
static unsigned peerSum(const uint8_t *data, unsigned n, unsigned sum = 0)
{
  for (unsigned i = 0; i < n; ++i)
    sum += unsigned(data[i])<<((i & 1) ? 0 : 8);
  return sum;
}

static unsigned peerChecksum(
  const Zutcp::IPPath &path, const uint8_t *data, unsigned n)
{
  unsigned sum = IPPROTO_TCP + n;
  if (path.local.v6()) {
    sum = peerSum(reinterpret_cast<const uint8_t *>(&path.local.in6Addr()), 16, sum);
    sum = peerSum(reinterpret_cast<const uint8_t *>(&path.remote.in6Addr()), 16, sum);
  } else {
    sum = peerSum(reinterpret_cast<const uint8_t *>(&path.local.inAddr()), 4, sum);
    sum = peerSum(reinterpret_cast<const uint8_t *>(&path.remote.inAddr()), 4, sum);
  }
  sum = peerSum(data, n, sum);
  while (sum>>16) sum = (sum & 65535) + (sum>>16);
  return sum ^ 65535;
}

static void peerTCP(uint8_t *bytes, const Zutcp::IPPath &path, unsigned n)
{
  // Peer source port 4444 -> service port 5555; sequence/ACK are test values.
  const uint8_t header[20] = {
    0x11,0x5c,0x15,0xb3, 0,0,0,1, 0,0,0,2, 0x50,0x18,0x10,0, 0,0,0,0};
  memcpy(bytes, header, 20);
  for (unsigned i = 0; i < n; ++i) bytes[20 + i] = pattern(i, n);
  unsigned check = peerChecksum(path, bytes, n + 20);
  bytes[16] = check>>8; bytes[17] = check;
}

struct EndpointInput {
  ZiMultiplex *mx;
  ZmRef<Zi::IOBuf> *input;
  Zutcp::IPView *seen;
  ZuBSpan *payload;
  bool *onRx;
  ZmSemaphore *done;
  unsigned packets = 0;

  static void receive(void *owner, ZmRef<Zi::IOBuf> buf,
    ZiSockAddr local, ZiSockAddr peer, unsigned ecn, const Zutcp::TCPHeader &) {
    auto self = static_cast<EndpointInput *>(owner);
    Zutcp::IPView ip;
    Zutcp::TCPView tcp;
    if (!Zutcp::readIP({buf->data_(), buf->size}, ip) || !Zutcp::readTCP(ip, tcp) ||
        tcp.header.flags != 0x18 || local.port() != 5555 || local.ip() != ip.local ||
        peer.port() != 4444 ||
        peer.ip() != ip.remote || ecn != ip.ecn) return;
    ++self->packets;
    *self->seen = ip;
    *self->payload = buf->cspan();
    *self->onRx = self->mx->invoked(self->mx->rxThread()) && buf->registered();
    *self->input = ZuMv(buf);
    self->done->post();
  }
};

static void testCarriage(bool v6, bool tcp = false)
{
  ZuTestScopeRT(testCarriage);
  alarm(30);
  unsigned index = strtoul(getenv("ZXDP_RX_TEST_IFINDEX"), nullptr, 10);
  unsigned peer = strtoul(getenv("ZXDP_RX_TEST_PEER_IFINDEX"), nullptr, 10);
  Zutcp::IPPath path{ZiIP{v6 ? "2001:db8::1" : "198.18.0.1"},
    ZiIP{v6 ? "2001:db8::2" : "198.18.0.2"}, {}, {}, 1500};
  bool geometry = mac(index, path.localMAC) && mac(peer, path.remoteMAC);
  // Generic IP coverage uses fixture-only protocol 253. TCP coverage uses the
  // selected production framing and an independent peer segment codec.
  unsigned protocol = tcp ? IPPROTO_TCP : 253;
  ZiSockAddr local{path.local, 0}, remote{path.remote, 0};
  int fd = socket(v6 ? AF_INET6 : AF_INET, SOCK_RAW | SOCK_CLOEXEC, protocol);
  int tos = 3;
  bool bound = fd >= 0 && geometry &&
    !bind(fd, remote.sa(), remote.len()) &&
    !setsockopt(fd, v6 ? IPPROTO_IPV6 : IPPROTO_IP, v6 ? IPV6_TCLASS : IP_TOS,
      &tos, sizeof(tos));
  bound = bound && !connect(fd, local.sa(), local.len());
  ZuCheckRT(bound, log_("independent raw-IP fixture peer configured, IPv", v6 ? 6 : 4));
  if (!bound) { if (fd >= 0) close(fd); alarm(0); return; }

  ZiMultiplex mx{ZiMxParams{}.rxBufSize(32).rxQuantum(8)
    .scheduler([](auto &p) { p.nThreads(3); })};
  ZmSemaphore rxDone, txDone, controlDone;
  ZmRef<Zi::IOBuf> input;
  Zutcp::IPView seen;
  ZuBSpan seenPayload;
  bool onRx = false;
  Zutcp::StackParams params{.interface = index,
    .packet = [
      &mx, &input, &seen, &seenPayload, &onRx, &rxDone, &path, protocol, tcp
    ](ZmRef<Zi::IOBuf> buf) {
      Zutcp::IPView view;
      if (!Zutcp::readIP(buf->cspan(), view) || view.local != path.local ||
          view.protocol != protocol) return;
      Zutcp::TCPView segment;
      if (tcp && (!Zutcp::readTCP(view, segment) ||
          segment.header.srcPort != 4444 || segment.header.dstPort != 5555 ||
          segment.header.flags != 0x18)) return;
      seenPayload = tcp ? segment.payload : view.payload;
      seen = view;
      onRx = mx.invoked(mx.rxThread()) && buf->registered();
      input = ZuMv(buf);
      rxDone.post();
    }, .spin = 0};
  if (tcp) params.packet = {};
  ZmRef<Zutcp::Stack> stack = new Zutcp::Stack{&mx, ZuMv(params)};
  mx.plugin(stack);
  auto tx = mx.allocator<Zi::IOBufType::TxTCP, Zi::IOBuf, 2048>(4);
  // AF_XDP queue release can remain pending in the kernel after close. The
  // caller owns lifecycle retries; retain the same providers and bound the wait.
  bool started = false;
  unsigned attempts = 0;
  auto startDeadline = Zm::now() + ZuTime{2};
  do {
    ++attempts;
    if ((started = mx.start())) break;
    usleep(1000); // fixture control caller only
  } while (Zm::now() < startDeadline);
  std::cout << "# raw Stack startup attempts=" << attempts << '\n';
  ZuCheckRT(started, log_("raw packet Stack starts without a kernel Zutcp socket"));
  if (!started) { close(fd); alarm(0); return; }
  EndpointInput endpoint{&mx, &input, &seen, &seenPayload, &onRx, &rxDone};
  EndpointInput wildcard{&mx, &input, &seen, &seenPayload, &onRx, &rxDone};
  ZiIP any;
  if (v6) any = in6addr_any; else any = in_addr{};
  ZiSockAddr boundLocal{path.local, 5555}, boundWildcard{any, 5555};
  if (tcp) {
    bool boundEndpoint = false;
    mx.rxRun([
      &stack, &boundLocal, &boundWildcard, &endpoint, &wildcard, &boundEndpoint, &controlDone
    ] {
      auto duplicate = boundLocal;
      ZiSockAddr ephemeral{boundLocal.ip(), 0}, next{boundLocal.ip(), 0};
      boundEndpoint = stack->bind(boundLocal, &endpoint, &EndpointInput::receive) &&
        !stack->bind(duplicate, &wildcard, &EndpointInput::receive) &&
        !stack->unbind(boundLocal, &wildcard) &&
        stack->bind(boundWildcard, &wildcard, &EndpointInput::receive) &&
        stack->bind(ephemeral, &endpoint, &EndpointInput::receive) &&
        stack->bind(next, &endpoint, &EndpointInput::receive) &&
        ephemeral.port() >= 49152 && next.port() != ephemeral.port() &&
        stack->unbind(ephemeral, &endpoint) && stack->unbind(next, &endpoint);
      controlDone.post();
    });
    controlDone.wait();
    ZuCheckRT(boundEndpoint, log_("native local address/port membership installed"));
    bool routeDone = false;
    auto coldPath = path;
    coldPath.remote = ZiIP{v6 ? "2001:db8::6" : "198.18.0.6"};
    mx.rxRun([&stack, coldPath, &routeDone, &controlDone, &mx] {
      bool posted = stack->resolvePath(coldPath,
        [&routeDone, &controlDone, &mx](Zutcp::IPPath resolved, ZtString<> error) {
          routeDone = !error && resolved.mtu == 1500 &&
            mx.invoked(mx.rxThread());
          controlDone.post();
        });
      if (!posted) controlDone.post();
    });
    controlDone.wait();
    ZuCheckRT(routeDone, log_("cold route result returns to Rx from a non-I/O worker"));
    bool routeFailed = false;
    coldPath.remote = ZiIP{v6 ? "2001:db8:ffff::1" : "203.0.113.1"};
    mx.rxRun([&stack, coldPath, &routeFailed, &controlDone, &mx] {
      bool posted = stack->resolvePath(coldPath,
        [&routeFailed, &controlDone, &mx](Zutcp::IPPath, ZtString<> error) {
          routeFailed = !!error && mx.invoked(mx.rxThread());
          controlDone.post();
        });
      if (!posted) controlDone.post();
    });
    controlDone.wait();
    ZuCheckRT(routeFailed, log_("cold lookup failure returns once on Rx"));
  }
  // Include a full IP MTU in each family. This low-level fixture constructs the
  // entire wire view in the advertised backing; it does not use ZiTxStream.
  unsigned head = tcp ? Zutcp::tcpHead(path.local.type()) : Zutcp::ipHead(path.local.type());
  for (unsigned n : {0u, 1u, 5u, 127u, 1500u - head + 14}) {
    uint8_t bytes[1600];
    unsigned peerLength = n + (tcp ? 20 : 0);
    if (tcp) peerTCP(bytes, path, n);
    else for (unsigned i = 0; i < n; ++i) bytes[i] = pattern(i, n);
    bool sent = send(fd, bytes, peerLength, 0) == int(peerLength);
    bool arrived = sent && !rxDone.timedwait(Zm::now() + ZuTime{2});
    bool intact = arrived && onRx && input && seen.remote == path.remote &&
      seenPayload.length() == n && seen.ecn == 3 &&
      seenPayload.data() >= input->data() &&
      seenPayload.data() + n <= input->end() &&
      !memcmp(seenPayload.data(), bytes + (tcp ? 20 : 0), n);
    ZuCheckRT(intact, log_("kernel-to-UMEM original payload/integrity, length ", n));
    if (!arrived) break;
    seen = {};
    input = nullptr;
    bool accepted = false;
    mx.txRun([&mx, &stack, tx, &path, head, n, protocol, tcp, &accepted, &txDone] {
      ZmAssert_(mx.invoked(mx.txThread()));
      auto packet = tx->allocBuf(&mx, 0);
      if (packet && packet->size >= head + n) {
	auto frame = packet->data_();
	for (unsigned i = 0; i < n; ++i) frame[head + i] = pattern(i, n);
	Zutcp::TCPHeader header{5555, 4444, 2, 1, 4096, 0, 0x18};
	auto wire = tcp ? Zutcp::writeTCP(frame, packet->size, path, header, n, 3) :
	  Zutcp::writeIP(frame, packet->size, path, protocol, n, 3);
	packet->skip = 0; packet->length = wire;
	accepted = wire && stack->sendPacket(packet);
      }
      txDone.post();
    });
    txDone.wait();
    pollfd event{fd, POLLIN, 0};
    int received = accepted && poll(&event, 1, 2000) > 0 ? recv(fd, bytes, sizeof(bytes), 0) : -1;
    // Linux IPv4 raw input includes its IP header; IPv6 raw input is payload.
    unsigned offset = !v6 && received >= 20 ? unsigned(bytes[0] & 15)<<2 : 0;
    bool valid = received >= 0 && offset <= unsigned(received) &&
      unsigned(received) - offset == peerLength &&
      (v6 || (offset >= 20 && bytes[9] == protocol));
    if (valid && tcp) {
      auto segment = bytes + offset;
      valid = segment[0] == 0x15 && segment[1] == 0xb3 &&
        segment[2] == 0x11 && segment[3] == 0x5c && segment[12] == 0x50 &&
        segment[13] == 0x18 && !peerChecksum(path, segment, peerLength);
      offset += 20;
    }
    for (unsigned i = 0; valid && i < n; ++i) valid &= bytes[offset + i] == pattern(i, n);
    ZuCheckRT(valid, log_("Zxdp emits ", tcp ? "TCP/IP" : "direct IP",
      " with exact payload, length ", n));
  }
  input = nullptr;
  if (tcp) {
    bool removed = false;
    mx.rxRun([&stack, &boundLocal, &endpoint, &removed, &controlDone] {
      removed = stack->unbind(boundLocal, &endpoint);
      controlDone.post();
    });
    controlDone.wait();
    ZuCheckRT(removed && endpoint.packets == 5 && !wildcard.packets,
      log_("exact local membership wins over wildcard and only its owner can remove it"));
    uint8_t bytes[25];
    peerTCP(bytes, path, 5);
    bool sent = send(fd, bytes, sizeof(bytes), 0) == sizeof(bytes);
    bool arrived = sent && !rxDone.timedwait(Zm::now() + ZuTime{2});
    ZuCheckRT(arrived && wildcard.packets == 1 && seenPayload.length() == 5 &&
        !memcmp(seenPayload.data(), bytes + 20, 5),
      log_("same-family wildcard handles unmatched local IP"));
    input = nullptr;
    mx.rxRun([&stack, &boundWildcard, &wildcard, &removed, &controlDone] {
      removed = stack->unbind(boundWildcard, &wildcard);
      controlDone.post();
    });
    controlDone.wait();
    ZuCheckRT(removed, log_("wildcard membership removed before owner retirement"));
  }
  if (tcp) {
    // Hold cold work on its own worker, then stop through the real mx control
    // engine. Rx/Tx must keep running while the route completion is drained.
    ZmSemaphore workerHeld, releaseWorker, stopDone;
    unsigned worker = 0;
    for (unsigned i = 0; i < mx.nWorkers(); ++i) {
      auto id = mx.workerID(i);
      if (id != mx.rxThread() && id != mx.txThread()) { worker = id; break; }
    }
    bool posted = false, cancelled = false, stopped = false;
    mx.run([&workerHeld, &releaseWorker] {
      workerHeld.post(); releaseWorker.wait();
    }, worker);
    workerHeld.wait();
    auto coldPath = path;
    coldPath.remote = ZiIP{v6 ? "2001:db8::6" : "198.18.0.6"};
    mx.rxRun([&stack, coldPath, &posted, &cancelled, &controlDone, &rxDone, &mx] {
      posted = stack->resolvePath(coldPath,
        [&cancelled, &controlDone, &mx](Zutcp::IPPath, ZtString<> error) {
          cancelled = !!error && mx.invoked(mx.rxThread());
          controlDone.post();
        });
      rxDone.post();
    });
    rxDone.wait();
    mx.ZmEngine<ZmScheduler>::stop([&stopped, &stopDone](bool ok) {
      stopped = ok; stopDone.post();
    });
    auto deadline = Zm::now() + ZuTime{2};
    while (stack->ready() && Zm::now() < deadline) usleep(1000);
    bool gated = !stack->ready();
    // The blocked worker and its pending route must still prevent completion.
    bool premature = !stopDone.trywait();
    releaseWorker.post();
    if (posted) controlDone.wait();
    if (!premature) stopDone.wait();
    ZuCheckRT(posted && gated && !premature && cancelled && stopped,
      log_("shutdown drains a pending cold lookup before plugin completion"));
  } else {
    ZuCheckRT(mx.stop(), log_("raw service retires all submitted descriptors"));
  }
  close(fd);
  alarm(0);
}

static void testRouteV4() { testRoute(false); }
static void testRouteV6() { testRoute(true); }
static void testV4() { testCarriage(false); }
static void testV6() { testCarriage(true); }
static void testTCPV4() { testCarriage(false, true); }
static void testTCPV6() { testCarriage(true, true); }

int main(int argc, char **argv)
{
  parse(argc, argv);
  if (geteuid() || !getenv("ZUTCP_IP_FIXTURE")) {
    std::cout << "1..0 # SKIP requires privileged zutcpipfixture.sh setup\n";
    return 0;
  }
  ZiLog::init("zutcpiptest");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  ZuTestMain();
  ZuTestCall(testRouteV4);
  ZuTestCall(testRouteV6);
  ZuTestCall(testV4);
  ZuTestCall(testV6);
  ZuTestCall(testTCPV4);
  ZuTestCall(testTCPV6);
  ZiLog::stop();
}
