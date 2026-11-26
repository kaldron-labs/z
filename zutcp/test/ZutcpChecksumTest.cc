//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>
#include <string.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/Zutcp.hh>
#include <zlib/ZutcpChecksum.hh>

#include "ZutcpTestMxPlugin.hh"

using namespace ZuTestUtil;

namespace Zutcp {

struct TCPChecksumTest {
  using SumFn = unsigned (*)(const void *, unsigned);

  static unsigned reference(const uint8_t *p, unsigned length) {
    uint64_t value = 0;
    for (unsigned i = 0; i < length; ++i)
      value += uint64_t(p[i]) << ((i & 1) ? 0 : 8);
    while (value>>16) value = (value & 0xffff) + (value>>16);
    return unsigned(value);
  }

  static bool check(SumFn fn) {
    // Two full 16-bit segment extents plus every SIMD alignment.
    uint8_t bytes[2 * (1U<<16) + 64];
    constexpr unsigned lengths[] = {
      0, 1, 2, 3, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65,
      127, 128, 129, 255, 256, 257, 511, 512, 513, 1459, 1460,
      1500, 4095, 4096, 8192, 9000, 65534, 65535, 65536, 65537, 131071};
    for (unsigned pattern = 0; pattern < 3; ++pattern) {
      uint32_t random = 0x729ac13b;
      for (auto &byte : bytes) {
	random ^= random<<13; random ^= random>>17; random ^= random<<5;
	byte = pattern == 0 ? 0 : pattern == 1 ? 0xff : uint8_t(random);
      }
      for (unsigned offset = 0; offset < 64; ++offset)
	for (auto length : lengths)
	  if (fn(bytes + offset, length) != reference(bytes + offset, length))
	    return false;
    }
    return true;
  }

  static void testSums() {
    ZuTestScope(testSums);
    ZuCheck(check(TCPChecksum::scalar));
    ZuCheck(check(static_cast<SumFn>(&TCPChecksum::sum)));
#if defined(__x86_64__)
    ZuCheck(check(TCPChecksum::sse2));
    __builtin_cpu_init();
    ZuCheck(!__builtin_cpu_supports("avx2") || check(TCPChecksum::avx2));
#elif defined(__ARM_NEON) || defined(__aarch64__)
    ZuCheck(check(TCPChecksum::neon));
#endif
  }

  static void testTCP() {
    ZuTestScopeRT(testTCP);
    constexpr unsigned payloads[] = {
      0, 1, 2, 3, 5, 7, 15, 31, 32, 33, 63, 64, 65, 127, 128, 255, 512, 1400, 1460};
    for (bool v6 : {false, true}) {
      ZiIP local{v6 ? "2001:db8::1" : "192.0.2.1"};
      ZiIP remote{v6 ? "2001:db8::2" : "192.0.2.2"};
      auto src = v6 ? reinterpret_cast<const uint8_t *>(&local.in6Addr()) :
	reinterpret_cast<const uint8_t *>(&local.inAddr());
      auto dst = v6 ? reinterpret_cast<const uint8_t *>(&remote.in6Addr()) :
	reinterpret_cast<const uint8_t *>(&remote.inAddr());
      bool passed = true;
      for (unsigned options : {0U, 12U, 40U}) for (auto payload : payloads) {
	uint8_t frame[1600]; // full MTU plus maximum IP/TCP option headers
	memset(frame, 0xa5, sizeof(frame));
	auto ip = frame + 14;
	unsigned ipHead = v6 ? 40 : 20, tcpHead = 20 + options;
	unsigned length = tcpHead + payload, size = ipHead + length;
	unsigned wire = 14 + size;
	if (wire < 60) wire = 60;
	memset(ip, 0, ipHead);
	ip[0] = v6 ? 0x60 : 0x45;
	if (v6) {
	  ip[4] = length>>8; ip[5] = length; ip[6] = 6;
	  memcpy(ip + 8, src, 16); memcpy(ip + 24, dst, 16);
	} else {
	  ip[2] = size>>8; ip[3] = size; ip[9] = 6;
	  memcpy(ip + 12, src, 4); memcpy(ip + 16, dst, 4);
	}
	auto tcp = ip + ipHead;
	for (unsigned i = 0; i < length; ++i) tcp[i] = uint8_t(i ^ (i>>3));
	tcp[12] = (tcpHead>>2)<<4;
	tcp[16] = 0x34; tcp[17] = 0x56; // stale completion or hardware seed
	passed &= MxPlugin::tcpChecksum(frame, wire, {14, uint16_t(14 + ipHead)});
	passed &= !MxPlugin::tcpChecksum(local, remote, {tcp, length});
	unsigned old = (unsigned(tcp[16])<<8) | tcp[17];
	tcp[16] = tcp[17] = 0;
        uint64_t pseudo = reference(src, v6 ? 16 : 4) + reference(dst, v6 ? 16 : 4) + length + 6;
        uint64_t sum = pseudo + reference(tcp, length);
        while (pseudo>>16) pseudo = (pseudo & 0xffff) + (pseudo>>16);
	while (sum>>16) sum = (sum & 0xffff) + (sum>>16);
	passed &= old == (unsigned(sum) ^ 0xffff);
	tcp[16] = old>>8; tcp[17] = old;
	for (unsigned i = 14 + size; i < wire; ++i) frame[i] ^= 0xff;
	passed &= !MxPlugin::tcpChecksum(local, remote, {tcp, length});
	TCPChecksum metadata;
	passed &= metadata.init(frame, wire, {14, uint16_t(14 + ipHead)});
	tcp[8] ^= 1;
	passed &= MxPlugin::tcpChecksum(local, remote, {tcp, length}) != 0;
	passed &= MxPlugin::tcpChecksum(frame, wire, {14, uint16_t(14 + ipHead)}, true);
	passed &= ((unsigned(tcp[16])<<8) | tcp[17]) == pseudo;
	metadata.finish();
	passed &= !MxPlugin::tcpChecksum(local, remote, {tcp, length});
      }
      ZuCheckRT(passed, log_("TCP utility/finalization covers payloads, options, padding and stale seeds"));
    }
  }

  static void testBounds() {
    ZuTestScope(testBounds);
    uint8_t frame[80]{}, saved[80]{};
    frame[14] = 0x45;
    frame[16] = 0; frame[17] = 40; frame[23] = 6;
    frame[46] = 0x50;
    memcpy(saved, frame, sizeof(frame));
    ZuCheck(!MxPlugin::tcpChecksum(nullptr, sizeof(frame), {14, 34}));
    ZuCheck(!MxPlugin::tcpChecksum(frame, 53, {14, 34}));
    ZuCheck(!MxPlugin::tcpChecksum(frame, sizeof(frame), {14, 35}));
    frame[46] = 0x40;
    saved[46] = 0x40;
    ZuCheck(!MxPlugin::tcpChecksum(frame, sizeof(frame), {14, 34}) &&
      !memcmp(frame, saved, sizeof(frame)));
    ZiIP local{"192.0.2.1"}, remote{"2001:db8::1"};
    ZuCheck(MxPlugin::tcpChecksum(local, remote, {frame + 34, 20}) != 0);
    ZuCheck(MxPlugin::tcpChecksum(local, local, {frame + 34, 19}) != 0);
  }

  struct Device : ZmNHeap<Device>, ZutcpTest::Xdp {
  friend inline ZuStringT<"Zutcp.ChecksumTest.Device"> ZmNHeapID(Device *);
    bool offload = false, exact = true;
    unsigned pending = 0, calls = 0, refusals = 0;

    using ZmNHeap<Device>::operator new;
    using ZmNHeap<Device>::operator delete;
    using Xdp::Xdp;
    void advertise(bool enabled) { tcpChksumOffload(enabled); }
    bool send(uint8_t *frame, unsigned wire) override {
      ++calls;
      IPView ip;
      TCPView tcp;
      bool parsed = readIP({frame, wire}, ip) && readTCP(ip, tcp);
      exact &= parsed;
      if (!parsed) return false;
      auto segment = ip.payload.data();
      unsigned actual = (unsigned(segment[16])<<8) | segment[17];
      if (offload) {
	bool v6 = ip.local.v6();
	auto src = v6 ? reinterpret_cast<const uint8_t *>(&ip.local.in6Addr()) :
	  reinterpret_cast<const uint8_t *>(&ip.local.inAddr());
	auto dst = v6 ? reinterpret_cast<const uint8_t *>(&ip.remote.in6Addr()) :
	  reinterpret_cast<const uint8_t *>(&ip.remote.inAddr());
	uint64_t pseudo = reference(src, v6 ? 16 : 4) + reference(dst, v6 ? 16 : 4) +
	  ip.payload.length() + 6;
	while (pseudo>>16) pseudo = (pseudo & 0xffff) + (pseudo>>16);
	exact &= actual == pseudo;
      } else exact &= !Zutcp::MxPlugin::tcpChecksum(ip.local, ip.remote, ip.payload);
      if (refusals) { --refusals; return false; }
      ++pending;
      return true;
    }
    unsigned pollTx() override { unsigned n = pending; pending = 0; return n; }
  };

  static void testRestartSelection(unsigned retries) {
    ZuTestScopeRT(testRestartSelection);
    auto env = ZutcpTest::fixtureEnv();
    ZmRef<MxPlugin> stack = new MxPlugin;
    ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) { s.nThreads(3); })
      .rxThread(1).txThread(2), stack};
    ZmRef<Device> device = new Device{&mx};
    ZmRef<Zpkt::Nif> backend = device;
    bool configured = false;
    try { stack->init(&mx, env, ZuMv(backend), {.txRetries = retries}); configured = true; }
    catch (ZeException &e) {
      std::cerr << e << '\n' << std::flush;
    }
    ZuCheckRT(configured, log_("configure checksum capability fixture"));
    if (!configured) return;
    unsigned runs = 0;
    bool exact = true;
    for (bool offload : {false, true, false}) {
      device->offload = offload;
      device->advertise(offload);
      if (!mx.start()) { exact = false; break; }
      ++runs;
      device->advertise(!offload); // TCP must use the mode sampled at startup.
      ZmSemaphore done;
      mx.txRun([&mx, stack, device, &done, &exact, retries] {
	for (bool v6 : {false, true}) for (unsigned payload : {0U, 1U, 1400U}) {
	  IPPath path{.local = ZiIP{v6 ? "2001:db8::1" : "192.0.2.1"},
	    .remote = ZiIP{v6 ? "2001:db8::2" : "192.0.2.2"}, .mtu = 1500};
	  auto packet = stack->allocPacket(&mx, path.local.type());
	  if (!packet) { exact = false; continue; }
	  TCPHeader header{1234, 5678, 1, 2, 4096, 0, 0x10};
	  memset(packet->data_() + tcpHead(path.local.type()), 0x5a, payload);
	  unsigned wire = writeTCP(packet->data_(), packet->size, path, header, payload, 0);
	  packet->skip = 0; packet->length = wire;
	  TCPLayout layout{14, uint16_t(ipHead(path.local.type()))};
	  auto field = packet->data_() + layout.tcp + 16;
	  const bool idle = !stack->txAdmission().outstanding;
	  TxRetry retry{.spin = true};
	  auto calls = device->calls;
	  device->refusals = retries + 1;
	  exact &= wire && !stack->sendPacket(packet, nullptr, &layout, &retry) &&
	    retry.refused == idle && device->calls == calls + 1 + (idle ? retries : 0) &&
	    !field[0] && !field[1] && packet->length == wire;
	  if (idle) {
	    retry.spin = false;
	    device->refusals = 1;
	    calls = device->calls;
	    exact &= !stack->sendPacket(packet, nullptr, &layout, &retry) &&
	      retry.refused && device->calls == calls + 1 && packet->length == wire;
	  }
	  // A fresh spin-enabled submission can succeed inside the same call.
	  retry.spin = true;
	  device->refusals = idle ? retries : 0;
	  calls = device->calls;
	  exact &= stack->sendPacket(packet, nullptr, &layout, &retry) &&
	    !retry.refused && device->calls == calls + 1 + (idle ? retries : 0) && !packet->data_();
	}
	done.post();
      });
      done.wait();
      mx.stop();
      exact &= !device->pending;
    }
    ZuCheckRT(exact && device->exact && runs == 3,
      log_("start caches active offload once per run; restart switches software/seed paths, "
	"refusal restores checksum and acceptance detaches backing for IPv4/IPv6"));
  }

};

} // namespace Zutcp

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(Zutcp::TCPChecksumTest::testSums);
  ZuTestCall(Zutcp::TCPChecksumTest::testTCP);
  ZuTestCall(Zutcp::TCPChecksumTest::testBounds);
  ZuTestCall(Zutcp::TCPChecksumTest::testRestartSelection, 0);
  ZuTestCall(Zutcp::TCPChecksumTest::testRestartSelection, 3);
}
