//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zrestd_proto_HH
#define zrestd_proto_HH

#include <zlib/ZmList.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/ZhttpServer.hh>

#include "zrestd_auth.hh"
#include "zrestproto_srv.hh"

struct PingParser : public PingParser_<PingParser> {
  using Base = PingParser_<PingParser>;
  using Base::header;

  using Headers = ZhttpHeaders("authorization");

  App *app = nullptr;
  TokenString authorization;
  unsigned authorizationCount = 0;

  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuSpan<uint8_t> value) {
    if constexpr (Key{}() == "authorization") {
      ++authorizationCount;
      if (value.length() <= sizeof("Bearer ") - 1 + JWTMax)
	authorization = value;
    }
  }

  template <typename Link> void complete(Link *, bool);
};

namespace PingResult {
  enum { Unauthorized, OK };
}

using Requests = ZuTypeList<AuthParser, RefreshParser, PingParser>;

struct Parser : public Zrest::MReqParser<Requests> {
  using Reqs = Requests;
  void init(App &);
  bool operation(Zhttp::Method::T method, Zhttp::Target &target) {
    if (!Zrest::MReqParser<Requests>::operation(method, target)) return false;
    u.dispatch([this](auto, auto &request) { request.app = app; });
    return true;
  }
  App *app = nullptr;
};
using Builder = Zrest::MResBuilder<Parser>;

struct ResBuilder_ : public ZmObject, public Builder {
  bool close_ = false;
  bool close() const { return close_; }
};
ZuDerive(ResBuilderQ, (ZmList<ResBuilder_,
  ZmListNode<ResBuilder_, ZmListHeapID<"zrestd.ResBuilder">>>));
using ResBuilder = ResBuilderQ::Node;

extern ZmSemaphore done;

class App {
public:
  using Parser = ::Parser;
  using ResBuilderQ = ::ResBuilderQ;

  App(const Options *options) : m_options{options} { }
  bool init();

  template <typename Link>
  void auth(Link *link, const AuthParser &parser, bool ok) {
    ZmRef<TokenResponse> tokens;
    switch (auth_(parser, ok, tokens)) {
      case AuthResult::Unauthorized:
	send_<AuthUnauthorized>(link, parser, new Unauthorized{});
	break;
      case AuthResult::OK:
	send_<AuthOK>(link, parser, tokens.ptr());
	authOK_();
	break;
      case AuthResult::InternalError:
	send_<AuthInternalError>(link, parser, new InternalError{});
	fail_();
	break;
    }
  }

  template <typename Link>
  void refresh(Link *link, const RefreshParser &parser, bool ok) {
    ZmRef<TokenResponse> tokens;
    switch (refresh_(parser, ok, tokens)) {
      case AuthResult::Unauthorized:
	send_<RefreshUnauthorized>(link, parser, new Unauthorized{});
	break;
      case AuthResult::OK:
	send_<RefreshOK>(link, parser, tokens.ptr());
	refreshOK_();
	break;
      case AuthResult::InternalError:
	send_<RefreshInternalError>(link, parser, new InternalError{});
	fail_();
	break;
    }
  }

  template <typename Link>
  void ping(Link *link, const PingParser &parser, bool ok) {
    if (ping_(parser, ok) == PingResult::Unauthorized) {
      send_<PingUnauthorized>(link, parser, new Unauthorized{});
      return;
    }
    auto pong = new Pong{};
    pong->pong = true;
    send_<PingOK>(link, parser, pong);
    pong_();
  }

  void listening(int transport, unsigned port);
  void listenFailed(int, bool);
  void connected(int) { }
  void disconnected(int) { }
  unsigned errors() const { return m_errors; }
  unsigned processed() const { return m_pong; }
  void transportFailed() { ++m_errors; }

private:
  int auth_(const AuthParser &, bool, ZmRef<TokenResponse> &);
  int refresh_(const RefreshParser &, bool, ZmRef<TokenResponse> &);
  void authOK_();
  void refreshOK_();
  int ping_(const PingParser &, bool);
  void pong_();

  template <typename Response, typename Link, typename Request, typename Object>
  void send_(Link *link, const Request &, Object *object) {
    ZmRef<ResBuilder> response = new ResBuilder{};
    response->close_ = m_options->noKeepalive;
    response->template init<Response, Request>(object);
    link->send(ZuMv(response));
  }

  void event_(ZuCSpan);
  void fail_();
  void signal_(Zhttp::ResponseOutcome::T);

  const Options	*m_options;
  Ztls::Random	m_rng;
  unsigned	m_auth = 0;
  unsigned	m_refresh = 0;
  unsigned	m_pong = 0;
  unsigned	m_errors = 0;
};

template <typename Link>
void AuthParser::complete(Link *link, bool ok) { app->auth(link, *this, ok); }
template <typename Link>
void RefreshParser::complete(Link *link, bool ok) {
  app->refresh(link, *this, ok);
}
template <typename Link>
void PingParser::complete(Link *link, bool ok) { app->ping(link, *this, ok); }

#endif /* zrestd_proto_HH */
