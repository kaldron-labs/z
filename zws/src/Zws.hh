//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z websockets library

#ifndef Zws_HH
#define Zws_HH

#include <zlib/ZwsLib.hh>

#include <zlib/Ztls.hh>
#include <zlib/Zhttp.hh>

namespace Zws {

template <typename App, typename Link> class Client;

template <
  typename App_,
  typename Impl_,
  typename IOBufAlloc_ = Ztls::IOBufAlloc<>>
class CliLink :
  public Ztls::CliLink<App_, CliLink<App_, Impl_, IOBufAlloc_>>,
  public ZiRx<CliLink<App_, Impl_, IOBufAlloc_>, IOBufAlloc_> {
public:
  using App = App_;
  using Impl = Impl_;
  using Base = Ztls::CliLink<App, Impl>;
  using IOBufAlloc = IOBufAlloc_;
  using Rx = ZiRx<CliLink, IOBufAlloc>;

  using Base::impl;
  using Base::app;

template <typename, typename> friend class Client;

public:
  struct State {
    enum {
      Down = 0,
      Handshake,
      Up,
      Closing
    };
  };

  CliLink(App *app, ZtString<> server, uint16_t port, ZtString<> path) :
      Base{app, ZuMv(server), port}, m_path{ZuMv(path)} {
    m_key.length(16);
  }

  bool connect() {
    if (ZuUnlikely(!app()->random(m_key))) return false;
    Base::connect();
    return true;
  }

  void connected(const char *alpn, int /* tlsver */) {
    if (!alpn || strcmp(alpn, "http/1.1")) {
      disconnect();
      return;
    }

    scheduleTimeout();
    m_state = State::Handshake;

    ZmRef<IOBuf> buf = new IOBufAlloc{impl()};
    buf << "GET " << m_path << " HTTP/1.1\r\nHost: " << this->server();
    if (port != 443) buf << ':' << unsigned(this->port());
    buf
      << "\r\nConnection: Upgrade\r\nUpgrade: websocket\r\n"
	 "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: "
      << ZtQuote::Base64{m_key} << "\r\n\r\n";
    Base::send_(ZuMv(buf));
  }

  void disconnected() {
    m_state = State::Down;

    cancelTimeout();
  }

  // handle all the following opcodes send/receive:
  //
  // Text Frame (Opcode: 0x1): Used to send textual data, typically encoded
  // as UTF-8.
  // 
  // Binary Frame (Opcode: 0x2): Used to send binary data, such as files or
  // media streams.
  // 
  // Continuation Frame (Opcode: 0x0): Used when a message is fragmented
  // across multiple frames. This frame continues a previous text or binary
  // frame until the message is fully received.
  // 
  // Connection Close Frame (Opcode: 0x8): Used to initiate or confirm the
  // closing of the WebSocket connection. It can optionally contain a status
  // code and reason for closure. After sending or receiving a close frame,
  // no further data can be sent, and the connection will be closed.
  // 
  // Ping Frame (Opcode: 0x9): Used to check if the recipient is still
  // responsive. It can include an optional payload that the recipient must
  // return in the pong response.
  // 
  // Pong Frame (Opcode: 0xA): Sent in response to a ping frame. It can also
  // be sent unsolicited as a heartbeat.

  // FIXME
  // - provide a bufAlloc() that returns an IOBuf with a skipped header
  //   that is large enough to prepend a websockets frame header without
  //   copying/moving data
 
  // FIXME really there are three choices - text, binary, continuation
  void send(ZmRef<ZiIOBuf> buf, bool text = false, bool final = true) {
  }

  // FIXME - need to have configurable ping sending
  // FIXME - need to have configurable pong sending
  // FIXME - need to have configurable pong timeout
  // FIXME - need additional state for graceful shutdown - sending of
  //         close frame that is also acked by close frame

  // FIXME - look at ZvCSV.cc split() for example relevant to parsing
  // HTTP header (zero-copy of unquoted values)


  // URI format
// https://username:password@host:port/path/to/resource1;rkey1=rvalue1;rkey2=rvalue2/resource2;rkey3=rvalue3?qkey1=qvalue1&qkey2=qvalue2#section
// host can be IPv6, e.g. [2001:db8::1]

  int process(Ztls::RxCursor &rx) {
    while (!rx.empty()) {
      auto span = rx.span();
      int n = process_span_(span);
      if (ZuUnlikely(n < 0)) return -1;
      if (!n) return 0;
      rx.advance(n);
    }
    return 1;
  }

  int process_span_(ZuSpan<uint8_t> rcvd) {
    auto state = m_state.load_();
    if (ZuUnlikely(state == State::Down))
      return -1; // disconnect

    if (ZuUnlikely(state == State::Handshake)) {
      cancelTimeout();

      // FIXME
      // - need a stateful http parser with the state stored in the link
      //   along with any trailing unparsed data (partial header key: value)
      // - essentially 
      for (unsigned i = 0; i < length; i++) {

      }

      ZtString<> s(24);
      HMAC hmac(Ztls::MD::SHA1);
      uint8_t sha1[20];
      s << ZtQuote::Base64{m_key};
      hmac.start(ZuCSpan(s));
      hmac.update(ZuCSpan("258EAFA5-E914-47DA-95CA-C5AB0DC85B11"));
      hmac.finish(sha1);
      s.length(0);
      s << ZtQuote::Base64{ZuBSpan{sha1, 20}};



dGhlIHNhbXBsZSBub25jZQ==                   

      // FIXME 
      // - parse/validate upgrade ack
      // - on failure:
      //   impl()->connectFailed(bool transient);
      //   m_state = State::Down;
      //   return -1;

      // FIXME - begin pinging if ping interval configured
 
      m_state = State::Up;

      impl()->connected();

      return length; // FIXME - actual number bytes consumed
    }

    // FIXME - handle all opcodes including continuations, ping/pong, etc.

    auto i = impl()->process(data, length);

    if (ZuUnlikely(i < 0)) m_state = State::Down;
    return i;
  }

private:
  void scheduleTimeout() {
    if (this->app()->timeout())
      this->app()->mx()->add([link = ZmMkRef(impl())]() {
	link->disconnect();
      }, Zm::now(this->app()->timeout()), &m_timer);
  }
  void cancelTimeout() {
    this->app()->mx()->del(&m_timer);
  }

private:
  ZtString<>		m_path;
  ZuArray<16, uint8_t>	m_key;
  ZmScheduler::Timer	m_timer;
  ZmAtomic<int>		m_state = State::Down;

};

// FIXME - actually four timeouts / intervals:
// 1] http upgrade timeout
// 2] ping send interval (optional)
//    (pong timeout should be ping send interval, if configured)
// 3] unsolicited pong send timer (which should be reset if pong
//    is sent early in response to a ping)
// 4] close timeout

// user agent should also be configurable

// defaults for all timeouts are 5s

template <typename App_, typename Link_>
class Client : public Ztls::Client<App_> {
public:
  using App = App_;
  using Link = Link_;
  using Base = Ztls::Client<App>;

  using Base::app;

  void init(ZiMultiplex *mx, const ZvCf *cf) {
    static const char *alpn[] = { "http/1.1", 0 };

    TLS::init(mx, cf->get("thread", true), alpn, cf->get("caPath", false));

    m_reconnFreq = cf->getInt("reconnFreq", 0, 3600, 0);
    m_timeout = cf->getInt("timeout", 0, 3600, 0);
  }

  void final() {
    Base::final();
  }

  unsigned reconnFreq() const { return m_reconnFreq; }
  unsigned timeout() const { return m_timeout; }

private:
  unsigned		m_reconnFreq = 0;
  unsigned		m_timeout = 0;
};

} // Zws

#endif /* Zws_HH */
