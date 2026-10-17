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

template <typename, typename> class RequestParser;

namespace H1 {

using ClientHeaders = ZhttpHeaders(
  "upgrade", "connection", "sec-websocket-accept",
  "sec-websocket-protocol", "sec-websocket-extensions");
ZhttpHdrCatalogDerive(ZwsAPI ClientHdrCatalog, ClientHeaders);

class ClientParser : public Zhttp::Parser {

public:
  using HdrCatalog = ClientHdrCatalog;
  using Zhttp::Parser::header;

  ClientParser(ZuBSpan key = {}, ZuBSpan protocol = {}) :
    m_key{key}, m_protocol{protocol} { }

  void expected(ZuBSpan key, ZuBSpan protocol = {}) {
    m_key = key;
    m_protocol = protocol;
    m_accept.length(0);
    m_selected.length(0);
    m_complete = false;
    m_http10 = false;
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
  bool bodyInfo(Zhttp::Channel *channel, Zhttp::BodyType::T type, uint64_t) {
    m_complete = true;
    m_http10 = channel->http10();
    if (type != Zhttp::BodyType::None || channel->bodyFramed())
      m_invalid = true;
    return true;
  }

  template <typename Key>
  void header(
      Zhttp::FieldSection::T section, ZuSpan<uint8_t> value) {
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
    return m_complete && !m_invalid && !m_http10 &&
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
  bool			m_complete = false;
  bool			m_http10 = false;
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
  "sec-websocket-version", "sec-websocket-protocol", "authorization",
  "cookie", "origin");
ZhttpHdrCatalogDerive(ZwsAPI ServerHdrCatalog, ServerHeaders);

class ServerParser : public Zhttp::Parser {

public:
  using HdrCatalog = ServerHdrCatalog;
  using Zhttp::Parser::header;

  void reset() {
    m_target.length(0);
    m_host.length(0);
    m_key.length(0);
    m_protocols.length(0);
    m_authorization.length(0);
    m_cookie.length(0);
    m_origin.length(0);
    m_complete = false;
    m_http10 = false;
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

  bool operation(
    Zhttp::Method::T method, Zhttp::Target &target) {
    m_method = method;
    m_target = target.path;
    return true;
  }
  bool bodyInfo(Zhttp::Channel *channel, Zhttp::BodyType::T type, uint64_t) {
    m_complete = true;
    m_http10 = channel->http10();
    if (type != Zhttp::BodyType::None) m_invalid = true;
    return true;
  }

  template <typename Key>
  void header(
      Zhttp::FieldSection::T section, ZuSpan<uint8_t> value) {
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
    } else if constexpr (Key{}() == "authorization") {
      if (m_authorization) m_invalid = true;
      m_authorization = value;
    } else if constexpr (Key{}() == "cookie") {
      if (m_cookie) m_invalid = true;
      m_cookie = value;
    } else if constexpr (Key{}() == "origin") {
      if (m_origin) m_invalid = true;
      m_origin = value;
    }
  }

  bool valid() const {
    return m_complete && !m_invalid && !m_http10 &&
      m_method == Zhttp::Method::GET && m_target && m_hostSeen && m_host &&
      m_upgradeSeen && m_upgrade && m_connectionSeen && m_connection &&
      m_keySeen && validKey(m_key) && m_versionSeen && m_version13;
  }
  ZuBSpan host() const { return m_host; }
  ZuBSpan target() const { return m_target; }
  ZuBSpan key() const { return m_key; }
  ZuBSpan protocols() const { return m_protocols; }
  ZuBSpan authorization() const { return m_authorization; }
  ZuBSpan cookie() const { return m_cookie; }
  ZuBSpan origin() const { return m_origin; }

private:
  template <typename, typename> friend class Zws::RequestParser;

  HandshakeString	m_target;
  HandshakeString	m_host;
  HandshakeString	m_key;
  HandshakeString	m_protocols;
  HandshakeString	m_authorization;
  HandshakeString	m_cookie;
  HandshakeString	m_origin;
  bool			m_complete = false;
  bool			m_http10 = false;
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
  "sec-websocket-protocol", "authorization", "cookie", "origin");
ZhttpHdrCatalogDerive(ZwsAPI RequestHdrCatalog, RequestHeaders);

class Request : public Zhttp::Builder {

public:
  using HdrCatalog = RequestHdrCatalog;
  using Zhttp::Builder::header;

  Request(const URI &uri, ZuBSpan key, ZuBSpan protocol = {},
      ZuBSpan authorization = {}, ZuBSpan cookie = {},
      ZuBSpan origin = {}) :
    m_uri{&uri}, m_key{key}, m_protocol{protocol},
    m_authorization{authorization}, m_cookie{cookie}, m_origin{origin} { }

  ~Request() { if (m_authorization) ZuClear(m_authorization); }

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
    else if constexpr (Key{}() == "sec-websocket-protocol") {
      if (m_protocol) l(m_protocol);
    }
    else if constexpr (Key{}() == "authorization")
      l(m_authorization);
    else if constexpr (Key{}() == "cookie")
      l(m_cookie);
    else if constexpr (Key{}() == "origin")
      l(m_origin);
  }

private:
  const URI		*m_uri;
  HandshakeString	m_key;
  HandshakeString	m_protocol;
  HandshakeString	m_authorization;
  HandshakeString	m_cookie;
  HandshakeString	m_origin;
};

using ResponseHeaders = ZhttpHeaders(
  ("upgrade", ("websocket")),
  ("connection", ("Upgrade")),
  "sec-websocket-accept", "sec-websocket-protocol");
ZhttpHdrCatalogDerive(ZwsAPI ResponseHdrCatalog, ResponseHeaders);

class Response : public Zhttp::Builder {

public:
  using HdrCatalog = ResponseHdrCatalog;
  using Zhttp::Builder::header;

  Response(ZuBSpan accept, ZuBSpan protocol = {}) :
    m_accept{accept}, m_protocol{protocol} { }

  unsigned status() const { return 101; }
  bool streamResponse() const { return true; }
  template <typename Key, typename L>
  void header(L &&l) {
    if constexpr (Key{}() == "sec-websocket-accept")
      l(m_accept);
    else if constexpr (Key{}() == "sec-websocket-protocol") {
      if (m_protocol) l(m_protocol);
    }
  }

private:
  HandshakeString	m_accept;
  HandshakeString	m_protocol;
};

class ErrorResponse : public Zhttp::Builder {

public:
  using HdrCatalog = Zhttp::DefltHdrCatalog;
  unsigned status() const { return 400; }
};

} // namespace H1
} // namespace Zws

#endif /* ZwsH1_HH */
