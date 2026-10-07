//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// JSON-RPC WebSocket message assembly and profile-independent binding

#ifndef ZjrpcWS_HH
#define ZjrpcWS_HH

#ifndef ZjrpcLib_HH
#include <zlib/ZjrpcLib.hh>
#endif

#include <zlib/ZmHeap.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmRef.hh>

#include <zlib/ZiAssert.hh>

#include <zlib/Zws.hh>

#include <zlib/ZjrpcIO.hh>

namespace Zjrpc {

template <typename Link, typename Heap = ZuVoid>
struct WSOpen_ : public Heap, public ZmObject {
  ZmRef<Link> link;
  Zhttp::ConnectedInfo info;
  WSOpen_(Link *link_, Zhttp::ConnectedInfo info_) :
    link{link_}, info{ZuMv(info_)} { }
};
template <typename Link>
using WSOpenHeap = ZmHeap<"Zjrpc.WS.Open", WSOpen_<Link>>;
template <typename Link>
using WSOpen = WSOpen_<Link, WSOpenHeap<Link>>;

class WSConfig {
public:
  const Limits &limits() const { return m_limits; }
  WSConfig &&limits(Limits value) { m_limits = value; return ZuMv(*this); }
  Zws::Config &ws() { return m_ws; }
  const Zws::Config &ws() const { return m_ws; }
  template <typename L>
  WSConfig &&ws(L &&l) { ZuFwd<L>(l)(m_ws); return ZuMv(*this); }

  Zws::Config binding() const {
    auto config = m_ws;
    if (config.maxMessage > m_limits.maxJSONBytes) config.maxMessage = m_limits.maxJSONBytes;
    if (config.maxQueuedInput > m_limits.maxQueueBytes)
      config.maxQueuedInput = m_limits.maxQueueBytes;
    return config;
  }
private:
  Limits m_limits;
  Zws::Config m_ws;
};

// All state here is Rx-owned. Credits cover complete messages posted to Tx,
// where Zws's own Rx queue no longer accounts for their retained input buffers.
class WSRx {
public:
  int begin(Zws::Opcode::T opcode, const Limits &limits) {
    if (m_closed || opcode != Zws::Opcode::Text) return -1;
    return m_input.begin(0, limits.maxJSONBytes) ? 1 : -1;
  }
  template <typename Rx>
  int process(Rx &rx) {
    bool ok = Zhttp::bodyEach(rx, [this](ZuSpan<uint8_t> span) { (void)m_input.feed(span); });
    return ok && m_input.buffer() ? 1 : -1;
  }
  void open() { m_closed = false; }
  void close() { m_closed = true; m_input.reset(); }
  bool up() const { return !m_closed; }

  template <typename Endpoint, typename Link>
  int end(Endpoint *endpoint, Link &link) {
    auto body = m_input.take();
    if (m_closed || !body) return -1;
    if (!endpoint->wsAccept()) return 1;
    const auto &limits = endpoint->limits();
    unsigned length = body->length;
    if (m_count >= limits.maxQueue || m_bytes > limits.maxQueueBytes ||
	length > limits.maxQueueBytes - m_bytes) return -1;
    ++m_count;
    m_bytes += length;
    link.app()->txRun([
      endpoint, link = ZmRef{&link}, body = ZuMv(body), length]() mutable {
      endpoint->wsFrame_(*link, ZuMv(body));
      auto hub = link->app();
      hub->rxRun([link = ZuMv(link), length]() mutable {
	link->state().rx.ack(length);
      });
    });
    return 1;
  }
  void ack(unsigned length) { --m_count; m_bytes -= length; }
private:
  Input m_input;
  uint64_t m_bytes = 0;
  unsigned m_count = 0;
  bool m_closed = true;
};

struct WSClientState { WSRx rx; };

// Zws performs all framing, masking, fragmentation, control and close handling.
// The callbacks assemble one mutable JSON message, then hand it to its owner.
template <typename Endpoint>
class WSIO {
public:
  template <typename Link>
  int messageStart(Link &link, Zws::Opcode::T opcode) {
    if (opcode != Zws::Opcode::Text) {
      link.close(Zws::CloseCode::Unsupported);
      return -1;
    }
    int rc = link.state().rx.begin(opcode, endpoint()->limits());
    if (rc < 0) link.close(Zws::CloseCode::TooLarge);
    return rc;
  }
  template <typename Link, typename Rx>
  int process(Link &link, Rx &rx) {
    int rc = link.state().rx.process(rx);
    if (rc < 0) link.close(Zws::CloseCode::TooLarge);
    return rc;
  }
  template <typename Link>
  int messageEnd(Link &link) {
    int rc = link.state().rx.end(endpoint(), link);
    if (rc < 0) link.close(Zws::CloseCode::Policy);
    return rc;
  }

  template <typename Link>
  void pong(Link &link, ZuBSpan payload) { control_<false>(link, 0, payload); }

protected:
  // Rx owner: established peers close through Zws's handshake and timer.
  // A connection still opening has no WebSocket channel to close yet.
  template <typename Link>
  void stopWS_(Link &link) {
    if (link.state().rx.up()) link.close(Zws::CloseCode::GoingAway);
    else link.disconnect();
  }
  template <bool Closed, typename Link>
  void control_(Link &link, uint16_t code, ZuBSpan payload) {
    ZmRef<ZiIOBuf> body = new InputBuf{};
    if (!body->append(payload)) { link.close(Zws::CloseCode::Internal); return; }
    link.app()->txRun([this, link = ZmRef{&link}, body = ZuMv(body), code]() mutable {
      (void)code;
      try {
	if constexpr (Closed)
	  Zws::H1_::closed(*endpoint()->impl(), *link, code, ZuBSpan{*body}, 0);
	else
	  Zws::H1_::pong(*endpoint()->impl(), *link, ZuBSpan{*body}, 0);
      } catch (...) { link->close(Zws::CloseCode::Internal); }
    });
  }
  template <typename Link, typename M>
  bool sendWS_(Link &link, const M &message) {
    ZiAssert(endpoint()->invoked(), "Zjrpc", (), "WS send outside owner shard", return false);
    bool ok = false;
    link.txStream_([&message, &ok, this](auto &tx) {
      Output out{tx, endpoint()->limits().maxJSONBytes};
      message.write(out);
      if (!out) { tx.fail(); return; }
      ok = out.flush() && tx.valid();
    }, Zws::Opcode::Text);
    return ok;
  }
private:
  auto endpoint() { return static_cast<Endpoint *>(this); }
};

} // Zjrpc

#endif /* ZjrpcWS_HH */
