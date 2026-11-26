//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zquic.hh>
#include <zlib/ZtlsEngineOpenSSL.hh>

#include "ZutcpTestUDPDevice.hh"

using namespace ZuTestUtil;

namespace {

// Substitute the next hop at socket setup. The QUIC endpoint factory receives
// the same published native adapter as a successful connected UDP lookup.
class Stack : public Zutcp::MxPlugin {
public:
  void udp(ZiConnectFn fn, ZiFailFn fail, ZiIPType::T, ZiIP local, uint16_t port,
      ZiIP remote, uint16_t peerPort, Zi::IOBufAllocator *rx, ZiCxnOptions opts) override {
    Zutcp::IPPath path{local, remote, {2, 0, 0, 0, 0, 1}, {2, 0, 0, 0, 0, 2}, 1500};
    ZiCxnInfo info{.type = ZiCxnType::UDP, .socket = Zi::nullSocket(), .options = ZuMv(opts),
      .local = {local, port}, .remote = {remote, peerPort}, .mps = 1472, .rxAllocator = rx};
    if (!ZutcpTest::MxPlugin::publishUDP(*this, ZuMv(fn), ZuMv(info), path)) fail(false);
  }
};

struct Endpoint : Zquic_::Endpoint_<Endpoint> {
  enum { EndpointRef = false };
  ZmSemaphore opened;
  bool ready = false;
  void endpointReady(Endpoint_ *) { ready = true; opened.post(); }
  void endpointFailed(bool) { opened.post(); }
  void endpointDown_(Endpoint_ *) { }
  template <typename Packets> void endpointPacket(Packets) { }
};

void testNativeQueue()
{
  ZuTestScope(testNativeQueue);
  auto env = ZutcpTest::fixtureEnv();
  ZmRef<Stack> stack = new Stack;
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) { s.nThreads(3); })
    .rxThread(1).txThread(2), stack};
  ZmRef<UDPLeaseTest::Device> device = new UDPLeaseTest::Device{&mx};
  Zutcp::MxParams params;
  params.txRetries = 0;
  params.txBackoff = ZuTime{10};
  stack->init(&mx, env, ZmRef<Zpkt::Nif>{device}, params);
  Endpoint ep;
  auto rx = Zquic::pktRxAllocator(&mx);
  auto tx = mx.allocator<Zi::IOBufType::TxUDP, Zquic_::TxPkt, Zquic::BufSize>(4);
  ZiSockAddr peer{ZiIP{"192.0.2.2"}, 4433};
  ZuCheck(rx && tx && ep.init(&mx, rx, tx) && mx.start(), (return));
  bool opened = ep.openUDP(Zquic::PathMode::ClientConnected, ZiIP{"192.0.2.1"}, 2381,
    peer.ip(), peer.port()) && !ep.opened.timedwait(Zm::now(3)) && ep.ready;
  ZuCheck(opened);
  if (opened) {
    Zquic_::EndpointTxWait wait;
    Zquic_::EndpointTxToken token;
    ZmSemaphore granted, retired;
    bool consumed = false;
    mx.txRun([&] {
      ZutcpTest::MxPlugin::udpRouteEpoch(*stack);
      ep.waitTx(wait, mx.txThread(), 1, [&](int result, auto value) {
	if (result == Zquic_::EndpointTxResult::Ready) { token = value; granted.post(); }
	else { consumed = result == Zquic_::EndpointTxResult::Retired; retired.post(); }
      });
    });
    bool reserved = !granted.timedwait(Zm::now(3)) && token;
    ZuCheck(reserved);
    bool ordered = reserved && ZmBlock<bool>{}([&](auto done) {
      mx.txRun([&, done = ZuMv(done)]() mutable {
	Zquic::TrafficSecret secret;
	Zquic::PktProtState keys;
	uint8_t seed[32]{}, header[32], plain[16]{1}; // PING and padding
	bool ok = Zquic::PktProt::deriveSecret(secret, &Ztls_::opensslAes128gcmsha256,
	  Zquic::byteSpan(seed, sizeof(seed))) &&
	  keys.init(secret, Zquic::PktNumSpace::AppData, true);
	ZtArray<uint8_t> expected[2];
	device->refuse = true;
	for (unsigned i = 0; ok && i < 2; ++i) {
	  unsigned pn = i + 1;
	  auto buf = ep.allocTxPkt();
	  int h = Zquic::Pkt::writeShort(header, sizeof(header), Zquic::CxnID{"server01"}, pn, 2);
	  if (!buf || h < 2) { ok = false; break; }
	  int n = Zquic::PktProt::protectShort(buf->data(), buf->size - buf->skip, keys, pn,
	    Zquic::byteSpan(header, h), Zquic::byteSpan(plain, sizeof(plain)), h - 2, 2);
	  if (n <= 0) { ok = false; break; }
	  buf->length = unsigned(n);
	  expected[i].append(buf->data(), n);
	  ok = ep.send(ZuMv(buf), peer, Zquic::EcnMark::ECT0, false,
	    i ? Zquic_::EndpointTxToken{} : token);
	}
	auto d = ep.diag();
	ok &= d.txPending == 1 && d.txQueued == 1 && !d.tx.datagramsTx &&
	  !d.tx.txDropped && !d.failures() && device->calls == 1 && !device->frames.length();
	device->refuse = false;
	using Cxn = Zquic_::EndpointCxn<Zquic_::Endpoint_<Endpoint>>;
	auto cxn = static_cast<Cxn *>(ep.pktAllocTarget().physical);
	ok &= cxn && ZutcpTest::MxPlugin::expireUDP(*stack, cxn);
	d = ep.diag();
	ok &= !d.txPending && !d.txQueued && d.tx.datagramsTx == 2 &&
	  !d.tx.txDropped && !d.failures() && device->frames.length() == 2;
	for (unsigned i = 0; ok && i < 2; ++i) {
	  Zutcp::IPView ip;
	  Zutcp::UDPView udp;
	  auto &frame = device->frames[i];
	  ok = Zutcp::readIP({frame.data(), frame.length()}, ip) &&
	    Zutcp::readUDP(ip, udp) && udp.dstPort == peer.port() &&
	    udp.payload.length() == expected[i].length() &&
	    !memcmp(udp.payload.data(), expected[i].data(), expected[i].length());
	}
	device->retire = true;
	stack->reconcileTx();
	done(ok && device->pool->balanced());
      });
    });
    ZuCheck(ordered);
    bool finished = !retired.timedwait(Zm::now(3));
    if (!finished) {
      ZmBlock<>{}([&](auto done) {
	mx.txRun([&, done = ZuMv(done)]() mutable { ep.cancelTx(wait, ZuMv(done)); });
      });
    }
    ZuCheck(finished && consumed);
  }
  ZmBlock<>{}([&](auto done) { ep.disconnect([done = ZuMv(done)]() mutable { done(); }); });
  ZuCheck(mx.stop());
}
} // namespace

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testNativeQueue);
}
