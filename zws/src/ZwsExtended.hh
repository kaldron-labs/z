//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// WebSocket Extended CONNECT handshake over HTTP/2 and HTTP/3

#ifndef ZwsExtended_HH
#define ZwsExtended_HH

#ifndef ZwsLib_HH
#include <zlib/ZwsLib.hh>
#endif

#include <zlib/Zhttp.hh>

#include <zlib/ZwsHandshake.hh>
#include <zlib/ZwsURI.hh>

namespace Zws {
namespace Extended {

using ClientHeaders = ZhttpHeaders(
  "sec-websocket-protocol", "sec-websocket-extensions");
ZhttpHdrCatalogDerive(ZwsAPI ClientHdrCatalog, ClientHeaders);
using ServerHeaders = ZhttpHeaders(
  "host", "sec-websocket-version", "sec-websocket-protocol");
ZhttpHdrCatalogDerive(ZwsAPI ServerHdrCatalog, ServerHeaders);

template <typename Profile>
class Request : public Zhttp::Builder {
public:
  using HdrCatalog = ServerHdrCatalog;
  using Zhttp::Builder::header;

  Request(const URI &uri, ZuBSpan protocol = {}) :
    m_uri{&uri}, m_protocol{protocol} { }

  template <typename L>
  void operation(L &&l) {
    l(Zhttp::Method::CONNECT, [this](auto &&emit) {
      emit([this](auto &tx) { tx << m_uri->target; });
    });
  }
  template <typename L>
  void host(L &&l) { l(m_uri->authority()); }
  template <typename L>
  void protocol(L &&l) { l("websocket"); }
  template <typename Key, typename L>
  void header(L &&l) {
    if constexpr (Key{}() == "sec-websocket-version")
      l("13");
    else if constexpr (Key{}() == "sec-websocket-protocol") {
      if (m_protocol) l(m_protocol);
    }
  }
private:
  const URI		*m_uri;
  HandshakeString	m_protocol;
};

template <typename Profile>
class Response : public Zhttp::Builder {
public:
  using HdrCatalog = ClientHdrCatalog;
  using Zhttp::Builder::header;

  Response(ZuBSpan protocol = {}) : m_protocol{protocol} { }

  unsigned status() const { return 200; }
  bool streamResponse() const { return true; }
  template <typename Key, typename L>
  void header(L &&l) {
    if constexpr (Key{}() == "sec-websocket-protocol") {
      if (m_protocol) l(m_protocol);
    }
  }
private:
  HandshakeString	m_protocol;
};

template <typename Profile>
class ErrorResponse : public Zhttp::Builder {
public:
  using HdrCatalog = Zhttp::DefltHdrCatalog;
  unsigned status() const { return 400; }
};

class ClientParser : public Zhttp::Parser {
public:
  using HdrCatalog = ClientHdrCatalog;
  using Zhttp::Parser::header;

  void expected(ZuBSpan protocol) {
    m_protocol = protocol;
    m_selected.length(0);
    m_status = 0;
    m_protocolSeen = false;
    m_valid = false;
    m_invalid = false;
  }
  void status(unsigned value) { m_status = value; }
  template <typename Key>
  void header(Zhttp::FieldSection::T section, ZuSpan<uint8_t> value) {
    if (section != Zhttp::FieldSection::Final) return;
    if constexpr (Key{}() == "sec-websocket-protocol") {
      if (m_protocolSeen) m_invalid = true;
      m_protocolSeen = true;
      m_selected = value;
    } else if constexpr (Key{}() == "sec-websocket-extensions") {
      m_invalid = true;
    }
  }
  bool bodyInfo(Zhttp::Channel *, Zhttp::BodyType::T type, uint64_t) {
    m_valid = !m_invalid && type == Zhttp::BodyType::Streamed &&
      m_status >= 200 && m_status < 300 &&
      ((!m_protocol && !m_protocolSeen) ||
       (m_protocol && m_protocolSeen && subprotocol(m_protocol, m_selected)));
    return true;
  }
  bool valid() const { return m_valid; }
  ZuBSpan selected() const { return m_selected; }

private:
  HandshakeString m_protocol;
  HandshakeString m_selected;
  unsigned m_status = 0;
  bool m_protocolSeen = false;
  bool m_valid = false;
  bool m_invalid = false;
};

class ServerParser : public Zhttp::Parser {
public:
  using HdrCatalog = ServerHdrCatalog;
  using Zhttp::Parser::header;

  void reset() {
    m_target.length(0);
    m_host.length(0);
    m_protocol.length(0);
    m_protocols.length(0);
    m_method = -1;
    m_hostSeen = false;
    m_versionSeen = false;
    m_protocolSeen = false;
    m_version13 = false;
    m_valid = false;
    m_invalid = false;
  }
  bool operation(Zhttp::Method::T method, Zhttp::Target &target) {
    m_method = method;
    m_target.length(0);
    m_target << target.path;
    m_protocol = target.protocol;
    return true;
  }
  template <typename Key>
  void header(Zhttp::FieldSection::T section, ZuSpan<uint8_t> value) {
    if (section != Zhttp::FieldSection::Final) return;
    if constexpr (Key{}() == "host") {
      if (m_hostSeen) m_invalid = true;
      m_hostSeen = true;
      m_host = value;
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
  bool bodyInfo(Zhttp::Channel *, Zhttp::BodyType::T type, uint64_t) {
    m_valid = !m_invalid && type == Zhttp::BodyType::Streamed &&
      m_method == Zhttp::Method::CONNECT && m_protocol == "websocket" &&
      m_target && m_hostSeen && m_host && m_versionSeen && m_version13;
    return true;
  }
  bool valid() const { return m_valid; }
  ZuBSpan host() const { return m_host; }
  ZuBSpan target() const { return m_target; }
  ZuBSpan protocols() const { return m_protocols; }

private:
  HandshakeString m_target;
  HandshakeString m_host;
  HandshakeString m_protocol;
  HandshakeString m_protocols;
  Zhttp::Method::T m_method = -1;
  bool m_hostSeen = false;
  bool m_versionSeen = false;
  bool m_protocolSeen = false;
  bool m_version13 = false;
  bool m_valid = false;
  bool m_invalid = false;
};

} // namespace Extended
} // namespace Zws

#endif /* ZwsExtended_HH */
