//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>
#include <net/if.h>
#include <string.h>

#include <zlib/ZiLog.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/Zutcp.hh>

#include "ZutcpTestInstance.hh"

enum { ServerUDP = 18172, ClientUDP = 18173, ServerTCP = 18174 };
static constexpr auto TCPData = "tcp fixture bytes"_z;
static const ZuBSpan UDPData[] = {"first datagram", "", "a longer final datagram"};

// The transmitted IP/UDP frame must surround the caller's own payload backing.
// Completion leaves its descriptor and payload intact through the Zi callback.
static bool framedUDP(const ZiIOBuf *buf, ZuBSpan payload)
{
  if (!buf || !buf->registered() || buf->cspan() != payload) return false;
  for (auto family : {ZiIPType::V4, ZiIPType::V6}) {
    unsigned head = Zutcp::udpHead(family);
    if (buf->skip < head) continue;
    unsigned wire = head + buf->length;
    if (wire < 60) wire = 60; // Ethernet minimum, excluding FCS
    if (wire > buf->size - buf->skip + head) continue;
    Zutcp::IPView ip;
    Zutcp::UDPView udp;
    if (Zutcp::readIP({buf->data() - head, wire}, ip) &&
	Zutcp::readUDP(ip, udp) && udp.payload.data() == buf->data() &&
	udp.payload == payload) return true;
  }
  return false;
}

template <bool UDP> struct Input : ZiIOBuf { using ZiIOBuf::ZiIOBuf; };

struct Exchange;
class Connection : public ZiConnection, public ZmNHeap<Connection> {
friend inline ZuStringT<"Zutcp.UDPTest.Connection"> ZmNHeapID(Connection *);
public:
  Connection(ZiMultiplex *mx, const ZiCxnInfo &info, Exchange *exchange) :
    ZiConnection{mx, info}, m_exchange{exchange} { }
  void connected(ZiRxContext &) override;
  void disconnected(bool) override;
  void write(ZuBSpan, ZiSockAddr peer = {});

private:
  bool received(ZiRxContext &);
  Exchange *m_exchange;
  ZiIOBuf *m_prepared = nullptr;
  unsigned m_streamBytes = 0;
};

struct Exchange {
  Zi::IOBufAllocator *rx, *tx;
  ZiIP local, peer;
  bool server, udp;
  ZmRef<Connection> connection;
  ZmRef<ZiIOBuf> retained;
  ZmAtomic<unsigned> errors = 0, datagrams = 0;
  ZmSemaphore opened, input, output, retired;

  ZiConnectFn callback(ZiMultiplex *mx) {
    return ZiConnectFn{[this, mx](const ZiCxnInfo &info) -> ZiConnection * {
      connection = new Connection{mx, info, this};
      return connection;
    }};
  }
  ZiFailFn failed() {
    return ZiFailFn{[this](bool) { ++errors; opened.post(); }};
  }
};

void Connection::connected(ZiRxContext &io)
{
  auto input = info().rxAllocator->allocBuf(this);
  if (!input || input->data_() || info().rxAllocator != m_exchange->rx ||
      !info().rxAllocator->pool || (info().type == ZiCxnType::UDP) != m_exchange->udp)
    ++m_exchange->errors;
  if (input) {
    m_prepared = input.ptr();
    io.init(ZiRxFn{this, ZmFnPtr<&Connection::received>{}}, ZuMv(input));
  }
  m_exchange->opened.post();
}

void Connection::disconnected(bool)
{
  if (up() || plugin() || !rxContext().completed() || !txContext().completed())
    ++m_exchange->errors;
  m_exchange->retired.post();
}

bool Connection::received(ZiRxContext &io)
{
  auto state = m_exchange;
  if (state->udp) {
    auto index = state->datagrams++;
    if (!io.buf || index >= sizeof(UDPData)/sizeof(UDPData[0])) {
      ++state->errors;
    } else {
      auto expected = UDPData[index];
      if (io.buf.ptr() != m_prepared || !dynamic_cast<Input<true> *>(io.buf.ptr()) ||
	  io.buf->allocator() != state->rx || io.buf->owner != this || !io.buf->registered() ||
	  io.buf->cspan() != expected || io.addr.ip() != state->peer ||
	  io.addr.port() != (state->server ? ClientUDP : ServerUDP) ||
	  !io.tos.is<uint8_t>() || io.tos.p<uint8_t>() != 0x28) ++state->errors;
      if (!index) state->retained = io.buf;
      if (state->server) write(expected, io.addr);
    }
  } else {
    if (!io.buf || !io.buf->length) return true;
    if (!dynamic_cast<Input<false> *>(io.buf.ptr()) || io.buf->allocator() != state->rx ||
	io.buf->length > TCPData.length() - m_streamBytes ||
	memcmp(io.buf->data(), TCPData.data() + m_streamBytes, io.buf->length)) {
      ++state->errors;
      io.disconnect();
      state->input.post();
      return true;
    }
    m_streamBytes += io.buf->length;
    if (m_streamBytes != TCPData.length()) {
      io.buf = state->rx->allocBuf(this);
      return true;
    }
    if (state->server) write(TCPData);
  }
  io.buf = state->rx->allocBuf(this);
  m_prepared = io.buf.ptr();
  state->input.post();
  return true;
}

void Connection::write(ZuBSpan payload, ZiSockAddr peer)
{
  send(ZiTxFn{[this, payload, peer](ZiTxContext &io) {
    auto state = m_exchange;
    auto buf = state->tx->allocBuf(this);
    if (!buf || !buf->append(payload)) {
      ++state->errors;
      io.complete();
      state->output.post();
      return false;
    }
    io.init(ZiTxFn{[state, payload, original = buf.ptr(),
	backing = buf->data_(), skip = buf->skip, size = buf->size,
	owner = buf->owner](ZiTxContext &io) {
      if (state->udp && (!io.buf || io.buf.ptr() != original ||
	  io.buf->data_() != backing || io.buf->skip != skip || io.buf->size != size ||
	  io.buf->owner != owner)) ++state->errors;
      if (io.length < 0 || unsigned(io.length) != payload.length() ||
	  (state->udp && !framedUDP(io.buf.ptr(), payload))) ++state->errors;
      auto output = &state->output;
      io.complete();
      output->post();
      return true;
    }}, ZuMv(buf), peer, ZiTOS{uint8_t(0x28)});
    return true;
  }});
}

static bool wait(ZmSemaphore &sem)
{
  return sem.timedwait(Zm::now(10)) == 0;
}

static bool run(ZiMultiplex &mx, Exchange &udp, Exchange *tcp, unsigned cycle)
{
  auto options = ZiCxnOptions{}.udp(true);
  mx.udp(udp.callback(&mx), udp.failed(), udp.local, udp.server ? ServerUDP : ClientUDP,
    udp.server ? ZiIP{} : udp.peer, udp.server ? 0 : ServerUDP, udp.rx, options);
  if (!wait(udp.opened) || !udp.connection || udp.errors.load_()) return false;
  if (tcp && udp.server) {
    ZmSemaphore listening;
    mx.listen(ZiListenFn{[&listening](const ZiListenInfo &) { listening.post(); }},
      ZiFailFn{[tcp, &listening](bool) { ++tcp->errors; listening.post(); }},
      tcp->callback(&mx), tcp->local, ServerTCP, 8, tcp->rx);
    if (!wait(listening) || tcp->errors.load_()) return false;
  }
  std::cout << "ready " << cycle << std::endl;
  char go;
  if (!(std::cin >> go) || go != 'g') return false;
  if (!udp.server) {
    if (tcp) {
      mx.connect(tcp->callback(&mx), tcp->failed(),
	tcp->local, 0, tcp->peer, ServerTCP, tcp->rx);
      if (!wait(tcp->opened) || !tcp->connection || tcp->errors.load_()) return false;
      tcp->connection->write(TCPData);
    }
  } else if (tcp && (!wait(tcp->opened) || !tcp->connection || tcp->errors.load_()))
    return false;
  for (auto payload : UDPData) {
    if (!udp.server) udp.connection->write(payload);
    if (!wait(udp.input) || !wait(udp.output) || udp.errors.load_()) return false;
  }
  if (tcp && (!wait(tcp->input) || !wait(tcp->output) || tcp->errors.load_())) return false;
  if (!udp.retained || udp.retained->cspan() != UDPData[0]) return false;
  udp.retained = nullptr;
  udp.connection->close();
  if (!wait(udp.retired) || udp.errors.load_()) return false;
  // Both peers acknowledge traffic before either starts tearing TCP down.
  std::cout << "passed " << cycle << std::endl;
  return std::cin >> go && go == 'g';
}

int main(int argc, char **argv)
{
  if (argc != 6) {
    std::cerr << "usage: zutcpudptest interface local peer server|client udp|mixed\n";
    return 2;
  }
  auto ifindex = if_nametoindex(argv[1]);
  ZiIP local{argv[2]}, peer{argv[3]};
  bool server = !strcmp(argv[4], "server"), mixed = !strcmp(argv[5], "mixed");
  if (!ifindex || !local || !peer || local.type() != peer.type()) return 2;
  ZmRef<Zpkt::Env> env = new Zpkt::Env;
  ZmRef<Zutcp::Instance> transport = new Zutcp::Instance;
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(4).thread(4, [](auto &t) { t.isolated(true); });
  }), transport};
  auto xdp = ZutcpTest::openXdp(env, &mx, ifindex, 4);
  auto initialized = transport->init(&mx, env, ZuMv(xdp), {});
  if (initialized.is<ZeException>()) {
    ZiLogEvent(ZuMv(initialized.p<ZeException>())); return 1;
  }
  auto udpRx = mx.allocator<Zi::IOBufType::Rx, Input<true>>();
  auto udpTx = mx.allocator<Zi::IOBufType::TxUDP>(16);
  auto tcpRx = mixed ? mx.allocator<Zi::IOBufType::Rx, Input<false>>() : nullptr;
  auto tcpTx = mixed ? mx.allocator<Zi::IOBufType::TxTCP>(16) : nullptr;
  if (!udpRx || !udpTx || (mixed && (!tcpRx || !tcpTx || tcpRx->pool != udpRx->pool)))
    return 1;
  auto allocated = mx.alloc();
  if (allocated.is<ZeException>()) { ZiLogEvent(ZuMv(allocated.p<ZeException>())); return 1; }
  for (unsigned cycle = 0; cycle < 2; ++cycle) {
    if (!mx.start()) return 1;
    Exchange udp{udpRx, udpTx, local, peer, server, true};
    Exchange tcp{tcpRx, tcpTx, local, peer, server, false};
    bool passed = run(mx, udp, mixed ? &tcp : nullptr, cycle);
    bool stopped = mx.stop();
    if (!passed || !stopped) return 1;
  }
}
