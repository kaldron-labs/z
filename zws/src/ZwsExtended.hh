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
using ServerHeaders = ZhttpHeaders(
  "host", "sec-websocket-version", "sec-websocket-protocol");

template <typename Profile>
class Request :
  public Zhttp::MessageTraits<Profile>::template Request<
    Request<Profile>, ServerHeaders, ZuTypeList<>, false, false> {
public:
  Request(const URI &uri, ZuCSpan protocol = {}) :
    m_uri{&uri}, m_protocol{protocol} { }

  template <typename L>
  void operation(L &&l) {
    auto target = Zhttp::splitPathQuery(m_uri->target);
    l(Zhttp::Method::CONNECT, target.path, target.hasQuery,
      [query = target.query](auto &stream) { stream << query; });
  }
  template <typename L>
  void host(L &&l) { l(m_uri->authority()); }
  template <typename L>
  void protocol(L &&l) { l("websocket"); }
  template <typename Key, typename L>
  void header(L &&l) {
    if constexpr (Key{}() == "sec-websocket-version")
      l("13");
    else if constexpr (Key{}() == "sec-websocket-protocol")
      l(m_protocol);
  }

private:
  const URI		*m_uri;
  HandshakeString	m_protocol;
};

template <typename Profile>
class Response :
  public Zhttp::MessageTraits<Profile>::template Response<
    Response<Profile>, ClientHeaders, ZuTypeList<>, false, false> {
public:
  Response(ZuCSpan protocol = {}) : m_protocol{protocol} { }

  unsigned status() const { return 200; }
  bool streamResponse() const { return true; }
  template <typename Key, typename L>
  void header(L &&l) {
    if constexpr (Key{}() == "sec-websocket-protocol") l(m_protocol);
  }

private:
  HandshakeString	m_protocol;
};

template <typename Profile>
class ErrorResponse :
  public Zhttp::MessageTraits<Profile>::template Response<
    ErrorResponse<Profile>, ZuTypeList<>, ZuTypeList<>, false, false> {
public:
  unsigned status() const { return 400; }
};

template <typename Link, typename Profile>
class ClientParser :
  public Zhttp::MessageTraits<Profile>::template ResponseParser<
    ClientParser<Link, Profile>, ClientHeaders> {
  using Message = Zhttp::MessageTraits<Profile>;
  using Base = typename Message::template ResponseParser<
    ClientParser, ClientHeaders>;

public:
  using State = typename Base::State;

  void bind(Link &link, ZuCSpan protocol) {
    Base::reset();
    m_link = &link;
    m_protocol = protocol;
    m_selected.length(0);
    m_state = State::Initial;
    m_status = 0;
    m_protocolSeen = false;
    m_established = false;
    m_invalid = false;
    m_dispatch.init(link, link);
    Base::requestMethod(Zhttp::Method::CONNECT);
  }
  void disable_() { m_dispatch.disable_(); }
  void final_() {
    m_dispatch.disable_();
    m_dispatch.final_();
    m_link = nullptr;
  }

  void operation(Zhttp::Method::T, const Zhttp::RequestTarget &) { }
  void status(unsigned value) { m_status = value; }
  template <typename Key>
  void header(Zhttp::FieldSection::T section, ZuBSpan value) {
    if (section != Zhttp::FieldSection::Final) return;
    if constexpr (Key{}() == "sec-websocket-protocol") {
      if (m_protocolSeen) m_invalid = true;
      m_protocolSeen = true;
      m_selected = value;
    } else if constexpr (Key{}() == "sec-websocket-extensions") {
      m_invalid = true;
    }
  }
  void bodyInfo(Zhttp::BodyType::T type, uint64_t) {
    if (m_invalid || type != Zhttp::BodyType::Streamed ||
	m_status < 200 || m_status >= 300 ||
	((!m_protocol && m_protocolSeen) ||
	 (m_protocol && (!m_protocolSeen ||
	  !subprotocol(m_protocol, m_selected))))) {
      m_invalid = true;
      return;
    }
    Base::stream();
    m_established = true;
    m_link->established_();
  }
  template <typename Rx>
  bool body(Rx &rx) { return Zhttp::bodyDrain(rx); }
  template <typename Rx>
  void streamRx_(Rx &rx) {
    (void)m_dispatch.process(rx);
  }
  void streamPeerEnd_() { m_dispatch.peerEnd(); }
  void streamError_() { m_dispatch.error(); }
  void complete(typename State::T state) { m_state = state; }

  bool established() const { return m_established; }
  bool invalid() const { return m_invalid; }
  ZuCSpan selected() const { return m_selected; }
  typename State::T state() const { return m_state; }

private:
  Zhttp::StreamDispatch<Link, Link>	m_dispatch;
  Link					*m_link = nullptr;
  HandshakeString			m_protocol;
  HandshakeString			m_selected;
  typename State::T			m_state = State::Initial;
  unsigned				m_status = 0;
  bool					m_protocolSeen = false;
  bool					m_established = false;
  bool					m_invalid = false;
};

template <typename Link, typename Profile>
class ServerParser :
  public Zhttp::MessageTraits<Profile>::template RequestParser<
    ServerParser<Link, Profile>, ServerHeaders> {
  using Message = Zhttp::MessageTraits<Profile>;
  using Base = typename Message::template RequestParser<
    ServerParser, ServerHeaders>;

public:
  using State = typename Base::State;

  void bind(Link &link) {
    Base::reset();
    m_link = &link;
    m_target.length(0);
    m_host.length(0);
    m_protocol.length(0);
    m_protocols.length(0);
    m_state = State::Initial;
    m_method = -1;
    m_hostSeen = false;
    m_versionSeen = false;
    m_protocolSeen = false;
    m_version13 = false;
    m_established = false;
    m_invalid = false;
    m_dispatch.init(link, link);
    Base::extendedConnect(Zhttp::Stream{link}.localCap());
  }
  void disable_() { m_dispatch.disable_(); }
  void final_() {
    m_dispatch.disable_();
    m_dispatch.final_();
    m_link = nullptr;
  }

  void operation(
    Zhttp::Method::T method, const Zhttp::RequestTarget &target) {
    m_method = method;
    m_target.length(0);
    m_target << Zhttp::PathQuery{
      target.path, target.query, target.hasQuery};
    m_protocol = target.protocol;
  }
  void status(unsigned) { }
  template <typename Key>
  void header(Zhttp::FieldSection::T section, ZuBSpan value) {
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
  void bodyInfo(Zhttp::BodyType::T type, uint64_t) {
    if (m_invalid || type != Zhttp::BodyType::Streamed ||
	m_method != Zhttp::Method::CONNECT || m_protocol != "websocket" ||
	!m_target || !m_hostSeen || !m_host ||
	!m_versionSeen || !m_version13) {
      m_invalid = true;
      m_link->reject_();
      return;
    }
    HandshakeString selected;
    if (!m_link->accept_(
	  m_host, m_target, m_protocols, selected)) {
      m_invalid = true;
      m_link->reject_();
      return;
    }
    if (selected && !subprotocol(m_protocols, selected)) {
      m_invalid = true;
      m_link->reject_();
      return;
    }
    m_link->respond_(selected);
    Base::stream();
    m_established = true;
    m_link->established_();
  }
  template <typename Rx>
  bool body(Rx &rx) { return Zhttp::bodyDrain(rx); }
  template <typename Rx>
  void streamRx_(Rx &rx) {
    (void)m_dispatch.process(rx);
  }
  void streamPeerEnd_() { m_dispatch.peerEnd(); }
  void streamError_() { m_dispatch.error(); }
  void complete(typename State::T state) { m_state = state; }

  bool established() const { return m_established; }
  bool invalid() const { return m_invalid; }
  typename State::T state() const { return m_state; }

private:
  Zhttp::StreamDispatch<Link, Link>	m_dispatch;
  Link					*m_link = nullptr;
  HandshakeString			m_target;
  HandshakeString			m_host;
  HandshakeString			m_protocol;
  HandshakeString			m_protocols;
  typename State::T			m_state = State::Initial;
  Zhttp::Method::T			m_method = -1;
  bool					m_hostSeen = false;
  bool					m_versionSeen = false;
  bool					m_protocolSeen = false;
  bool					m_version13 = false;
  bool					m_established = false;
  bool					m_invalid = false;
};

} // namespace Extended
} // namespace Zws

#endif /* ZwsExtended_HH */
