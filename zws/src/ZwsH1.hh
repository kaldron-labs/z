//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// RFC 6455 HTTP/1 opening handshake

#ifndef ZwsH1_HH
#define ZwsH1_HH

#ifndef ZwsLib_HH
#include <zlib/ZwsLib.hh>
#endif

#include <zlib/Zhttp.hh>

#include <zlib/ZwsHandshake.hh>
#include <zlib/ZwsURI.hh>

namespace Zws {
namespace H1 {

using ClientHeaders = ZhttpHeaders(
  "upgrade", "connection", "sec-websocket-accept",
  "sec-websocket-protocol", "sec-websocket-extensions");

class ClientParser :
  public Zhttp::Parser,
  public Zhttp::H1ResponseParser<ClientParser, ClientHeaders> {
  using Base = Zhttp::H1ResponseParser<ClientParser, ClientHeaders>;

public:
  using Headers = ClientHeaders;
  using State = Zhttp::H1::ParserState;
  using Zhttp::Parser::header;

  ClientParser(ZuBSpan key = {}, ZuBSpan protocol = {}) :
    m_key{key}, m_protocol{protocol} { }

  void expected(ZuBSpan key, ZuBSpan protocol = {}) {
    Base::reset();
    m_key = key;
    m_protocol = protocol;
    m_accept.length(0);
    m_selected.length(0);
    m_state = State::Initial;
    m_status = 0;
    m_upgradeSeen = false;
    m_connectionSeen = false;
    m_acceptSeen = false;
    m_protocolSeen = false;
    m_upgrade = false;
    m_connection = false;
    m_invalid = false;
  }

  void status(unsigned value) { m_status = value; }
  bool enable1xx() const { return true; }
  void bodyInfo(Zhttp::BodyType::T type, uint64_t) {
    if (type != Zhttp::BodyType::None || Base::bodyFramed())
      m_invalid = true;
  }
  void complete(State::T state) { m_state = state; }

  template <typename Key>
  void header(Zhttp::FieldSection::T section, ZuBSpan value) {
    if (section != Zhttp::FieldSection::Final) return;
    if constexpr (Key{}() == "upgrade") {
      if (m_upgradeSeen) m_invalid = true;
      m_upgradeSeen = true;
      m_upgrade = token(value, "websocket");
    } else if constexpr (Key{}() == "connection") {
      if (m_connectionSeen) m_invalid = true;
      m_connectionSeen = true;
      m_connection = token(value, "upgrade");
    } else if constexpr (Key{}() == "sec-websocket-accept") {
      if (m_acceptSeen) m_invalid = true;
      m_acceptSeen = true;
      m_accept = value;
    } else if constexpr (Key{}() == "sec-websocket-protocol") {
      if (m_protocolSeen) m_invalid = true;
      m_protocolSeen = true;
      m_selected = value;
    } else if constexpr (Key{}() == "sec-websocket-extensions") {
      m_invalid = true;
    }
  }

  bool valid() const {
    return m_state == State::Complete && !m_invalid && !Base::http10() &&
      m_status == 101 && m_upgradeSeen && m_upgrade &&
      m_connectionSeen && m_connection && m_acceptSeen &&
      validAccept(m_accept, m_key) &&
      ((!m_protocol && !m_protocolSeen) ||
       (m_protocol && m_protocolSeen &&
	subprotocol(m_protocol, m_selected)));
  }
  ZuBSpan selected() const { return m_selected; }

private:
  HandshakeString	m_key;
  HandshakeString	m_protocol;
  HandshakeString	m_accept;
  HandshakeString	m_selected;
  State::T		m_state = State::Initial;
  unsigned		m_status = 0;
  bool			m_upgradeSeen = false;
  bool			m_connectionSeen = false;
  bool			m_acceptSeen = false;
  bool			m_protocolSeen = false;
  bool			m_upgrade = false;
  bool			m_connection = false;
  bool			m_invalid = false;
};

using ServerHeaders = ZhttpHeaders(
  "host", "upgrade", "connection", "sec-websocket-key",
  "sec-websocket-version", "sec-websocket-protocol");

class ServerParser :
  public Zhttp::Parser,
  public Zhttp::H1RequestParser<ServerParser, ServerHeaders> {
  using Base = Zhttp::H1RequestParser<ServerParser, ServerHeaders>;

public:
  using Headers = ServerHeaders;
  using State = Zhttp::H1::ParserState;
  using Zhttp::Parser::header;

  void reset() {
    Base::reset();
    m_target.length(0);
    m_host.length(0);
    m_key.length(0);
    m_protocols.length(0);
    m_state = State::Initial;
    m_method = -1;
    m_hostSeen = false;
    m_upgradeSeen = false;
    m_connectionSeen = false;
    m_keySeen = false;
    m_versionSeen = false;
    m_protocolSeen = false;
    m_upgrade = false;
    m_connection = false;
    m_version13 = false;
    m_invalid = false;
  }

  void operation(
    Zhttp::Method::T method, const Zhttp::Target &target) {
    m_method = method;
    m_target = target.pathQuery;
  }
  void bodyInfo(Zhttp::BodyType::T type, uint64_t) {
    if (type != Zhttp::BodyType::None) m_invalid = true;
  }
  void complete(State::T state) { m_state = state; }

  template <typename Key>
  void header(Zhttp::FieldSection::T section, ZuBSpan value) {
    if (section != Zhttp::FieldSection::Final) return;
    if constexpr (Key{}() == "host") {
      if (m_hostSeen) m_invalid = true;
      m_hostSeen = true;
      m_host = value;
    } else if constexpr (Key{}() == "upgrade") {
      if (m_upgradeSeen) m_invalid = true;
      m_upgradeSeen = true;
      m_upgrade = token(value, "websocket");
    } else if constexpr (Key{}() == "connection") {
      if (m_connectionSeen) m_invalid = true;
      m_connectionSeen = true;
      m_connection = token(value, "upgrade");
    } else if constexpr (Key{}() == "sec-websocket-key") {
      if (m_keySeen) m_invalid = true;
      m_keySeen = true;
      m_key = value;
    } else if constexpr (Key{}() == "sec-websocket-version") {
      if (m_versionSeen) m_invalid = true;
      m_versionSeen = true;
      m_version13 = value == "13";
    } else if constexpr (Key{}() == "sec-websocket-protocol") {
      if (m_protocolSeen) m_invalid = true;
      m_protocolSeen = true;
      m_protocols = value;
    }
  }

  bool valid() const {
    return m_state == State::Complete && !m_invalid && !Base::http10() &&
      m_method == Zhttp::Method::GET && m_target && m_hostSeen && m_host &&
      m_upgradeSeen && m_upgrade && m_connectionSeen && m_connection &&
      m_keySeen && validKey(m_key) && m_versionSeen && m_version13;
  }
  ZuBSpan host() const { return m_host; }
  ZuBSpan target() const { return m_target; }
  ZuBSpan key() const { return m_key; }
  ZuBSpan protocols() const { return m_protocols; }

private:
  HandshakeString	m_target;
  HandshakeString	m_host;
  HandshakeString	m_key;
  HandshakeString	m_protocols;
  State::T		m_state = State::Initial;
  Zhttp::Method::T	m_method = -1;
  bool			m_hostSeen = false;
  bool			m_upgradeSeen = false;
  bool			m_connectionSeen = false;
  bool			m_keySeen = false;
  bool			m_versionSeen = false;
  bool			m_protocolSeen = false;
  bool			m_upgrade = false;
  bool			m_connection = false;
  bool			m_version13 = false;
  bool			m_invalid = false;
};

using RequestHeaders = ZhttpHeaders(
  ("upgrade", ("websocket")),
  ("connection", ("Upgrade")),
  "sec-websocket-key", "sec-websocket-version",
  "sec-websocket-protocol");

class Request :
  public Zhttp::Builder,
  public Zhttp::H1Request<Request, RequestHeaders> {
  using Base = Zhttp::H1Request<Request, RequestHeaders>;

public:
  using Headers = RequestHeaders;
  using Zhttp::Builder::header;

  Request(const URI &uri, ZuBSpan key, ZuBSpan protocol = {}) :
    m_uri{&uri}, m_key{key}, m_protocol{protocol} { }

  template <typename L>
  void operation(L &&l) {
    l(Zhttp::Method::GET, [this](auto &&emit) {
      emit([this](auto &tx) { tx << m_uri->target; });
    });
  }
  template <typename L>
  void host(L &&l) { l(m_uri->authority()); }
  template <typename Key, typename L>
  void header(L &&l) {
    if constexpr (Key{}() == "sec-websocket-key")
      l(m_key);
    else if constexpr (Key{}() == "sec-websocket-version")
      l("13");
    else if constexpr (Key{}() == "sec-websocket-protocol")
      l(m_protocol);
  }

private:
  const URI		*m_uri;
  HandshakeString	m_key;
  HandshakeString	m_protocol;
};

using ResponseHeaders = ZhttpHeaders(
  ("upgrade", ("websocket")),
  ("connection", ("Upgrade")),
  "sec-websocket-accept", "sec-websocket-protocol");

class Response :
  public Zhttp::Builder,
  public Zhttp::H1Response<Response, ResponseHeaders> {
  using Base = Zhttp::H1Response<Response, ResponseHeaders>;

public:
  using Headers = ResponseHeaders;
  using Zhttp::Builder::header;

  Response(ZuBSpan accept, ZuBSpan protocol = {}) :
    m_accept{accept}, m_protocol{protocol} { }

  unsigned status() const { return 101; }
  template <typename Key, typename L>
  void header(L &&l) {
    if constexpr (Key{}() == "sec-websocket-accept")
      l(m_accept);
    else if constexpr (Key{}() == "sec-websocket-protocol")
      l(m_protocol);
  }

private:
  HandshakeString	m_accept;
  HandshakeString	m_protocol;
};

class ErrorResponse :
  public Zhttp::Builder,
  public Zhttp::H1Response<ErrorResponse> {
  using Base = Zhttp::H1Response<ErrorResponse>;

public:
  using Headers = ZuTypeList<>;
  unsigned status() const { return 400; }
};

} // namespace H1
} // namespace Zws

#endif /* ZwsH1_HH */
