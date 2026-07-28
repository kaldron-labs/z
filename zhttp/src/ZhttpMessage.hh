//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z http library - transport-neutral message session utilities

#ifndef ZhttpMessage_HH
#define ZhttpMessage_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <zlib/ZhttpTransport.hh>

namespace Zhttp {

template <
  typename Profile,
  typename Traits = Zhttp::ProfileTraits<Profile>>
struct MessageTraits;

template <
  typename App, typename Request, typename Link, typename Profile,
  typename ReqHeaders, typename RespHeaders, uint64_t RespBodyMax,
  bool ReqBody = false, bool ReqChunked = false>
class ClientMessage;

} // namespace Zhttp

#ifndef Zhttp_HH
#include <zlib/Zhttp.hh>
#endif

#include <zlib/ZuTL.hh>

namespace Zhttp {

template <int> struct MessageVersion;

template <> struct MessageVersion<Version::H1> {
  enum {
    ID = Version::H1,
    OneMessagePerLink = false,
    CloseDelimited = true
  };

  template <
    typename Impl, bool Request, typename Headers, uint64_t MaxBody>
  using Parser = H1::Parser<Impl, Request, Headers, MaxBody>;
  template <
    typename Impl, typename Headers, typename Trailers,
    bool HasBody, bool Chunked>
  using Builder =
    H1::Builder<Impl, Headers, Trailers, HasBody, Chunked>;
};

template <> struct MessageVersion<Version::H2> {
  enum {
    ID = Version::H2,
    OneMessagePerLink = true,
    CloseDelimited = false
  };

  template <
    typename Impl, bool Request, typename Headers, uint64_t MaxBody>
  using Parser = H2::Parser<Impl, Request, Headers, MaxBody>;
  template <
    typename Impl, typename Headers, typename Trailers,
    bool HasBody, bool>
  using Builder =
    H2::Builder<Impl, Headers, Trailers, HasBody, false>;
};

template <> struct MessageVersion<Version::H3> {
  enum {
    ID = Version::H3,
    OneMessagePerLink = true,
    CloseDelimited = false
  };

  template <
    typename Impl, bool Request, typename Headers, uint64_t MaxBody>
  using Parser = H3::Parser<Impl, Request, Headers, MaxBody>;
  template <
    typename Impl, typename Headers, typename Trailers,
    bool HasBody, bool>
  using Builder =
    H3::Builder<Impl, Headers, Trailers, HasBody, false>;
};

template <typename Profile, typename Traits>
struct MessageTraits :
  public MessageVersion<Traits::HTTPVersion> {
  using ProfileT = Profile;
  using Transport = typename Traits::Transport;
  enum { Multiplexed = Traits::Multiplexed };
};

template <typename Link, typename Builder>
void sendReq(Link &link, Builder &builder) {
  auto tx = link.transmit(builder);
  builder.request(tx);
  builder.finish(tx);
  link.finish();
}

template <typename Link, typename Builder>
void sendResp(Link &link, Builder &builder) {
  auto tx = link.transmit(builder);
  builder.response(tx);
  builder.finish(tx);
  link.finish();
}

// Protocol-neutral client message adapter.  App supplies request intent and
// response handling; HTTP-version-specific builders, parsers, EOF rules, and
// link completion remain library-owned.
template <
  typename App_, typename Request_, typename Link_, typename Profile_,
  typename ReqHeaders_, typename RespHeaders_, uint64_t RespBodyMax_,
  bool ReqBody_, bool ReqChunked_>
class ClientMessage {
public:
  using App = App_;
  using Request = Request_;
  using Link = Link_;
  using Profile = Profile_;
  using ReqHeaders = ReqHeaders_;
  using RespHeaders = RespHeaders_;
  using Message = MessageTraits<Profile>;
  enum {
    ReqBody = ReqBody_,
    ReqChunked = ReqChunked_
  };
  static constexpr uint64_t RespBodyMax = RespBodyMax_;

private:
  struct ReqOps {
    template <typename L>
    void operation(L &&l) {
      app->requestOperation(*req, ZuFwd<L>(l));
    }
    template <typename L>
    void host(L &&l) {
      app->requestHost(*req, ZuFwd<L>(l));
    }
    template <typename Key, typename L>
    void header(L &&l) {
      app->template requestHeader<Key>(*req, ZuFwd<L>(l));
    }

    App		*app = nullptr;
    Request	*req = nullptr;
  };

  struct Builder :
    public Message::template Builder<
      Builder, ReqHeaders, ZuTypeList<>, ReqBody, ReqChunked>,
    public ReqOps {
    using Base = typename Message::template Builder<
      Builder, ReqHeaders, ZuTypeList<>, ReqBody, ReqChunked>;

    Builder(App *app, Request *request) :
      ReqOps{app, request} { }

    using ReqOps::header;
    using ReqOps::host;
    using ReqOps::operation;
  };

  struct RespOps {
    void operation(Method::T, ZuBSpan) { }
    void status(unsigned value) {
      app->responseStatus(*link, *request, value);
    }
    void contentLength(uint64_t value) {
      app->responseContentLength(*link, *request, value);
    }
    void chunked() {
      app->responseChunked(*link, *request);
    }
    void version(ZuBSpan value) {
      app->responseVersion(*link, *request, value);
    }
    template <typename Key>
    void header(ZuBSpan value) {
      app->template responseHeader<Key>(*link, *request, value);
    }
    void body(ZuBSpan value) {
      app->responseBody(*link, *request, value);
    }
    template <typename ParserState>
    void complete(typename ParserState::T state) {
      app->template responseComplete<ParserState>(
	*link, *request, state);
    }

    App		*app = nullptr;
    Link	*link = nullptr;
    Request	*request = nullptr;
  };

  struct Parser :
    public Message::template Parser<
      Parser, false, RespHeaders, RespBodyMax>,
    public RespOps {
    using Base = typename Message::template Parser<
      Parser, false, RespHeaders, RespBodyMax>;
    using State = typename Base::State;

    void operation(Method::T method, ZuBSpan path) {
      RespOps::operation(method, path);
    }
    void complete(typename State::T state) {
      RespOps::template complete<State>(state);
    }

    using RespOps::body;
    using RespOps::chunked;
    using RespOps::contentLength;
    using RespOps::header;
    using RespOps::status;
    using RespOps::version;
  };

public:
  using ParserState = typename Parser::State;

  ClientMessage(App *app = nullptr, Link *link = nullptr) :
    m_app{app}, m_link{link} {
    bind(nullptr);
  }

  void bind(Request *request) {
    m_request = request;
    static_cast<RespOps &>(m_parser) = {m_app, m_link, request};
  }
  void reset() { m_parser.reset(); }

  void send() {
    Builder builder{m_app, m_request};
    ::Zhttp::sendReq(*m_link, builder);
  }

  template <typename Rx>
  int process(Rx &rx) {
    auto state = m_link->receive(m_parser, rx);
    if (state == ParserState::Error) return -1;
    if (state == ParserState::Complete) return 1;
    return m_app->responseFailed(*m_request) ? -1 : 0;
  }

  void eof() {
    if constexpr (Message::CloseDelimited) m_parser.eof();
  }

private:
  App		*m_app = nullptr;
  Link		*m_link = nullptr;
  Request	*m_request = nullptr;
  Parser	m_parser;
};

template <typename Impl, typename Parser_, typename Message_>
struct ServerSession {
  using Parser = Parser_;
  using State = typename Parser::State;
  using Message = Message_;

  Parser	parser;
  bool		complete = false;

  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  void connected(auto &) { }
  void disconnected(auto &, bool) { }

  template <typename Link, typename Rx>
  int process(Link &link, Rx &rx) {
    if (complete) return 1;
    auto state = link.receive(parser, rx);
    if (state == State::Error) return impl()->error(link, parser);
    if (state != State::Complete) return 0;
    int rc = impl()->request(link, parser);
    if constexpr (Message::OneMessagePerLink)
      complete = true;
    else
      parser.reset();
    return rc;
  }

  template <typename Link>
  int error(Link &, Parser &) { return -1; }
  template <typename Link>
  int request(Link &, Parser &) { return 1; }
};

} // namespace Zhttp

#endif /* ZhttpMessage_HH */
