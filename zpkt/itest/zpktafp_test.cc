//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <arpa/inet.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmBlock.hh>

#include <zlib/ZiMultiplex.hh>

#include <zlib/ZpktEnv.hh>
#include "ZpktTestCf.hh"

using namespace ZuTestUtil;

enum { FullFrame = 1514, ShortFrame = 60 };

struct Frame {
  unsigned length = 0;
  uint8_t bytes[FullFrame]{};
};

struct Endpoint {
  ZiMultiplex mx;
  ZmRef<Zpkt::Env> env = new Zpkt::Env;
  ZmRef<Zpkt::Nif> nif;
  ZmRef<Zi::IOBufPool> ingress, tx, control;
  Zi::IOBufPoolList txPools;
  Frame input[4];
  unsigned received = 0;

  ~Endpoint() {
    if (nif && nif->ready()) nif->stop();
    if (nif) nif->final();
    if (mx.running()) mx.stop();
  }

  bool init(const char *name, bool bypass) {
    auto module = getenv("ZPKT_AFP_MODULE_PATH");
    auto cf = ZpktTest::object();
    ZpktTest::value(cf.ptr(), "module", module ? module : ZPKT_AFP_MODULE);
    auto entry = ZpktTest::object(ZpktTest::object(cf.ptr(), "interfaces"), name);
    ZpktTest::value(entry, "rxSpin", "0");
    if (!bypass) ZpktTest::value(entry, "qdiscBypass", "false");
    try { env->init(cf.ptr()); }
    catch (const ZeException &err) { log(err); return false; }
    if (env->loopback()) return false;
    auto result = env->nif(&mx, name);
    if (result.is<ZeException>()) { log(result.p<ZeException>()); return false; }
    nif = result.p<Zpkt::Nif *>();
    if (nif->zeroCopy() || nif->mtu() != 1500 || nif->rxQueues() != 1) return false;
    ingress = nif->bufPool(Zi::IOBufType::Rx, 8, FullFrame, 32);
    tx = nif->bufPool(Zi::IOBufType::TxTCP, 8, FullFrame);
    control = nif->bufPool(Zi::IOBufType::TxUDP, 1, ShortFrame, ShortFrame);
    txPools.pushNode(tx.ptr());
    Zi::IOBufPool *reserves[] = {control.ptr()};
    nif->reserveTxPools(reserves);
    auto allocated = nif->alloc(ingress.ptr(), txPools);
    if (allocated.is<ZeException>()) { log(allocated.p<ZeException>()); return false; }
    return mx.start();
  }

  bool start() {
    received = 0;
    bool ok = nif->start([this](uint8_t *packet, unsigned length, ZiRxBurst *) {
      if (received < 4 && length <= FullFrame) {
	input[received].length = length;
	memcpy(input[received].bytes, packet, length);
      }
      ++received;
      ingress->free(packet);
      if (received == 1) nif->wake();
    });
    if (!ok) log(nif->error());
    return ok && !nif->tcpChksumOffload();
  }

  bool send(const Frame &frame) {
    auto pool = frame.length == ShortFrame ? control.ptr() : tx.ptr();
    auto packet = pool->alloc();
    if (!packet) return false;
    memcpy(packet, frame.bytes, frame.length);
    bool ok = ZmBlock<bool>{}([this, packet, length = frame.length](auto done) {
      mx.txRun([this, packet, length, done]() mutable {
	bool accepted = nif->txReady(packet) == Zi::OK && nif->send(packet, length) == Zi::OK;
	done(accepted && nif->pollTx() == 1 && nif->pollTx() == 0);
      });
    });
    pool->free(packet);
    return ok;
  }

  bool receive(const Frame &frame) {
    received = 0;
    ZmBlock<>{}([this](auto done) {
      mx.rxRun([this, done]() mutable { nif->rx(); done(); });
    });
    return received == 1 && input[0].length == frame.length &&
      !memcmp(input[0].bytes, frame.bytes, frame.length);
  }
};

static Frame makeFrame(Endpoint &src, Endpoint &dst, unsigned length, unsigned tag)
{
  Frame frame{length};
  memcpy(frame.bytes, dst.nif->mac().data(), 6);
  memcpy(frame.bytes + 6, src.nif->mac().data(), 6);
  frame.bytes[12] = 0x88;
  frame.bytes[13] = 0xb5;
  for (unsigned i = 14; i < length; ++i) frame.bytes[i] = uint8_t(i + tag);
  return frame;
}

static void testIngress()
{
  ZuTestScopeRT(testIngress);
  Endpoint peer;
  bool ready = peer.init("zafp1", false) && peer.start();
  ZuCheckRT(ready, log_("open wire peer"));
  if (!ready) return;
  int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  sockaddr_in dst{};
  dst.sin_family = AF_INET;
  dst.sin_port = htons(19001);
  inet_pton(AF_INET, "198.18.7.1", &dst.sin_addr);
  bool bound = fd >= 0 && !bind(fd, reinterpret_cast<sockaddr *>(&dst), sizeof(dst));
  ZuCheckRT(bound, log_("bind independent host UDP receiver"));
  if (bound) {
    // IPv4/UDP, 198.18.7.2 -> 198.18.7.1:19001, one payload byte, Ethernet padding.
    Frame probe{ShortFrame, {
      2,0,0,0,0,1, 0,0,0,0,0,0, 8,0,
      0x45,0,0,29, 0,0,0,0, 64,17,0xe0,0xa8, 198,18,7,2, 198,18,7,1,
      0x4a,0x38,0x4a,0x39, 0,9,0,0, 'Z'}};
    memcpy(probe.bytes + 6, peer.nif->mac().data(), 6);
    auto delivered = [&](bool expected) {
      if (!peer.send(probe)) return false;
      pollfd item{fd, POLLIN, 0};
      int n = poll(&item, 1, expected ? 1000 : 100);
      if (!expected) return !n;
      char byte = 0;
      return n == 1 && recv(fd, &byte, 1, MSG_DONTWAIT) == 1 && byte == 'Z';
    };
    ZuCheckRT(delivered(true), log_("Linux receives on the unclaimed veth"));
    Endpoint owner;
    bool opened = owner.init("zafp0", true);
    ZuCheckRT(opened && delivered(false), log_("open suppresses kernel UDP before start"));
    if (opened) {
      Endpoint contender;
      ZuCheckRT(!contender.init("zafp0", true) && delivered(false),
        log_("a second Env cannot displace the first interface claim"));
      bool running = owner.start();
      ZuCheckRT(running && delivered(false) && owner.receive(probe),
        log_("AFP receives the UDP frame while the kernel socket stays empty"));
      if (running) {
        ZuCheckRT(owner.nif->stop() && delivered(false), log_("stop retains the claim"));
      }
      auto held = owner.tx->alloc();
      owner.nif->final();
      ZuCheckRT(held && !!owner.nif->error() && delivered(false),
        log_("a held buffer keeps the claim across refused finalization"));
      if (held) owner.tx->free(held);
      owner.nif->final();
      ZuCheckRT(delivered(true), log_("final restores kernel UDP delivery"));
    }
  }
  if (fd >= 0) close(fd);
}

static void testWire()
{
  ZuTestScopeRT(testWire);
  for (bool bypass : {true, false}) {
    Endpoint a, b;
    bool opened = a.init("zafp0", bypass) && b.init("zafp1", bypass);
    ZuCheckRT(opened, log_("Env opens named AFP devices with qdisc bypass ", bypass));
    if (!opened) continue;
    bool running = a.start() && b.start();
    ZuCheckRT(running, log_("both AFP packet sockets activate"));
    if (!running) continue;
    a.nif->status();
    b.nif->status();
    auto aFull = makeFrame(a, b, FullFrame, 1);
    auto bFull = makeFrame(b, a, FullFrame, 2);
    auto aShort = makeFrame(a, b, ShortFrame, 3);
    auto bShort = makeFrame(b, a, ShortFrame, 4);
    bool posted = a.send(aFull) && b.send(bFull);
    ZuCheckRT(posted && a.receive(bFull) && b.receive(aFull),
      log_("full-MTU non-IP traffic reaches both peers with local outgoing copies suppressed"));
    posted = a.send(aShort) && b.send(bShort);
    ZuCheckRT(posted && a.receive(bShort) && b.receive(aShort),
      log_("both peers transmit exact-fit control frames and count each retirement once"));
    bool dormant = a.nif->stop() && b.send(bFull) && a.start();
    ZuCheckRT(dormant && b.send(bShort) && a.receive(bShort),
      log_("restart excludes traffic from the stopped run"));
    auto queues = a.nif->allQueues([](Ztc::Queue *) {});
    ZuCheckRT(queues == 2, log_("two active queue views available"));
  }
}

int main(int argc, char **argv)
{
  if (!getenv("ZPKT_AFP_PRIVATE")) {
    if (geteuid() || !getenv("ZPKT_RUN_FIXTURE")) {
      std::cout << "1..0 # SKIP requires root and ZPKT_RUN_FIXTURE=1 for private veth\n";
      return 0;
    }
    execl("/bin/bash", "bash", ZPKT_AFP_FIXTURE, argv[0], static_cast<char *>(nullptr));
    return 1;
  }
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testIngress);
  ZuTestCall(testWire);
}
