//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <errno.h>
#include <limits.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <netinet/in.h>
#include <poll.h>
#include <string.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmThread.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>

#include <zlib/ZpktEnv.hh>
#include "ZpktTestCf.hh"

using namespace ZuTestUtil;

enum { Full = INT_MAX, MTU = 1500, FrameSize = MTU + ETH_HLEN, Control = 60 };

struct WireFrame {
  unsigned	length = Control;
  unsigned	flags = 0;
  unsigned	packetType = PACKET_HOST;
  unsigned	auxLength = CMSG_LEN(sizeof(tpacket_auxdata));
  tpacket_auxdata aux{};
  uint8_t	bytes[FrameSize]{};
};

struct Probe {
  ZmAtomic<int>	socket{-1}, wake{-1}, txResult{Full}, rxError{0};
  ZmAtomic<int>	bypassError{0}, bindError{0};
  ZmAtomic<unsigned> gateWrite{0}, gatePoll{0}, gateTx{0};
  ZmAtomic<unsigned> opens{0}, runEventFDs{0}, dormantBinds{0}, activeBinds{0};
  ZmAtomic<unsigned> bypassRequests{0}, activeBeforeBypass{0}, statsReads{0}, drops{0};
  ZmAtomic<unsigned> sendCalls{0}, recvCalls{0}, pollCalls{0};
  ZmAtomic<unsigned> unsound{0};
  unsigned	rxCount = 0, rxCursor = 0;
  bool		expectBypass = true;
  WireFrame	frames[12];
  ZmSemaphore	writerEntered, writerRelease, pollEntered, pollRelease, txEntered, txRelease;

  void framesWith(unsigned count, unsigned first = 1) {
    rxCursor = 0;
    rxCount = count;
    for (unsigned i = 0; i < count; ++i) {
      frames[i] = WireFrame{};
      frames[i].bytes[12] = 0x88;
      frames[i].bytes[13] = 0xb5;
      frames[i].bytes[14] = first + i;
    }
  }
};

static ZmAtomic<Probe *> active{nullptr};

struct StatusProbe {
  const char *first, *second;
  ZmSemaphore seen;
  bool matches = false;
};
static ZmAtomic<StatusProbe *> statusProbe{nullptr};

static bool statusContains(Zpkt::Nif *nif, const char *first, const char *second)
{
  StatusProbe sampled{first, second};
  statusProbe = &sampled;
  nif->status();
  sampled.seen.wait();
  statusProbe = nullptr;
  return sampled.matches;
}

extern "C" int __real_socket(int, int, int);
extern "C" int __real_bind(int, const sockaddr *, socklen_t);
extern "C" int __real_eventfd(unsigned, int);
extern "C" int __real_setsockopt(int, int, int, const void *, socklen_t);
extern "C" int __real_getsockopt(int, int, int, void *, socklen_t *);
extern "C" int __real_poll(pollfd *, nfds_t, int);
extern "C" ssize_t __real_write(int, const void *, size_t);

extern "C" int __wrap_socket(int domain, int type, int protocol)
{
  auto p = active.load_();
  if (!p || domain != AF_PACKET) return __real_socket(domain, type, protocol);
  if (type != (SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC) || protocol) ++p->unsound;
  ++p->opens;
  return (p->socket = __real_eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC));
}

extern "C" int __wrap_eventfd(unsigned value, int flags)
{
  int fd = __real_eventfd(value, flags);
  if (auto p = active.load_()) {
    if (value || flags != (EFD_NONBLOCK | EFD_CLOEXEC)) ++p->unsound;
    ++p->runEventFDs;
    p->wake = fd;
  }
  return fd;
}

extern "C" int __wrap_bind(int fd, const sockaddr *addr, socklen_t length)
{
  auto p = active.load_();
  if (!p || fd != p->socket) return __real_bind(fd, addr, length);
  auto packet = reinterpret_cast<const sockaddr_ll *>(addr);
  if (length != sizeof(*packet) || packet->sll_family != AF_PACKET || packet->sll_ifindex != 17)
    ++p->unsound;
  if (packet->sll_protocol == htons(ETH_P_ALL)) {
    if (p->expectBypass && p->bypassRequests != p->runEventFDs) ++p->activeBeforeBypass;
    ++p->activeBinds;
    if (int err = p->bindError.xch(0)) { errno = err; return -1; }
  } else {
    if (packet->sll_protocol) ++p->unsound;
    ++p->dormantBinds;
  }
  return 0;
}

extern "C" int __wrap_setsockopt(int fd, int level, int option, const void *data, socklen_t length)
{
  auto p = active.load_();
  if (!p || fd != p->socket) return __real_setsockopt(fd, level, option, data, length);
  if (level != SOL_PACKET || length != sizeof(int) || *static_cast<const int *>(data) != 1)
    ++p->unsound;
  if (option == PACKET_QDISC_BYPASS) {
    ++p->bypassRequests;
    if (int err = p->bypassError.xch(0)) { errno = err; return -1; }
  }
  return 0;
}

extern "C" int __wrap_getsockopt(int fd, int level, int option, void *data, socklen_t *length)
{
  auto p = active.load_();
  if (!p || fd != p->socket) return __real_getsockopt(fd, level, option, data, length);
  if (level != SOL_PACKET || option != PACKET_STATISTICS ||
      *length < sizeof(tpacket_stats)) { ++p->unsound; errno = EINVAL; return -1; }
  ++p->statsReads;
  *static_cast<tpacket_stats *>(data) = {0, p->drops.xch(0)};
  *length = sizeof(tpacket_stats);
  return 0;
}

extern "C" int __wrap_recvmmsg(int fd, mmsghdr *msg, unsigned count, int flags, timespec *timeout)
{
  auto p = active.load_();
  ZmAssert_(p && fd == p->socket);
  if (flags != (MSG_DONTWAIT | MSG_TRUNC) || timeout || count > 4) ++p->unsound;
  ++p->recvCalls;
  if (int err = p->rxError.xch(0)) { errno = err; return -1; }
  unsigned n = p->rxCount - p->rxCursor;
  if (n > count) n = count;
  for (unsigned i = 0; i < n; ++i) {
    auto &frame = p->frames[p->rxCursor++];
    auto &header = msg[i].msg_hdr;
    if (header.msg_namelen != sizeof(sockaddr_ll) || header.msg_flags ||
	header.msg_iovlen != 1 || header.msg_controllen != CMSG_SPACE(sizeof(tpacket_auxdata)))
      ++p->unsound;
    auto addr = static_cast<sockaddr_ll *>(header.msg_name);
    addr->sll_pkttype = frame.packetType;
    auto cmsg = CMSG_FIRSTHDR(&header);
    cmsg->cmsg_len = frame.auxLength;
    cmsg->cmsg_level = SOL_PACKET;
    cmsg->cmsg_type = PACKET_AUXDATA;
    auto aux = frame.aux;
    aux.tp_len = aux.tp_snaplen = frame.length;
    memcpy(CMSG_DATA(cmsg), &aux, sizeof(aux));
    msg[i].msg_len = frame.length;
    unsigned copy = frame.length;
    if (copy > header.msg_iov->iov_len) copy = header.msg_iov->iov_len;
    memcpy(header.msg_iov->iov_base, frame.bytes, copy);
    header.msg_flags = frame.flags;
  }
  if (!n) { errno = EAGAIN; return -1; }
  return n;
}

extern "C" int __wrap_sendmmsg(int fd, mmsghdr *msg, unsigned count, int flags)
{
  auto p = active.load_();
  ZmAssert_(p && fd == p->socket);
  if (flags != MSG_DONTWAIT || !count || count > Zpkt::MaxTxBatch) ++p->unsound;
  ++p->sendCalls;
  if (p->gateTx.xch(0)) { p->txEntered.post(); p->txRelease.wait(); }
  int outcome = p->txResult.xch(Full);
  if (outcome < 0) { errno = -outcome; return -1; }
  unsigned n = outcome == Full ? count : unsigned(outcome);
  if (n > count) { ++p->unsound; n = count; }
  for (unsigned i = 0; i < n; ++i) msg[i].msg_len = msg[i].msg_hdr.msg_iov->iov_len;
  return n;
}

extern "C" int __wrap_poll(pollfd *fds, nfds_t count, int timeout)
{
  if (auto p = active.load_(); p && count && fds[0].fd == p->wake) {
    if (count > 2 || timeout != -1) ++p->unsound;
    ++p->pollCalls;
    if (p->gatePoll.xch(0)) { p->pollEntered.post(); p->pollRelease.wait(); }
  }
  return __real_poll(fds, count, timeout);
}

extern "C" ssize_t __wrap_write(int fd, const void *data, size_t size)
{
  if (auto p = active.load_(); p && fd == p->wake && p->gateWrite.xch(0)) {
    p->writerEntered.post();
    p->writerRelease.wait();
  }
  return __real_write(fd, data, size);
}

extern "C" Zpkt::Plugin *ZpktPlugin(const ZfCf::AnyNode *);

static auto config(const char *module)
{
  auto cf = ZpktTest::object();
  ZpktTest::value(cf.ptr(), "module", module);
  auto interfaces = ZpktTest::object(cf.ptr(), "interfaces");
  ZpktTest::value(ZpktTest::object(interfaces, "plain"), "rxSpin", "0");
  auto shaped = ZpktTest::object(interfaces, "shaped");
  ZpktTest::value(shaped, "rxSpin", "0");
  ZpktTest::value(shaped, "qdiscBypass", "false");
  return cf;
}

static unsigned queries;

static ZuUnion<Zpkt::NifInfo, ZeException> query(ZuCSpan name)
{
  ++queries;
  if (name == "_loop") return ZeEXCEPT(Error, "Zpkt.Test", "sentinel reached host lookup");
  return Zpkt::NifInfo{17, MTU, 3, {2, 0, 0, 0, 0, 17}};
}

struct Endpoint {
  Probe			probe;
  ZiMultiplex		mx{ZiMxParams{}.rxQuantum(4)};
  ZmRef<Zpkt::Plugin>	plugin;
  ZmRef<Zpkt::Nif>	nif;
  ZmRef<Zi::IOBufPool>	ingress, tx;
  Zi::IOBufPoolList	pools;
  ZmSemaphore		rxDone;

  ~Endpoint() {
    if (nif && nif->state() != ZmEngineState::Stopped) nif->stop();
    if (nif) nif->final();
    if (mx.running()) mx.stop();
    active = nullptr;
  }

  bool init(bool bypass = true, unsigned rxCount = 4, unsigned rxSize = FrameSize) {
    probe.expectBypass = bypass;
    active = &probe;
    auto cf = config(ZPKT_AFP_MODULE);
    plugin = ZpktPlugin(cf.ptr());
    cf = nullptr;
    auto result = plugin->nif(&mx, bypass ? "plain" : "shaped",
      Zpkt::NifInfo{17, MTU, 3, {2, 0, 0, 0, 0, 17}});
    if (result.is<ZeException>()) { log(result.p<ZeException>()); return false; }
    nif = result.p<Zpkt::Nif *>();
    ingress = nif->bufPool(Zi::IOBufType::Rx, rxCount, rxSize, 31);
    tx = nif->bufPool(Zi::IOBufType::TxUDP, 5, Control, Control);
    pools.pushNode(tx.ptr());
    auto allocation = nif->alloc(ingress.ptr(), pools);
    if (allocation.is<ZeException>()) { log(allocation.p<ZeException>()); return false; }
    return mx.start();
  }

  Zpkt::RecvFn discard() {
    return [this](uint8_t *p, unsigned, ZiRxBurst *) { ingress->free(p); };
  }

  void rx() { mx.rxRun([this] { nif->rx(); rxDone.post(); }); }
  template <typename L> bool onTx(L &&l) {
    return ZmBlock<bool>{}([this, &l](auto done) {
      mx.txRun([&l, done]() mutable { done(l()); });
    });
  }

  uint8_t *packet() {
    auto p = tx->alloc();
    ZmAssert_(p);
    memset(p, 0x66, Control);
    p[12] = 0x88;
    p[13] = 0xb5;
    return p;
  }
};

static void loader()
{
  ZuTestScope(loader);
  auto cf = config(ZPKT_AFP_MODULE);
  ZmRef<Zpkt::Env> env = new Zpkt::Env;
  env->nifQueryFn(query);
  env->init(cf.ptr());
  cf = nullptr;
  ZiMultiplex mx{ZiMxParams{}};
  ZuCHECK(!env->loopback() && !queries, "loaded AFP reports no backend loopback before opening");
  ZuCHECK(env->nif(&mx, "_loop").is<ZeException>() && !queries,
    "unsupported sentinel never asks the host");
  ZuCHECK(env->nif(&mx, "not-configured").is<ZeException>() && queries == 1,
    "a normal interface still resolves host geometry");
}

static void activation()
{
  ZuTestScope(activation);
  {
    Endpoint e;
    bool opened = e.init();
    ZuCHECK(opened, "open dormant interface with default policy");
    if (!opened) return;
    auto &s = e.probe;
    ZuCHECK(s.opens == 1 && s.dormantBinds == 1 && !s.activeBinds && !s.bypassRequests,
      "named creation leaves ingress dormant");
    s.bypassError = ENOPROTOOPT;
    ZuCHECK(e.nif->start(e.discard()) && s.bypassRequests == 1 && !s.activeBeforeBypass &&
	statusContains(e.nif.ptr(), "qdiscBypass requested on effective off",
	  "PACKET_QDISC_BYPASS unsupported"),
      "unsupported bypass keeps activation working on the normal qdisc path");
    ZuCHECK(e.nif->stop(), "stop the fallback run");
    s.bypassError = EPERM;
    ZuCHECK(!e.nif->start(e.discard()) && !e.nif->ready() && e.nif->allocated() &&
	s.activeBinds == 1 && !!e.nif->error(),
      "other bypass errors roll back before active bind and retain the pools");
    s.bindError = EADDRINUSE;
    ZuCHECK(!e.nif->start(e.discard()) && !e.nif->ready() && s.activeBinds == 2,
      "active bind failure leaves instance reusable");
    ZuCHECK(e.nif->start(e.discard()) && e.nif->stop() && s.opens == 4 &&
	s.runEventFDs == 4 && s.bypassRequests == 4 && !s.activeBeforeBypass && !s.unsound,
      "fresh sockets reapply bypass before each successful active bind");
  }
  {
    Endpoint e;
    bool opened = e.init(false);
    ZuCHECK(opened && e.nif->start(e.discard()) && !e.probe.bypassRequests && e.nif->stop(),
      "explicit opt-out leaves the fresh socket's qdisc defaults in force");
  }
}

static void transmit()
{
  ZuTestScope(transmit);
  Endpoint e;
  bool started = e.init() && e.nif->start(e.discard());
  ZuCHECK(started, "start with exact-fit control pool");
  if (!started) return;
  auto a = e.packet(), b = e.packet(), c = e.packet();
  auto &p = e.probe;
  ZuCHECK(e.onTx([&e, a] {
      return e.nif->txReady(a) == Zi::OK && e.nif->send(a, Control) == Zi::OK &&
	e.nif->txReady(a) == Zi::NotReady && e.nif->send(a, Control) == Zi::NotReady &&
	e.nif->pollTx() == 1 && e.nif->pollTx() == 0 && e.nif->txReady(a) == Zi::OK;
    }), "scalar submission retains backing until exact retirement");
  ZuCHECK(e.onTx([&e, &p, a] {
      p.txResult = -ENOBUFS;
      auto pressure = e.nif->send(a, Control);
      auto overrun = e.nif->send(a + 1, Control);
      p.txResult = -EMSGSIZE;
      auto oversize = e.nif->send(a, Control);
      p.txResult = 0;
      auto empty = e.nif->send(a, Control);
      return pressure == Zi::NotReady && overrun == Zi::IOError &&
	oversize == Zi::IOError && empty == Zi::NotReady &&
	e.nif->ready() && e.nif->txReady(a) == Zi::OK && e.nif->pollTx() == 0;
    }), "pressure, invalid extent, EMSGSIZE and empty outcomes leave the caller lease");
  ZuCHECK(e.onTx([&e, &p, a, b, c] {
      const Zpkt::TxPacket frames[]{{a, Control}, {b, Control}, {c, Control}};
      p.txResult = 1;
      return e.nif->send(frames) == 1 && e.nif->txReady(a) == Zi::NotReady &&
	e.nif->txReady(b) == Zi::OK && e.nif->txReady(c) == Zi::OK &&
	e.nif->pollTx() == 1 && !e.nif->pollTx();
    }), "positive partial send retires only the accepted frame and rolls back its suffix");
  unsigned calls = p.sendCalls;
  ZuCHECK(e.onTx([&e, &p, b, c] {
      const Zpkt::TxPacket repeated[]{{b, Control}, {b + 1, Control - 1}, {c, Control}};
      p.txResult = -EINTR;
      return e.nif->send(repeated) == 1 && e.nif->txReady(c) == Zi::OK;
    }) && p.sendCalls == calls + 2, "EINTR retries the prefix and a duplicate interior slot ends it");
  e.tx->free(b + 14);
  auto freeSlot = e.packet();
  ZuCHECK(freeSlot != b && e.onTx([&e] { return e.nif->pollTx() == 1; }),
    "caller return before retirement defers reuse");
  auto recycled = e.packet();
  ZuCHECK(recycled == b && e.onTx([&e, recycled] {
      return e.nif->send(recycled, Control) == Zi::OK && e.nif->pollTx() == 1;
    }), "the same canonical slot is reusable after retirement");
  for (auto packet : {a, c, freeSlot, recycled}) e.tx->free(packet);
  ZuCHECK(e.nif->stop() && e.onTx([&e, a] {
      return e.nif->txReady(a) == Zi::IOError && e.nif->send(a, Control) == Zi::IOError;
    }), "a stopped run rejects scalar service before touching caller identity");
}

static void receiveBurst()
{
  ZuTestScope(receiveBurst);
  Endpoint e;
  bool opened = e.init();
  ZuCHECK(opened, "initialize AFP receive fixture");
  if (!opened) return;
  unsigned delivered = 0, order[4]{};
  bool scopes = true;
  auto &p = e.probe;
  p.framesWith(8);
  bool started = e.nif->start([&e, &delivered, &order, &scopes]
    (uint8_t *packet, unsigned, ZiRxBurst *burst) {
      if (delivered < 4) order[delivered] = packet[14];
      scopes &= bool(burst);
      e.ingress->free(packet);
      if (++delivered == 1) e.nif->wake();
    });
  ZuCHECK(started, "start with bounded receive bursts");
  if (!started) return;
  e.nif->wake();
  e.rx();
  e.rxDone.wait();
  ZuCHECK(!delivered && !p.recvCalls, "preposted wake returns before dequeue");
  e.rx();
  e.rxDone.wait();
  ZuCHECK(delivered == 4 && p.recvCalls == 1 && scopes &&
      order[0] == 1 && order[1] == 2 && order[2] == 3 && order[3] == 4,
    "wake in callback one delivers the remainder in order and skips the next dequeue");
  e.nif->stop();

  delivered = 0;
  bool outside = false, after = false, success = false;
  ZmSemaphore stopped;
  p.framesWith(4);
  started = e.nif->start([&e, &delivered, &outside, &after, &success, &stopped]
    (uint8_t *packet, unsigned, ZiRxBurst *) {
      ++delivered;
      e.nif->stop([&outside, &after, &success, &stopped](bool ok) {
	after = outside;
	success = ok;
	stopped.post();
      });
      e.ingress->free(packet);
      outside = true;
    });
  ZuCHECK(started, "restart for asynchronous stop inside callback");
  if (!started) return;
  e.rx();
  e.rxDone.wait();
  stopped.wait();
  auto first = e.ingress->alloc(), second = e.ingress->alloc();
  auto third = e.ingress->alloc(), fourth = e.ingress->alloc();
  ZuCHECK(delivered == 1 && success && after && first && second && third && fourth,
    "stop suppresses the pending callbacks, releases their slots and completes after return");
  for (auto packet : {first, second, third, fourth}) if (packet) e.ingress->free(packet);
}

static void replenish()
{
  ZuTestScope(replenish);
  Endpoint e;
  bool opened = e.init(true, 1);
  ZuCHECK(opened, "open a one-slot ingress pool");
  if (!opened) return;
  uint8_t *held = nullptr;
  unsigned received = 0;
  bool singleton = true;
  ZmSemaphore txStopped;
  auto receive = [&e, &held, &received, &singleton](uint8_t *p, unsigned, ZiRxBurst *burst) {
    ++received;
    singleton &= !burst;
    held = p;
    e.nif->wake();
  };
  auto &p = e.probe;
  p.framesWith(1);
  bool started = e.nif->start(receive, [&txStopped] { txStopped.post(); });
  ZuCHECK(started, "start receive and retain one packet");
  if (!started) return;
  e.rx();
  e.rxDone.wait();
  auto first = held;
  p.framesWith(1, 20);
  p.gatePoll = 1;
  e.rx();
  p.pollEntered.wait();
  if (first) e.ingress->free(first + 14);
  p.pollRelease.post();
  e.rxDone.wait();
  ZuCHECK(held == first && held && held[14] == 20 && received == 2 && singleton,
    "off-owner slot return replenishes an exhausted receiver and delivers a singleton");

  p.gatePoll = 1;
  e.rx();
  p.pollEntered.wait();
  p.gateWrite = 1;
  ZmThread writer{[&e, held] { e.ingress->free(held); }};
  p.writerEntered.wait();
  ZmSemaphore stopDone;
  bool success = false;
  e.nif->stop([&stopDone, &success](bool ok) { success = ok; stopDone.post(); });
  p.pollRelease.post();
  e.rxDone.wait();
  txStopped.wait();
  ZuCHECK(stopDone.trywait() != 0, "teardown waits for an admitted replenishment writer");
  p.writerRelease.post();
  writer.join();
  stopDone.wait();
  ZuCHECK(success && p.runEventFDs == 1, "stop drains notification after the writer returns");

  p.framesWith(1, 30);
  bool restarted = e.nif->start(receive);
  ZuCHECK(restarted, "restart with new eventfd and retained pool backing");
  if (!restarted) return;
  e.rx();
  e.rxDone.wait();
  ZuCHECK(held == first && held[14] == 30 && received == 3 && p.runEventFDs == 2,
    "restart begins without stale wake and uses the same backing");
  e.nif->stop();
  e.ingress->free(held);
  auto returned = e.ingress->alloc();
  ZuCHECK(returned == first, "caller may return held backing while stopped");
  if (returned) e.ingress->free(returned);
}

static void vlanAndTelemetry()
{
  ZuTestScope(vlanAndTelemetry);
  Endpoint e;
  bool opened = e.init(false);
  ZuCHECK(opened, "open AFP VLAN fixture");
  if (!opened) return;
  unsigned count = 0, lengths[4]{};
  uint8_t bytes[4][FrameSize]{};
  auto receive = [&e, &count, &lengths, &bytes](uint8_t *p, unsigned n, ZiRxBurst *) {
    if (count < 4) { lengths[count] = n; memcpy(bytes[count], p, n); }
    e.ingress->free(p);
    if (++count == 1) e.nif->wake();
  };
  auto &p = e.probe;
  p.framesWith(4);
  p.frames[0].aux.tp_status = TP_STATUS_VLAN_VALID;
  auto &nested = p.frames[1];
  nested.bytes[12] = 0x81;
  nested.bytes[13] = 0x00;
  nested.bytes[14] = 0;
  nested.bytes[15] = 5;
  nested.bytes[16] = 0x88;
  nested.bytes[17] = 0xb5;
  nested.aux.tp_status = TP_STATUS_VLAN_VALID | TP_STATUS_VLAN_TPID_VALID;
  nested.aux.tp_vlan_tpid = 0x88a8;
  nested.aux.tp_vlan_tci = 7;
  p.frames[3].auxLength = CMSG_LEN(sizeof(tpacket_auxdata) - 1);
  bool started = e.nif->start(receive);
  ZuCHECK(started, "start with auxdata enabled");
  if (!started) return;
  e.rx();
  e.rxDone.wait();
  const uint8_t defaultTag[]{0x81, 0x00, 0, 0};
  const uint8_t outerTag[]{0x88, 0xa8, 0, 7};
  ZuCHECK(count == 3 && lengths[0] == Control + 4 && lengths[1] == Control + 4 &&
      lengths[2] == Control && !memcmp(bytes[0] + 12, defaultTag, 4) &&
      !memcmp(bytes[0], p.frames[0].bytes, 12) &&
      !memcmp(bytes[0] + 16, p.frames[0].bytes + 12, Control - 12) &&
      !memcmp(bytes[1] + 12, outerTag, 4) &&
      !memcmp(bytes[1], nested.bytes, 12) &&
      !memcmp(bytes[1] + 16, nested.bytes + 12, Control - 12) &&
      !memcmp(bytes[2], p.frames[2].bytes, Control),
    "restore TCI zero and explicit outer TPID, preserve the inner tag, and reject short metadata");

  count = 0;
  p.framesWith(4);
  p.frames[0].length = FrameSize - 3;
  p.frames[0].aux.tp_status = TP_STATUS_VLAN_VALID;
  p.frames[1].flags = MSG_CTRUNC;
  p.frames[2].packetType = PACKET_OUTGOING;
  p.frames[3].length = FrameSize - 4;
  p.frames[3].aux.tp_status = TP_STATUS_VLAN_VALID;
  p.rxError = EINTR;
  e.rx();
  e.rxDone.wait();
  ZuCHECK(count == 1 && lengths[0] == FrameSize && bytes[0][18] == 4,
    "VLAN expansion succeeds with four spare bytes and rejects three, truncation and outgoing");

  Ztc::QueueTelemetry rx, tx;
  auto queues = [&e, &rx, &tx] {
    return e.nif->allQueues([&rx, &tx](Ztc::Queue *q) {
      auto key = q->telKey();
      q->telemetry(key.p<2>() == Ztc::QueueType::Rx ? rx : tx);
    });
  };
  p.drops = 3;
  ZuCHECK(queues() == 2 && rx.full == 3 && rx.outCount == 4 &&
      rx.outBytes == 2 * (Control + 4) + Control + FrameSize &&
      rx.id == "afp.17.rx" && tx.id == "afp.17.tx" && tx.size == 5,
    "active queue snapshots expose exact deliveries, shared drops and completion capacity");
  p.drops = 9;
  unsigned samples = p.statsReads;
  bool sampled = statusContains(e.nif.ptr(), "kernel drops 12", "software discards 4");
  ZuCHECK(p.statsReads == samples + 1 && queues() == 2 && rx.full == 12 && sampled,
    "status and queue observers share kernel drops, with software discards reported separately");
  ZuCHECK(e.nif->stop() && queues() == 0 && e.nif->start(e.discard()) &&
      queues() == 2 && !rx.full && !rx.outCount && !p.unsound,
    "stopped queues disappear and restart resets statistics");
}

static void fatalStop()
{
  ZuTestScope(fatalStop);
  Endpoint e;
  bool opened = e.init();
  ZuCHECK(opened, "open stop ordering fixture");
  if (!opened) return;
  auto a = e.packet(), b = e.packet();
  bool txOwner = false, retired = false, completedAfter = false;
  ZmAtomic<unsigned> callbacks{0};
  auto retiredFn = [&e, &txOwner, &retired, &callbacks] {
    txOwner = e.mx.invoked(e.mx.txThread());
    retired = !e.nif->pollTx();
    ++callbacks;
  };
  bool started = e.nif->start(e.discard(), retiredFn);
  ZuCHECK(started, "activate with receive and txStopped callbacks");
  if (!started) { e.tx->free(a); e.tx->free(b); return; }
  bool sent = e.onTx([&e, a] { return e.nif->send(a, Control) == Zi::OK; });
  bool normal = e.nif->stop();
  ZuCHECK(sent && normal && callbacks == 1 && txOwner && retired && a[14] == 0x66,
    "normal stop drains the submission before one Tx-owner callback and preserves bytes");

  bool restarted = e.nif->start(e.discard(), retiredFn);
  ZuCHECK(restarted && e.onTx([&e, a] { return e.nif->send(a, Control) == Zi::OK; }),
    "retained backing can be submitted after restart");
  if (!restarted) { e.tx->free(a); e.tx->free(b); return; }
  auto &p = e.probe;
  p.txResult = -EPIPE;
  p.gateTx = 1;
  ZmSemaphore sendDone, stopDone;
  bool failedSend = false, stoppedOK = true;
  e.mx.txRun([&e, b, &failedSend, &sendDone] {
    failedSend = e.nif->send(b, Control) == Zi::IOError;
    sendDone.post();
  });
  p.txEntered.wait();
  e.nif->stop([&callbacks, &completedAfter, &stoppedOK, &stopDone](bool ok) {
    completedAfter = callbacks == 2;
    stoppedOK = ok;
    stopDone.post();
  });
  p.txRelease.post();
  sendDone.wait();
  stopDone.wait();
  auto error = e.nif->error();
  ZeLogBuf msg;
  if (error) error.fn()(msg, error);
  ZuCHECK(failedSend && !stoppedOK && completedAfter && txOwner && retired &&
      callbacks == 2 && memmem(msg.data(), msg.length(), "sendmmsg", 8) && a[14] == 0x66 && b[14] == 0x66,
    "fatal send racing stop records the first error and uses the same ordered teardown");
  e.tx->free(a);
  e.tx->free(b);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZiLog::init("ZpktAfpInstanceTest");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::lambdaSink([](ZeLogBuf &buf, const ZeEventInfo &info) {
    if (info.component != "Zpkt.Afp") return;
    auto sample = static_cast<StatusProbe *>(statusProbe);
    if (!sample) return;
    sample->matches =
      memmem(buf.data(), buf.length(), sample->first, strlen(sample->first)) &&
      memmem(buf.data(), buf.length(), sample->second, strlen(sample->second));
    sample->seen.post();
  }));
  ZiLog::start();
  ZuTestMain();
  ZuTestCall(loader);
  ZuTestCall(activation);
  ZuTestCall(transmit);
  ZuTestCall(receiveBurst);
  ZuTestCall(replenish);
  ZuTestCall(vlanAndTelemetry);
  ZuTestCall(fatalStop);
  ZiLog::stop();
}
