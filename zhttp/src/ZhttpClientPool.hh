//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z http library - transport-neutral request admission

#ifndef ZhttpClientPool_HH
#define ZhttpClientPool_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <zlib/ZtArray.hh>

#include <zlib/ZhttpClientHub.hh>
#include <zlib/ZhttpMessage.hh>

namespace Zhttp {

// Owner-thread scheduler shared by physical-connection and logical-stream
// pools.  App::admit() starts work; App::cancel() completes queued work.
template <typename App, typename Request>
class Admission {
  using Queue =
    ZtArray<Request *, ZtArrayHeapID<"Zhttp.Admission.Queue">>;

public:
  bool init(App *app, unsigned concurrency, unsigned maxPending) {
    if (!app || !concurrency) return false;
    m_app = app;
    m_concurrency = concurrency;
    m_queue.length(maxPending);
    return true;
  }

  bool submit(Request &request) {
    if (m_stopping) return false;
    if (m_active < m_concurrency) {
      ++m_active;
      m_app->admit(request);
      return true;
    }
    if (m_count >= m_queue.length()) return false;
    m_queue[m_tail] = &request;
    if (++m_tail == m_queue.length()) m_tail = 0;
    ++m_count;
    return true;
  }

  void release() {
    if (!m_active) return;
    --m_active;
    admit_();
  }

  void stop() {
    if (m_stopping) return;
    m_stopping = true;
    while (m_count) m_app->cancel(*pop_());
  }

  unsigned concurrency() const { return m_concurrency; }
  unsigned active() const { return m_active; }
  unsigned pending() const { return m_count; }
  bool stopping() const { return m_stopping; }
  bool idle() const { return !m_active && !m_count; }

private:
  Request *pop_() {
    auto request = m_queue[m_head];
    m_queue[m_head] = nullptr;
    if (++m_head == m_queue.length()) m_head = 0;
    --m_count;
    return request;
  }

  void admit_() {
    while (!m_stopping && m_active < m_concurrency && m_count) {
      auto request = pop_();
      ++m_active;
      m_app->admit(*request);
    }
  }

  App		*m_app = nullptr;
  Queue		m_queue;
  unsigned	m_concurrency = 0;
  unsigned	m_active = 0;
  unsigned	m_head = 0;
  unsigned	m_tail = 0;
  unsigned	m_count = 0;
  bool		m_stopping = false;
};

// One typed pool of normalized HTTP client links.  The pool owns transport
// links and message machinery; Owner owns request policy and attempt results.
template <
  typename Owner_, typename Profile_, typename Attempt_,
  typename Request_, typename ResParser_>
class ClientPool :
  public ClientHub<
    ClientPool<
      Owner_, Profile_, Attempt_,
      Request_, ResParser_>,
    Profile_> {
public:
  using Owner = Owner_;
  using Profile = Profile_;
  using Attempt = Attempt_;
  using Request = Request_;
  using ResParser = ResParser_;
  using Pool = ClientPool;
  using Message = MessageTraits<Profile>;
  using Base = ClientHub<Pool, Profile>;

  struct Link :
    public ClientLink<Pool, Link, Profile_> {
    using Base = ClientLink<Pool, Link, Profile_>;
    using Protocol = typename Profile::Protocol;
    using IO = ClientMessage<
      Owner, Attempt, Link, Profile,
      Request, ResParser>;

    Link(Pool *pool, unsigned id_) :
      Base{pool}, id{id_}, message{pool->owner(), this} { }

    Attempt *request() const { return m_request; }
    bool stopped() const { return m_stopped; }

    void assign(Attempt *request) {
      ++m_generation;
      m_request = request;
      m_complete = false;
      m_sent = false;
      m_stopped = false;
      m_closing = false;
    }
    void start() {
      if (!m_request) return;
      this->txErrorFn(ZiTxErrorFn{[this](ZeException &e) {
	return owner()->poolTxError(*this, m_request, e);
      }});
      owner()->poolConnect(*this, *m_request);
    }
    void sendRequest() {
      if (!m_request) return;
      message.bind(m_request);
      message.reset();
      m_sent = true;
      owner()->poolSend(*this, *m_request, Message::ID);
      auto link = ZmMkRef(this);
      unsigned generation = m_generation;
      pool()->txRun([link = ZuMv(link), generation]() mutable {
	link->message.beginTx();
	link->sendRequestTx_(generation);
      });
    }
    void close() {
      if (m_closing) return;
      m_closing = true;
      auto link = ZmMkRef(this);
      pool()->txRun([link = ZuMv(link)]() mutable {
	link->message.cancelTx();
	auto pool = link->pool();
	pool->rxRun([link = ZuMv(link)]() mutable {
	  link->disconnect();
	});
      });
    }
    void retire() {
      m_request = nullptr;
      if (this->active())
	close();
      else
	notifyStopped_();
    }

    bool reusable() const {
      if constexpr (Message::OneMessagePerLink) return false;
      return m_request && owner()->poolReusable(*m_request);
    }

    void onConnected(const ConnectedInfo &info) {
      if (!m_request || !pool()->accepting()) {
	close();
	return;
      }
      owner()->poolConnected(*this, *m_request, info);
      sendRequest();
    }
    void onDisconnected(bool peer) {
      owner()->poolDisconnected(*this, m_request, peer);
      if (m_request && !m_complete) {
	if constexpr (Message::CloseDelimited) {
	  owner()->poolCloseDelimited(*m_request);
	  message.eof();
	}
	if (m_request && !m_complete) complete(false);
      }
      notifyStopped_();
    }
    void onConnectFailed(bool transient) {
      owner()->poolConnectFailed(*this, m_request, transient);
      if (m_request && !m_complete) complete(false);
    }
    template <typename Rx>
    int process(Rx &rx) {
      return m_request ? message.process(rx) : -1;
    }
    template <
      typename Stream, typename Rx, int ID = Message::ID,
      ZuIfT<ID == Version::H1, int> = 0>
    int process(Stream, Rx &) { return -1; }

    void complete(bool ok) {
      if (!m_request || m_complete) return;
      m_complete = true;
      auto link = ZmMkRef(this);
      unsigned generation = m_generation;
      bool sent = m_sent;
      pool()->txRun([link = ZuMv(link), generation, ok, sent]() mutable {
	if (sent) link->message.cancelTx();
	auto pool = link->pool();
	BodyCommit commit = sent ? link->message.commit() : BodyCommit{};
	pool->rxRun([
	  link = ZuMv(link), commit, generation, ok]() mutable {
	  if (link->m_generation != generation) return;
	  auto request = link->m_request;
	  if (!request) return;
	  link->owner()->poolTxCommitted(*link, *request, commit);
	  bool reuse = ok && link->reusable();
	  link->owner()->poolComplete(*link, *request, ok, reuse);
	});
      });
    }

    Owner *owner() const { return pool()->owner(); }
    Pool *pool() const { return this->app(); }

    unsigned	id = 0;
    unsigned	slot = 0;
    IO		message;

  private:
    void sendRequestTx_(unsigned generation) {
      bool ok = message.send();
      auto link = ZmMkRef(this);
      auto pool = this->pool();
      BodyCommit commit = message.commit();
      pool->rxRun([link = ZuMv(link), commit, generation, ok]() mutable {
	if (link->m_generation != generation) return;
	auto request = link->request();
	if (!request) return;
	if (ok)
	  link->owner()->poolTxCommitted(*link, *request, commit);
	else {
	  link->owner()->poolTxFailed(*link, *request, commit);
	  link->complete(false);
	}
      });
    }

    void notifyStopped_() {
      if (m_stopped) return;
      m_stopped = true;
      pool()->linkStopped(*this);
    }

    Attempt	*m_request = nullptr;
    unsigned	m_generation = 0;
    bool	m_complete = false;
    bool	m_sent = false;
    bool	m_stopped = false;
    bool	m_closing = false;
  };

  using Links =
    ZtArray<ZmRef<Link>, ZtArrayHeapID<"Zhttp.ClientPool.Links">>;

  ClientPool(Owner *owner = nullptr) : m_owner{owner} { }

  Owner *owner() const { return m_owner; }
  void owner(Owner *owner_) { m_owner = owner_; }
  bool accepting() const { return !m_stopping && this->running(); }
  unsigned live() const { return m_live; }
  unsigned linkCount() const { return m_links.length(); }
  const Links &links() const { return m_links; }

  bool cancel(Attempt *request) {
    for (unsigned i = 0; i < m_links.length(); ++i)
      if (m_links[i]->request() == request) {
	m_links[i]->close();
	return true;
      }
    return false;
  }

  ZmRef<Link> open(Attempt *request, unsigned id) {
    if (!accepting() || !request) return {};
    if constexpr (!Message::Multiplexed)
      for (unsigned i = 0; i < m_links.length(); ++i)
	if (m_links[i]->stopped()) {
	  auto link = m_links[i];
	  ++m_live;
	  link->id = id;
	  link->assign(request);
	  link->start();
	  return link;
	}
    ZmRef<Link> link = new Link{this, id};
#ifdef ZmObject_DEBUG
    if constexpr (Message::Multiplexed)
      link->ZmObject::debug();
    else
      link->ZmPolymorph::debug();
#endif
    link->slot = m_links.length();
    m_links.push(link);
    ++m_live;
    link->assign(request);
    link->start();
    return link;
  }

  void connected(Link &link, const ConnectedInfo &info) {
    link.onConnected(info);
  }
  void disconnected(Link &link, bool peer) {
    link.onDisconnected(peer);
  }
  void connectFailed(Link &link, bool transient) {
    link.onConnectFailed(transient);
  }
  template <typename Rx>
  int process(Link &link, Rx &rx) {
    return link.process(rx);
  }

  void linkStopped(Link &link) {
    m_owner->poolStopped(link);
    if (m_live) --m_live;
    if (m_stopping && !m_live)
      if constexpr (!Message::Multiplexed) Base::stop_();
    if constexpr (Message::Multiplexed)
      this->rxRun([this, hold = ZmMkRef(&link)]() mutable {
	unsigned i = hold->slot;
	unsigned n = m_links.length();
	if (i >= n || m_links[i].ptr() != hold.ptr()) return;
	if (i != --n) {
	  m_links[i] = ZuMv(m_links[n]);
	  m_links[i]->slot = i;
	}
	m_links.length(n);
      });
  }

  // TCP/TLS ZmEngine hook.  The native engine retains stop(done);
  // Base::stop_() completes it only after every link is down.  QUIC uses
  // H3_::ClientHub::stop(done) instead and never enters this hook.
  void stop_() {
    m_stopping = true;
    if constexpr (!Message::Multiplexed) {
      if (!m_live) {
	Base::stop_();
	return;
      }
      for (unsigned i = 0; i < m_links.length(); ++i)
	if (m_links[i] && !m_links[i]->stopped())
	  m_links[i]->close();
    }
  }

  void final() {
    m_links.length(0);
    Base::final();
  }

  unsigned reconnFreq() const { return 0; }

private:
  Owner		*m_owner = nullptr;
  Links		m_links;
  unsigned	m_live = 0;
  bool		m_stopping = false;
};

} // namespace Zhttp

#endif /* ZhttpClientPool_HH */
