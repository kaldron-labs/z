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

#include <zlib/ZuUnion.hh>

namespace Zhttp {

// Application message callback contract
//
// Header callbacks precede the first body callback.  Body callbacks receive a
// concrete ZiRxStream-compatible bounded layer on the owning Rx shard; the
// layer and every span it offers are synchronous and callback-scoped.  The
// application must consume or copy offered input before returning.
//
// Body completion follows successful framing validation and consumption of
// all payload.  The terminal result follows body completion, is delivered
// exactly once, and no message callback is made after that result.
//
// Application Tx producers receive a concrete ZiTxStream-compatible body
// stream on the owning Tx shard.  The producer writes entity bytes only;
// libZhttp owns framing and final end-of-stream mapping.

template <
  typename Profile,
  typename Traits = Zhttp::ProfileTraits<Profile>>
struct MessageTraits;

template <
  typename App, typename Request, typename Link, typename Profile,
  typename ReqHeaders, typename RespHeaders, uint64_t RespBodyMax>
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
    bool HasBody, bool Streaming>
  using Builder =
    H2::Builder<Impl, Headers, Trailers, HasBody, Streaming>;
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
    bool HasBody, bool Streaming>
  using Builder =
    H3::Builder<Impl, Headers, Trailers, HasBody, Streaming>;
};

template <typename Profile, typename Traits>
struct MessageTraits :
  public MessageVersion<Traits::HTTPVersion> {
  using ProfileT = Profile;
  using Transport = typename Traits::Transport;
  enum { Multiplexed = Traits::Multiplexed };
};

template <typename Link, typename Builder>
bool sendReq(Link &link, Builder &builder) {
  auto tx = link.transmit(builder);
  if (!builder.request(tx)) return false;
  builder.finish(tx);
  link.finish();
  return true;
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
  typename ReqHeaders_, typename RespHeaders_, uint64_t RespBodyMax_>
class ClientMessage {
public:
  using App = App_;
  using Request = Request_;
  using Link = Link_;
  using Profile = Profile_;
  using ReqHeaders = ReqHeaders_;
  using RespHeaders = RespHeaders_;
  using Message = MessageTraits<Profile>;
  using BodyPolicy = typename App::BodyPolicy;
  using BodyCursor = typename BodyPolicy::Cursor;
  using BodyCursorStorage = ZuUnion<void, BodyCursor>;
  using ReqHeaderKeys = ZuTypeSlice<2, 0, ReqHeaders>;
  enum {
    ReqBody = BodyPolicy::HasBody,
    ReqChunked = BodyPolicy::Streaming
  };
  static constexpr uint64_t RespBodyMax = RespBodyMax_;
  static_assert(
    !ZuTypeIn<ZuStringT<"content-length">, ReqHeaderKeys>{},
    "libZhttp owns request content-length framing");
  static_assert(
    !ZuTypeIn<ZuStringT<"transfer-encoding">, ReqHeaderKeys>{},
    "libZhttp owns request transfer-encoding framing");

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
    template <typename L>
    void protocol(L &&l) {
      app->requestProtocol(*req, ZuFwd<L>(l));
    }
    template <typename Key, typename L>
    void header(L &&l) {
      app->template requestHeader<Key>(*req, ZuFwd<L>(l));
    }
    uint64_t contentLength() {
      return app->requestContentLength(*req);
    }

    App		*app = nullptr;
    Request	*req = nullptr;
  };

  template <bool HasBody, bool Chunked>
  struct Builder_ :
    public Message::template Builder<
      Builder_<HasBody, Chunked>,
      ReqHeaders, ZuTypeList<>, HasBody, Chunked>,
    public ReqOps {
    using Base = typename Message::template Builder<
      Builder_, ReqHeaders, ZuTypeList<>, HasBody, Chunked>;

    Builder_(App *app, Request *request) :
      ReqOps{app, request} { }

    using ReqOps::header;
    using ReqOps::host;
    using ReqOps::operation;
    using ReqOps::protocol;
    using ReqOps::contentLength;
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
    template <typename Rx>
    void body(Rx &rx) {
      app->responseBody(*link, *request, rx);
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
  void reset() {
    m_parser.reset();
    if constexpr (Message::ID != Version::H1)
      if (m_request)
	m_app->requestOperation(
	  *m_request,
	  [this](Method::T method, auto &&, auto &&) {
	    m_parser.requestMethod(method);
	  });
  }

  // Tx-owned request production.  startTx(), every send() turn, and
  // cancelTx() execute on Tx.  A producer must write no more than the supplied
  // batch; libZhttp verifies both progress and the actual framed byte count.
  void startTx(unsigned generation = 0) {
    m_bodyCursor = BodyCursorStorage{};
    m_bodyLength = 0;
    m_bodyProduced = 0;
    m_commit = {};
    m_txGeneration = generation;
    m_hasBody = false;
    if constexpr (ReqBody) {
      m_hasBody = !BodyPolicy::Optional ||
	m_app->requestHasBody(*m_request);
      if (m_hasBody) {
	if constexpr (!ReqChunked)
	  m_bodyLength = m_app->requestContentLength(*m_request);
	m_bodyCursor = m_app->requestBodyCursor(*m_request);
      }
    }
    m_txState = TxState::Active;
  }

  void cancelTx() {
    if (m_txState != TxState::Active) return;
    abandonTx_();
    m_txState = TxState::Cancelled;
  }

  BodyCommit commit() const { return m_commit; }
  unsigned txGeneration() const { return m_txGeneration; }

  int send() { return send(m_app->requestBodyBatch()); }
  int send(unsigned batch) {
    if (m_txState != TxState::Active) return BodySend::Cancelled;
    if constexpr (ReqBody)
      if (m_hasBody) return send_<true, ReqChunked>(batch);
    return send_<false, false>(batch);
  }

private:
  template <bool HasBody, bool Chunked>
  int send_(unsigned batch) {
    Builder_<HasBody, Chunked> builder{m_app, m_request};
    auto tx = m_link->transmit(builder);
    if (!m_commit.headers) {
      if (!builder.request(tx)) return failTx_();
      m_commit.headers = true;
    }
    if constexpr (HasBody) {
      uint64_t remaining = Chunked ?
	uint64_t(-1) : m_bodyLength - m_bodyProduced;
      auto body = builder.body(tx, remaining);
      int state = m_app->requestBody(
	*m_request, m_bodyCursor.template p<1>(), body, batch);
      body.flush();
      uint64_t produced = body.produced();
      m_bodyProduced += produced;
      m_commit.produced = m_bodyProduced;
      m_commit.committed = m_bodyProduced;
      if (!body.valid() || produced > batch) return failTx_();
      switch (state) {
	case BodyProduce::More:
	  if (!produced ||
	      (!Chunked && m_bodyProduced >= m_bodyLength))
	    return failTx_();
	  return BodySend::More;
	case BodyProduce::Done:
	  if (!Chunked && m_bodyProduced != m_bodyLength)
	    return failTx_();
	  break;
	default:
	  return failTx_();
      }
    }
    builder.finish(tx);
    m_link->finish();
    m_commit.final = true;
    m_bodyCursor = BodyCursorStorage{};
    m_txState = TxState::Complete;
    return BodySend::Complete;
  }

public:
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
  struct TxState {
    enum { Idle, Active, Complete, Failed, Cancelled };
  };

  int failTx_() {
    abandonTx_();
    m_txState = TxState::Failed;
    return BodySend::Failed;
  }

  void abandonTx_() {
    m_commit.reset = m_commit.committed;
    if constexpr (ReqBody && !ReqChunked)
      if (m_hasBody && m_bodyProduced < m_bodyLength)
	m_commit.discarded = m_bodyLength - m_bodyProduced;
    m_bodyCursor = BodyCursorStorage{};
  }

  App		*m_app = nullptr;
  Link		*m_link = nullptr;
  Request	*m_request = nullptr;
  Parser	m_parser;
  BodyCursorStorage m_bodyCursor;
  uint64_t	m_bodyLength = 0;
  uint64_t	m_bodyProduced = 0;
  BodyCommit	m_commit;
  unsigned	m_txGeneration = 0;
  int8_t	m_txState = TxState::Idle;
  bool		m_hasBody = false;
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
