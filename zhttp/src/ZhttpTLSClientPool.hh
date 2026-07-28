//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z http library - H1/H2 logical-request pool over one TLS coordinator

#ifndef ZhttpTLSClientPool_HH
#define ZhttpTLSClientPool_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <zlib/ZtArray.hh>

#include <zlib/ZhttpH2Engine.hh>
#include <zlib/ZhttpMessage.hh>

namespace Zhttp {

template <
  typename Owner, typename Request, typename ReqHeaders,
  typename RespHeaders, uint64_t RespBodyMax>
class TLSClientPool;

template <
  typename Pool, typename Impl, typename Owner_, typename Request_,
  typename ReqHeaders_, typename RespHeaders_, uint64_t RespBodyMax_,
  typename Profile>
class TLSClientPoolLink_ {
public:
  using Owner = Owner_;
  using Request = Request_;
  using Message = MessageTraits<Profile>;
  using IO = ClientMessage<
    Owner, Request, Impl, Profile,
    ReqHeaders_, RespHeaders_, RespBodyMax_>;

  TLSClientPoolLink_(Pool *pool, Impl *impl, unsigned slot_) :
    m_pool{pool}, m_impl{impl}, m_slot{slot_},
    m_message{pool->owner(), impl} { }

  Request *request() const { return m_request; }
  bool stopped() const { return m_stopped; }
  unsigned slot() const { return m_slot; }
  void slot(unsigned slot_) { m_slot = slot_; }

  void assign(Request *request) {
    ++m_generation;
    m_request = request;
    m_complete = -1;
    m_producerFailed = false;
    m_sent = false;
    m_stopped = false;
    m_closing = false;
    m_message.bind(request);
    m_message.reset();
  }
  void sendRequest() {
    if (!m_request) return;
    m_message.bind(m_request);
    m_message.reset();
    m_sent = true;
    owner()->poolSend(*m_impl, *m_request, Message::ID);
    auto link = ZmMkRef(m_impl);
    unsigned generation = m_generation;
    m_pool->txRun([this, link = ZuMv(link), generation]() mutable {
      m_message.startTx(generation);
      sendRequestTx_(generation);
    });
  }
  void close() {
    if (m_closing) return;
    m_closing = true;
    auto link = ZmMkRef(m_impl);
    m_pool->txRun([this, link = ZuMv(link)]() mutable {
      m_message.cancelTx();
      // Drain producer resumptions queued before cancellation, then return
      // to Rx before disabling the native logical link.
      m_pool->txRun([this, link = ZuMv(link)]() mutable {
	m_pool->rxRun([this, link = ZuMv(link)]() mutable {
	  m_impl->disconnect();
	});
      });
    });
  }
  void retire() {
    m_request = nullptr;
    if (!m_impl->active()) {
      notifyStopped_();
      return;
    }
    if constexpr (Message::OneMessagePerLink) {
      if (m_complete != 1) close();
    } else
      close();
  }

  bool reusable() const {
    if constexpr (Message::OneMessagePerLink) return false;
    return m_request && owner()->poolReusable(*m_request);
  }

  void onConnected(const ConnectedInfo &info) {
    if (!m_request || !m_pool->accepting()) {
      close();
      return;
    }
    m_pool->selected(*m_impl, info.httpVersion);
    owner()->poolConnected(*m_impl, *m_request, info);
    sendRequest();
  }
  void onDisconnected(bool peer) {
    owner()->poolDisconnected(*m_impl, m_request, peer);
    if (m_request && m_complete < 0) {
      if constexpr (Message::CloseDelimited) {
	owner()->poolCloseDelimited(*m_request);
	m_message.eof();
      }
      if (m_request && m_complete < 0) complete(false);
    }
    notifyStopped_();
  }
  void onConnectFailed(bool transient) {
    owner()->poolConnectFailed(*m_impl, m_request, transient);
    if (m_request && m_complete < 0) complete(false);
    notifyStopped_();
  }
  template <typename Rx>
  int process(Rx &rx) {
    return m_request ? m_message.process(rx) : -1;
  }

  void complete(bool ok) {
    if (!m_request || m_complete >= 0) return;
    m_complete = int8_t(ok);
    auto link = ZmMkRef(m_impl);
    unsigned generation = m_generation;
    bool sent = m_sent;
    m_pool->txRun([
      this, link = ZuMv(link), generation, ok, sent]() mutable {
      if (sent) m_message.cancelTx();
      // Drain any producer resumption queued before cancellation, then return
      // the authoritative Tx commitment to Rx for the retry decision.
      m_pool->txRun([
	this, link = ZuMv(link), generation, ok, sent]() mutable {
	BodyCommit commit = sent ? m_message.commit() : BodyCommit{};
	m_pool->rxRun([
	  this, link = ZuMv(link), commit, generation, ok]() mutable {
	  if (m_generation != generation || !m_request) return;
	  if (m_producerFailed)
	    owner()->poolTxFailed(*m_impl, *m_request, commit);
	  else
	    owner()->poolTxCommitted(*m_impl, *m_request, commit);
	  bool reuse = ok && reusable();
	  owner()->poolComplete(*m_impl, *m_request, ok, reuse);
	});
      });
    });
  }

  Owner *owner() const { return m_pool->owner(); }

private:
  void sendRequestTx_(unsigned generation) {
    if (m_message.txGeneration() != generation) return;
    unsigned batch = owner()->requestBodyBatch();
    int state = m_message.send(batch);
    switch (state) {
      case BodySend::More: {
	auto link = ZmMkRef(m_impl);
	m_pool->txRun([this, link = ZuMv(link), generation]() mutable {
	  sendRequestTx_(generation);
	});
	break;
      }
      case BodySend::Complete: {
	auto link = ZmMkRef(m_impl);
	BodyCommit commit = m_message.commit();
	m_pool->rxRun([this, link = ZuMv(link), commit, generation]() mutable {
	  if (m_generation == generation && m_request)
	    owner()->poolTxCommitted(*m_impl, *m_request, commit);
	});
	break;
      }
      case BodySend::Cancelled:
	break;
      default: {
	auto link = ZmMkRef(m_impl);
	m_pool->rxRun([this, link = ZuMv(link), generation]() mutable {
	  if (m_generation != generation) return;
	  m_producerFailed = true;
	  complete(false);
	});
	break;
      }
    }
  }

  void notifyStopped_() {
    if (m_stopped) return;
    m_stopped = true;
    m_pool->linkStopped(*m_impl);
  }

  Pool		*m_pool = nullptr;
  Impl		*m_impl = nullptr;
  Request	*m_request = nullptr;
  unsigned	m_generation = 0;
  unsigned	m_slot = 0;
  int8_t	m_complete = -1;
  bool		m_producerFailed = false;
  bool		m_sent = false;
  bool		m_stopped = false;
  bool		m_closing = false;
  IO		m_message;
};

template <
  typename Pool, typename Owner, typename Request,
  typename ReqHeaders, typename RespHeaders, uint64_t RespBodyMax,
  typename Profile>
class TLSClientPoolLink;

template <
  typename Pool, typename Owner, typename Request,
  typename ReqHeaders, typename RespHeaders, uint64_t RespBodyMax>
class TLSClientPoolLink<
  Pool, Owner, Request, ReqHeaders, RespHeaders, RespBodyMax, H1TLS> :
  public TLS_::ClientH1Logical<
    Pool, TLSClientPoolLink<
      Pool, Owner, Request, ReqHeaders, RespHeaders, RespBodyMax, H1TLS>,
    TLS_::ClientSession<
      Pool,
      TLSClientPoolLink<
	Pool, Owner, Request, ReqHeaders, RespHeaders, RespBodyMax, H1TLS>,
      TLSClientPoolLink<
	Pool, Owner, Request, ReqHeaders, RespHeaders, RespBodyMax, H2TLS>>>,
  public TLSClientPoolLink_<
    Pool,
    TLSClientPoolLink<
      Pool, Owner, Request, ReqHeaders, RespHeaders, RespBodyMax, H1TLS>,
    Owner, Request, ReqHeaders, RespHeaders, RespBodyMax, H1TLS> {
  using Impl = TLSClientPoolLink;
  using Native = TLS_::ClientH1Logical<
    Pool, Impl,
    TLS_::ClientSession<
      Pool, Impl,
      TLSClientPoolLink<
	Pool, Owner, Request, ReqHeaders, RespHeaders, RespBodyMax, H2TLS>>>;
  using Link = TLSClientPoolLink_<
    Pool, Impl, Owner, Request, ReqHeaders, RespHeaders,
    RespBodyMax, H1TLS>;

public:
  using Protocol = TLS;

  TLSClientPoolLink(Pool *pool, unsigned slot) :
    Native{pool}, Link{pool, this, slot} { }

  using Link::assign;
  using Link::close;
  using Link::onConnected;
  using Link::onConnectFailed;
  using Link::onDisconnected;
  using Link::process;
  using Link::request;
  using Link::retire;
  using Link::sendRequest;
  using Link::slot;
  using Link::stopped;

  void complete(bool ok) { Link::complete(ok); }
};

template <
  typename Pool, typename Owner, typename Request,
  typename ReqHeaders, typename RespHeaders, uint64_t RespBodyMax>
class TLSClientPoolLink<
  Pool, Owner, Request, ReqHeaders, RespHeaders, RespBodyMax, H2TLS> :
  public H2_::ClientLogical<
    Pool, TLSClientPoolLink<
      Pool, Owner, Request, ReqHeaders, RespHeaders, RespBodyMax, H2TLS>,
    TLS_::ClientSession<
      Pool,
      TLSClientPoolLink<
	Pool, Owner, Request, ReqHeaders, RespHeaders, RespBodyMax, H1TLS>,
      TLSClientPoolLink<
	Pool, Owner, Request, ReqHeaders, RespHeaders, RespBodyMax, H2TLS>>>,
  public TLSClientPoolLink_<
    Pool,
    TLSClientPoolLink<
      Pool, Owner, Request, ReqHeaders, RespHeaders, RespBodyMax, H2TLS>,
    Owner, Request, ReqHeaders, RespHeaders, RespBodyMax, H2TLS> {
  using Impl = TLSClientPoolLink;
  using Native = H2_::ClientLogical<
    Pool, Impl,
    TLS_::ClientSession<
      Pool,
      TLSClientPoolLink<
	Pool, Owner, Request, ReqHeaders, RespHeaders, RespBodyMax, H1TLS>,
      Impl>>;
  using Link = TLSClientPoolLink_<
    Pool, Impl, Owner, Request, ReqHeaders, RespHeaders,
    RespBodyMax, H2TLS>;

public:
  using Protocol = TLS;

  TLSClientPoolLink(Pool *pool, unsigned slot) :
    Native{pool}, Link{pool, this, slot} { }

  using Link::assign;
  using Link::close;
  using Link::onConnected;
  using Link::onConnectFailed;
  using Link::onDisconnected;
  using Link::process;
  using Link::request;
  using Link::retire;
  using Link::sendRequest;
  using Link::slot;
  using Link::stopped;

  void complete(bool ok) { Link::complete(ok); }
};

template <
  typename Owner_, typename Request_,
  typename ReqHeaders_, typename RespHeaders_, uint64_t RespBodyMax_>
class TLSClientPool :
  public TLS_::ClientEngine<
    TLSClientPool<
      Owner_, Request_, ReqHeaders_, RespHeaders_, RespBodyMax_>,
    TLSClientPoolLink<
      TLSClientPool<
	Owner_, Request_, ReqHeaders_, RespHeaders_, RespBodyMax_>,
      Owner_, Request_, ReqHeaders_, RespHeaders_, RespBodyMax_, H1TLS>,
    TLSClientPoolLink<
      TLSClientPool<
	Owner_, Request_, ReqHeaders_, RespHeaders_, RespBodyMax_>,
      Owner_, Request_, ReqHeaders_, RespHeaders_, RespBodyMax_, H2TLS>> {
public:
  using Owner = Owner_;
  using Request = Request_;
  using ReqHeaders = ReqHeaders_;
  using RespHeaders = RespHeaders_;
  using Pool = TLSClientPool;
  using H1Link = TLSClientPoolLink<
    Pool, Owner, Request, ReqHeaders, RespHeaders, RespBodyMax_, H1TLS>;
  using H2Link = TLSClientPoolLink<
    Pool, Owner, Request, ReqHeaders, RespHeaders, RespBodyMax_, H2TLS>;
  using Base = TLS_::ClientEngine<Pool, H1Link, H2Link>;
  using Base::stop;
  static constexpr uint64_t RespBodyMax = RespBodyMax_;

  struct Pair {
    ZmRef<H1Link>	h1;
    ZmRef<H2Link>	h2;
    int8_t		selected = -1;
  };
  using Pairs =
    ZtArray<Pair, ZtArrayHeapID<"Zhttp.TLSClientPool.Pairs">>;

  TLSClientPool(Owner *owner = nullptr) : m_owner{owner} { }

  Owner *owner() const { return m_owner; }
  bool accepting() const { return !m_stopping && this->running(); }
  unsigned live() const { return m_live; }

  void open(Request *request, unsigned) {
    if (!accepting() || !request) return;
    unsigned slot = m_pairs.length();
    Pair pair{
      .h1 = new H1Link{this, slot},
      .h2 = new H2Link{this, slot}
    };
    pair.h1->assign(request);
    pair.h2->assign(request);
    auto h1 = pair.h1;
    auto h2 = pair.h2;
    m_pairs.push(ZuMv(pair));
    ++m_live;
    Base::connect(h1, h2, request->url.host, request->url.port);
  }

  bool cancel(Request *request) {
    for (auto &pair: m_pairs) {
      switch (pair.selected) {
	case Version::H1:
	  if (pair.h1->request() == request) {
	    pair.h1->close();
	    return true;
	  }
	  break;
	case Version::H2:
	  if (pair.h2->request() == request) {
	    pair.h2->close();
	    return true;
	  }
	  break;
	default:
	  if (pair.h1->request() == request) {
	    pair.h1->close();
	    pair.h2->close();
	    return true;
	  }
	  break;
      }
    }
    return false;
  }

  template <typename Link>
  void selected(Link &link, int8_t version) {
    unsigned slot = link.slot();
    if (slot < m_pairs.length()) m_pairs[slot].selected = version;
  }

  template <typename Link>
  void connected(Link &link, const ConnectedInfo &info) {
    link.onConnected(info);
  }
  template <typename Link>
  void disconnected(Link &link, bool peer) {
    link.onDisconnected(peer);
  }
  template <typename Link>
  void connectFailed(Link &link, bool transient) {
    link.onConnectFailed(transient);
  }
  template <typename Link, typename Rx>
  int process(Link &link, Rx &rx) {
    return link.process(rx);
  }

  template <typename Link>
  void linkStopped(Link &link) {
    if (m_live) --m_live;
    m_owner->poolStopped(link);
    this->rxRun([this, hold = ZmMkRef(&link)]() mutable {
      unsigned i = hold->slot();
      unsigned n = m_pairs.length();
      if (i >= n) return;
      auto &pair = m_pairs[i];
      if constexpr (ZuIsSame<Link, H1Link>{}) {
	if (pair.h1.ptr() != hold.ptr()) return;
      } else {
	if (pair.h2.ptr() != hold.ptr()) return;
      }
      if (i != --n) {
	m_pairs[i] = ZuMv(m_pairs[n]);
	m_pairs[i].h1->slot(i);
	m_pairs[i].h2->slot(i);
      }
      m_pairs.length(n);
    });
  }

  template <typename Done>
  void stop(Done &&done) {
    m_stopping = true;
    Base::stop(ZuFwd<Done>(done));
  }
  void final() {
    ZmAssert(!m_pairs);
    Base::final();
  }

  unsigned reconnFreq() const { return 0; }
  void goaway(uint32_t) { }

private:
  Owner		*m_owner = nullptr;
  Pairs		m_pairs;
  unsigned	m_live = 0;
  bool		m_stopping = false;
};

} // namespace Zhttp

#endif /* ZhttpTLSClientPool_HH */
