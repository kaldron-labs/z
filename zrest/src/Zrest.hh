//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z REST library

#ifndef Zrest_HH
#define Zrest_HH

#include <zlib/ZrestLib.hh>

#include <zlib/ZuDerive.hh>

#include <zlib/ZiRx.hh>

#include <zlib/Ztls.hh>

#include <zlib/ZvCf.hh>
#include <zlib/ZvEngine.hh>

#include <zlib/Zhttp.hh>

namespace Zrest {

template <typename S>
inline void appendAuthority(S &s, ZuCSpan host, uint16_t port)
{
  bool bracket =
    host && host[0] != '[' && host.find([](auto c) { return c == ':'; }) >= 0;
  if (bracket) s << '[' << host << ']';
  else s << host;
  if (port != 443) s << ':' << unsigned(port);
}

// FIXME - 
struct Request : public MsgID {
  ZuTime	time;
  uint16_t	type;
  bool		completed;	// client-side - ackd
};

ZfbStruct(Request,
  (((time)),						(Time)),
  (((type)),						(UInt16)),
  (((completed)),					(Bool)));

// - note that Rx and Tx queues are built-in to ZvLink
// - will need explicit queues for SrvLink since it doesn't use ZvLink
// - with REST, only client-side links make sense; server-side is
//   inherently dynamically provisioned ... 

#if 0
// Tx
  // send message (low level)
  bool send_(Msg *msg, bool more); // true on success
  bool resend_(Msg *msg, bool more); // true on success

  // send gap (can do nothing if not required)
  bool sendGap_(const MxQueue::Span &gap, bool more); // true on success
  bool resendGap_(const MxQueue::Span &gap, bool more); // true on success

  // archive message (low level) (once ackd by receiver(s))
  void archive_(Msg *msg);

  // retrieve message from archive containing key (key may lie within a message)
  // - can optionally call unshift() for subsequent messages <head
  ZmRef<Msg> retrieve_(Key key, Key head);
#endif

namespace Zrest {

// the REST builder layers on the HTTP builder, enriching requests with
// accept-encoding: identity and content-type: application/json as needed
template <
  bool HasBody = false,			// has a body
  typename Context = ZuVoid>		// additional context for callbacks
struct Builder : public Zhttp::Builder<HasBody, Context> {
  using Base = Zhttp::Builder<HasBody, Context>;

  ZuDerive_(Builder, Base)

  using Base::buf;
  template <typename L> using IsCallable = Base::IsCallable<L>;

  // request with parameters
  template <typename Path, typename Query, typename Host, typename Headers>
  void request(
    this auto &&self,
    unsigned method, Path &&path, Query &&query, Host &&host,
    Headers &&headers)
  {
    ZuFwdLike<decltype(self)>(self).Zhttp::request(
      method, ZuFwd<Path>(path), ZuFwd<Query>(query), ZuFwd<Host>(host),
      [headers = ZuFwd<Headers>(headers)](auto &&builder) {
	ZiIOBuf *buf = builder.buf;
	*buf << "accept-encoding: identity\r\n";
	if constexpr (HasBody)
	  *buf << "content-type: application/json\r\n";
	if constexpr (!IsCallable<Headers>{})
	  *buf << ZuFwd<Headers>(headers);
	else
	  ZuFwd<Headers>(headers)(ZuFwdLike<decltype(self)>(self));
      });
  }

  // request without parameters
  template <typename Path, typename Host, typename Headers>
  void request(
    this auto &&self,
    unsigned method, Path &&path, Host &&host, Headers &&headers)
  {
    ZuFwdLike<decltype(self)>(self).Zhttp::request(
      method, ZuFwd<Path>(path), ZuFwd<Host>(host),
      [headers = ZuFwd<Header>(headers)](auto &&builder) {
	ZiIOBuf *buf = builder.buf;
	*buf << "accept-encoding: identity\r\n";
	if constexpr (HasBody)
	  *buf << "content-type: application/json\r\n";
	if constexpr (!IsCallable<Headers>{})
	  *buf << ZuFwd<Headers>(headers);
	else
	  ZuFwd<Headers>(headers)(ZuFwdLike<decltype(self)>(self));
      });
  }

  // response
  template <typename Reason, typename Headers>
  void response(
    this auto &&self, unsigned code, Reason &&reason, Headers &&headers)
  {
    ZuFwdLike<decltype(self)>(self).Zhttp::response(
      method, code, ZuFwd<Reason>(reason),
      [headers = ZuFwd<Header>(headers)](auto &&builder) {
	if constexpr (HasBody)
	  *buf << "content-type: application/json\r\n";
	if constexpr (!IsCallable<Headers>{})
	  *buf << ZuFwd<Headers>(headers);
	else
	  ZuFwd<Headers>(headers)(ZuFwdLike<decltype(self)>(self));
      });
  }
};

// Note: Link must define Builder = Zhttp::Builder<...>

// Link defines RxMsg!Zhttp::RxMsg and Builder!Zhttp::Builder

struct AnyMsgType {
  ZdbAnyTable	*table;
};

template <typename Impl_, typename Pool_>
class Link : public ZvLink<Impl_, Pool_> {
public:
  using Impl = Impl_;
  using Pool = Pool_;
  using Base = ZvLink<Impl, Pool>;

  struct RxMsg : public ZmObject, public Zhttp::RxMsg<...> {
    using Base = Zhttp::RxMsg<...>;
    using Base::Base;
  };

  using Builder = Zhttp::Builder<...>;

  struct Msg;

  struct MsgType : public AnyMsgType {
    virtual void rcvd(Link *, ZmRef<RxMsg>) = 0;
    virtual void process(Link *, Msg *msg) = 0;
    virtual void send_(Link *, Msg *msg) = 0;
    virtual ZmRef<Msg> retrieve(queueID, seqNo) = 0; // Zdb find
  };

  struct Msg : public ZvIOMsg {
    MsgType		*type = nullptr;
    ZmRef<ZdbAnyObject>	object;

    Msg(ZvIOQueueRxTx *owner_, MsgType *type_, ZmRef<ZdbAnyObject> object_) :
      ZvIOMsg(owner), type(type_), object(ZuMv(object_)) { object->pin(); }
    ~Msg() { object->unpin(); }
  };

  Link() : rxMsg(new RxMsg(new IOBufAlloc())) { ... }

#if 0
  // ZvIOQueueRx
  void rcvd(ZvIOMsg *msg) {		// used by protocol rx
    msg->seqNo = m_rxSeqNo++;
    Base::rcvd(msg);
  }
#endif

  // TLS callback
  int process(Ztls::RxStream &rx) {
    while (!rx.empty()) {
      int consumed = 0;
      int64_t n = rx.consume(
	[this, &consumed](ZuBSpan span) -> int64_t {
	  consumed = rxMsg->process(span, [this]() -> bool {
	    ZmRef<RxMsg> rxMsg = new RxMsg(new IOBufAlloc());
	    rxMsg.swap(this->rxMsg);
	    // -----
	    //   HTTP path -> message type parse (server side)
	    //   resolve msg type from header.path (header is Zhttp::Response)
	    // OR
	    //   pending request head -> message type parse (client side)
	    // -----
	    msgType->rcvd(ZuMv(this, ZuMv(rxMsg)); // virtual dispatch
	    return true; // false to disconnect

	  });
	  return consumed;
	},
	[](ZuBSpan) { });
      if (consumed < 0) return -1;
      if (n < 0) return -1;
      if (!n) return 0;
    }
    return 1;
  }

#if 0
  // ZvIOQueueRx
  void process(ZvIOMsg *msg_) {
    auto msg = static_cast<Msg *>(msg_):
    msg->type->process(this, msg);
  }
#endif

  // ZvIOQueueTx
  bool send_(ZvIOMsg *msg_, bool) {
    auto msg = static_cast<Msg *>(msg_):
    msg->type->send_(static_cast<TLS *>(this), msg);
    return true;
  }
  bool resend_(ZvIOMsg *msg, bool more) { return send_(msg, more); }

  // FIXME - client-side recovery
  // iterate over message table, recovering messages to be resent

  // archive_() -> archived() shifts messages from queue

  // ----
  // table
};

template <typename Link, typename T>
struct FooType : public typename Link::MsgType {
  using RxMsg = typename Link::RxMsg;
  using Builder = typename Link::Builder;
  using Msg = typename Link::Msg;

  void rcvd(Link *link, ZmRef<RxMsg> rxMsg) {
    ZdbObjRef<T> o = new ZdbObject<T>{table, ...};

    // ----- HTTP -> object parse
    ZuCSpan path = rxMsg->header.path;
    path.offset(...);// skip endpoint
    // for URI use path:
    ZfURI::new_<T>(o->ptr(), path);
    // or for POST with JSON body:
    ZfJSON::load(o->data(), rxMsg->body.span); // for POSTed body
    // can also use rxMsg->header.key(i) to access headers
    // -----

#if 0
    // locally allocate rx seqNo
    ZmRef<Msg> msg = new Msg(link, this, ZuMv(o));
    link->rcvd(ZuMv(msg)); // enqueues msg
#else
    process(link, o);
#endif

    table->insert(..., [...](ZdbObject<T> *o) {
      o->commit(); // no need to retain buffer
    });
  }

#if 0
  // ZvIOQueueRx
  void process(Link *link, Msg *msg) {
    process(link, static_cast<ZdbObject<T> *>(msg->object.ptr()));
  }
#endif

  void process(Link *link, ZdbObject<T> *o) {
    const T &object = o->data();

    // ----- process normalized object
    // idempotently process object
    // - client-side: update request to ackd with table->update,
    //   if this is a response to a request
    // if server-side, call send(link, ...) to send response
    // -----
  }

  // originate new message
  template <typename Tx>
  void send(Tx *tx, ...) {
    ZdbObjRef<T> o = new ZdbObject<T>{table, ...};

    // ----- originate request/response
    new (o->ptr()) T{...};
    // -----

    // allocate seqNo (can seqNo++ in Link)
    ZmRef<Msg> msg = new Msg(tx, this, o);
    tx->send(ZuMv(msg));
  }

  void send_(TLS *tls, Msg *msg) {
    const T &o = msg->object->data();
    Builder builder{new IOBufAlloc()};

    // ----- build object -> HTTP
    // use Builder with ZfJSON::save(s, o) for body
    builder.response(...); // or request
    // -----

    auto buf = builder.finish();
    // Note: tls->send() is not link->send() (which is our caller)
    tls->send(ZuMv(buf));

    // persist msg
    table->insert(ZuMv(o), [](ZdbObject<T> *o) { o->commit(); });
  }

  // FIXME - recover rx and tx from DB
  {
    table->select<Key>(ZuFwdTuple(LINKID), LIMIT, [](auto row, unsigned count)
  }
};

template <
  typename App,
  typename Link> class Client;

// CRTP - implementation must conform to the following interface:
#if 0
Impl : public CliLink {
  template <typename ...Args>
  void connected(Args &&...args) {
    CliLink::connected(ZuFwd<Args>(args)...);
    if (state() == ZvLinkState::Connected) {
      // send initial request
    }
  }
  void disconnected(bool) {	// optional
    CliLink::disconnected();
    // ...
  }

  using CliLink::header;
  using CliLink::body;
  bool rcvd() { ... } // return false to disconnect
};
#endif

template <
  typename App_,
  typename Impl_,
  typename RxKeys_ = ZuStringTL<>,
  typename TxKeys_ = ZuStringTL<>,
  typename IOBufAlloc_ = Ztls::RxBufAlloc<>,
  typename TxBufAlloc_ = Ztls::TxBufAlloc<>>
class CliLink :
  public Ztls::CliLink<App_, Impl_, IOBufAlloc_, TxBufAlloc_>,
  public ZiRx<
    CliLink<App_, Impl_, RxKeys_, TxKeys_, IOBufAlloc_, TxBufAlloc_>,
    IOBufAlloc_>,
  public ZvLink<CliLink, ZvTxPool<CliLink>>,
  public Zhttp::Receiver<CliLink, Zhttp::Header<RxKeys_>, Zhttp::Body<>>
{
  // ZvIOMsg is ZvIOQueue::Node!ZvIOMsg_, which must be derived
  // from to implement size(), but other than being comprehensible by
  // the Impl::send_() and process() functions, has no contract

  // in practice this will own a discriminated union of
  // ZdbObjRef (rename to ZdbRef), one for each message type

  // two tables for all messages, plus one per message type:
  //   rx (linkID), seqNo (just one row - the last seqNo received)
  //   - rx table is not needed at all for REST
  //   tx (linkID, seqNo), msgType, time, ackd, message payload fields
  //   - this can be defined for all protocols in ZvTxDB
  //   - both these can be defined in Zv using ZfbStruct(..., ...)
  //     despite Zdb depending on Zv

  // (linkID, seqNo) descending -> query and keep going
  // if acks can occur out of order, then ackd needs to be part of index?
  // - need to be able to rapidly query for all unackd, and iterate over them

  // 1] type list of messages
  // for each message
  // message <-> HTTP functions specified as request or response
  // request:
  //   method (fixed)
  //   fixed path (endpoint)
  //   variable path (optional) - URI - may be none
  //   header(s) (optional) - each one a lambda(object)
  //   body (optional) - JSON

  // request receivers will dispatch on endpoint + method

  //
  // response:
  //   code (fixed)
  //   reason (fixed)
  //   header(s) (optional) - each one a lambda(object)
  //   body (optional) - JSON

  // response receivers do not need to dispatch, as
  // a response is tied to a request by sequence

  // headers are too arbitrary
  // - need to present key, value to lambda for parsing
  // - provide built-in support for Authorization: Basic, Bearer and JWT
  // - factor out idempotent in-place decoding of base64 etc. (see ZfJSON)

public:
  using App = App_;
  using Impl = Impl_;
  using RxKeys = RxKeys_;
  using TxKeys = TxKeys_;
  using IOBufAlloc = IOBufAlloc_;
  using TxBufAlloc = TxBufAlloc_;

  using TLS = Ztls::CliLink<App, Impl, IOBufAlloc, TxBufAlloc>;
  using Rx = ZiRx<CliLink, IOBufAlloc>;
  using Link = ZvLink<CliLink, ZvTxPool<CliLink>>;
  using Header = Zhttp::Header<RxKeys>;
  using Body = Zhttp::Body<>;
  using Http = Zhttp::Receiver<CliLink, Header, Body>;

  using TLS::impl;
  using TLS::app;
  using TLS::server;
  using TLS::port;
  using TLS::connect;
  using TLS::disconnect;
  using TLS::send;
  // using TLS::send_;

  using Link::state;
  using Link::send;

  using Http::header;
  using Http::body;
  using Http::process;

template <typename, typename> friend class Client;

  CliLink(App *app, Ztls::Host server, uint16_t port) :
    TLS{app, ZuMv(server), port}
  {
    appendAuthority(m_host, server(), port);
  }

  // link state management
  void connected(Zi::Connected info) {
    if (info.alpn != "http/1.1") {
      this->disconnect();
      return;
    }
    Link::connected();
  }
  void disconnected(bool) {
    Link::disconnected();
  }
  bool up() const {
    switch (state()) {
      case ZvLinkState::Connecting:
      case ZvLinkState::Up:
	return true;
      default:
	return false;
    }
    ZuUnreachable();
  }

  send_(ZvIOMsg *msg, bool more) {
    using Builder = Zrest::Builder<>; // no body, this is the client
    Builder builder{new IOBufAlloc()};
    builder.request(method, path, [](Builder &builder) {
      ZfURI::save<Fields>(builder.buf, object);
    }, host, /* FIXME - headers must come from msg */);
    auto buf = builder.finish();
    TLS::send(ZuMv(buf));
  }
  bool resend_(ZvIOMsg *msg, bool more) {
    // FIXME
  }

  void rcvd() {
    // FIXME - use Http::body.span with JSON load
  }

  void processed() {
    scheduleTimeout();
  }

private:
  void scheduleTimeout() {
    if (this->app()->timeout())
      this->app()->mx()->add(&m_timer, Zm::now(this->app()->timeout()),
	  ZmScheduler::Update,
	  [this](auto &&arm) {
	    return arm([link = ZmMkRef(impl())]() {
	      link->disconnect();
	    });
	  });
  }
  void cancelTimeout() {
    this->app()->mx()->del(&m_timer);
  }

private:
  ZmScheduler::Timer	m_timer;
  ZmAtomic<int>		m_state = ZvLinkState::Down;
};

// FIXME - Client must be a ZvEngine and own a Zdb
template <typename App_, typename Link_>
class Client : public Ztls::Client<App_> {
public:
  using App = App_;
  using Link = Link_;
  using Base = Ztls::Client<App>;

  using Base::app;

  void init(ZiMultiplex *mx, const ZvCf *cf) {
    ZuCSpan alpn[] = { "http/1.1" };

    if (!Base::init(
	  Ztls::ClientParams(
	    mx, cf->get("rxThread", true), cf->get("txThread", true)).alpn(alpn)
	    .caPath(cf->get("caPath", false))))
      ZiLOG(Error, "Zrest", "TLS client initialization failed");

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

template <typename, typename> class Server;

template <
  typename App_,
  typename Impl_,
  typename RxKeys_ = ZuStringTL<>,
  typename TxKeys_ = ZuStringTL<>,
  typename IOBufAlloc_ = Ztls::RxBufAlloc<>,
  typename TxBufAlloc_ = Ztls::TxBufAlloc<>>
class SrvLink :
  public Ztls::SrvLink<App_, Impl_, IOBufAlloc_, TxBufAlloc_>,
  public ZiRx<
    SrvLink<App_, Impl_, RxKeys_, TxKeys_, IOBufAlloc_, TxBufAlloc_>,
    IOBufAlloc_>,
  public Zhttp::Receiver<SrvLink, Zhttp::Header<RxKeys_>, Zhttp::Body<>> {
public:
  using App = App_;
  using Impl = Impl_;
  using RxKeys = RxKeys_;
  using TxKeys = TxKeys_;
  using IOBufAlloc = IOBufAlloc_;
  using TxBufAlloc = TxBufAlloc_;

  using Base = Ztls::SrvLink<App, Impl, IOBufAlloc, TxBufAlloc>;
  using Rx = ZiRx<SrvLink, IOBufAlloc>;

  using Base::impl;
  using Base::app;
  using Base::disconnect;
  using Base::send;
  using Base::send_;

template <typename, typename> friend class Server;

  SrvLink(App *app) : Base(app) { }

  // void connected(Zi::Connected);
  // void disconnected(bool);

  // int process(const uint8_t *data, unsigned len);
};

template <typename App_, typename Link_>
class Server : public Ztls::Server<App_> {
public:
  using App = App_;
  using Link = Link_;
  using Base = Ztls::Server<App>;

  using Base::app;
  using Base::listen;
  using Base::stopListening;

  using Cxn = typename Link::Cxn;

  Cxn *accepted(const ZiCxnInfo &ci) {
    return new Cxn(new Link(this), ci);
  }

  template <typename Server>
  Server(Server &&server, unsigned port) :
    m_localIP{ZuFwd<Server>server}, m_localPort{port} { }

  void start() { listen(); }
  void stop() { stopListening(); }

  ZiIP localIP() const { return m_localIP; }
  unsigned localPort() const { return m_localPort; }

  void done() { m_sem.post(); }
  void wait() { m_sem.wait(); }

private:
  ZmSemaphore	m_sem;
  ZiIP		m_localIP;
  unsigned	m_localPort;
};

} // Zrest

#endif /* Zrest_HH */
