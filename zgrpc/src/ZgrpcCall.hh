//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Directional call state and invalidatable application call handles

#ifndef ZgrpcCall_HH
#define ZgrpcCall_HH

#ifndef ZgrpcLib_HH
#include <zlib/ZgrpcLib.hh>
#endif

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmPlatform.hh>
#include <zlib/ZmScheduler.hh>
#include <zlib/ZiAssert.hh>
#include <zlib/Zgrpc.hh>

namespace Zgrpc {

struct CallRoute {
  ZmScheduler *scheduler = nullptr;
  unsigned thread = 0;

  bool invoked() const { return !scheduler || scheduler->invoked(thread); }
  template <typename L> void run(L &&l) const {
    scheduler->run(ZuFwd<L>(l), thread);
  }
};

// Embed separately in the Rx and Tx owners. No directional state is shared.
class ZgrpcAPI Direction {
public:
  explicit Direction(bool streamed = false) : m_streamed{streamed} { }
  bool message();
  bool end(bool success = true);
  uint64_t count() const { return m_count; }
  bool ended() const { return m_ended; }
private:
  uint64_t		m_count = 0;
  bool			m_streamed;
  bool			m_ended = false;
};

// Immutable route snapshots outlive endpoint owners. Only destination-shard
// code loads and dereferences each raw owner. Endpoint teardown invalidates
// each route on its owner, then drains posted work before releasing storage.
class ZgrpcAPI CallCore {
public:
  CallCore(uint64_t id, uint64_t generation, CallRoute rx, void *rxOwner,
    CallRoute tx, void *txOwner, unsigned maxQueue);
  uint64_t id() const { return m_id; }
  uint64_t generation() const { return m_generation; }
  bool live() const { return m_rxOwner.load_(); }
  bool cancelled() const { return m_cancelled.load_(); }
  void invalidateRx(bool cancelled = false);
  void invalidateTx();

protected:
  const CallRoute &rxRoute() const { return m_rxRoute; }
  const CallRoute &txRoute() const { return m_txRoute; }
  void *rxOwner_() const;
  void *txOwner_() const;
  bool sendPost();
  void sendPosted() { --m_sendPosts; }
  bool claimEnd() { return live() && !m_endClaim.cmpXch(1, 0); }
  bool claimTerminal() { return live() && !m_terminalClaim.cmpXch(1, 0); }
  bool claimCancel() { return live() && !m_cancelClaim.cmpXch(1, 0); }
  void unclaimEnd() { m_endClaim = 0; }
  void unclaimTerminal() { m_terminalClaim = 0; }
  void unclaimCancel() { m_cancelClaim = 0; }

private:
  const uint64_t	m_id;
  const uint64_t	m_generation;
  const CallRoute	m_rxRoute;
  const CallRoute	m_txRoute;
  const unsigned	m_maxQueue;

  alignas(Zm::CacheLineSize)
  ZmAtomic<void *>	m_rxOwner;
  ZmAtomic<unsigned>	m_cancelled = 0;
  ZmAtomic<unsigned>	m_cancelClaim = 0;

  alignas(Zm::CacheLineSize)
  ZmAtomic<void *>	m_txOwner;
  ZmAtomic<unsigned>	m_sendPosts = 0;
  ZmAtomic<unsigned>	m_endClaim = 0;
  ZmAtomic<unsigned>	m_terminalClaim = 0;
};

struct FinishData {
  Status status;
  Metadata trailers;
};

template <typename Token, typename Data>
struct CallAction : public ZmNHeap<CallAction<Token, Data>>, public ZmObject {
friend inline ZuStringT<"Zgrpc.CallAction"> ZmNHeapID(CallAction *);

  ZmRef<Token> token;
  Data data;

  CallAction(Token *token_, Data data_) : token{token_}, data{ZuMv(data_)} { }
};

// Function tables are immutable and live at least as long as their call token.
// Each owner callback rechecks the immutable identity/generation before acting.
// send/end/finish run on Tx; complete selects a unary reply on Rx and transfers
// its owned output to Tx without another terminal-admission claim. Admitted
// production failures follow the owner's failure path before returning false;
// off-shard callers observe ingress admission only.
template <typename Input, typename Output>
struct CallOps {
  bool (*send)(void *, uint64_t, uint64_t, Message<Input>);
  bool (*end)(void *, uint64_t, uint64_t);
  bool (*finish)(void *, uint64_t, uint64_t, FinishData);
  bool (*complete)(void *, uint64_t, uint64_t, Reply<Output>);
  void (*cancel)(void *, uint64_t, uint64_t);
};

template <typename Self, typename Method, bool Server>
class CallToken : public CallCore {
  using Input = ZuIf<Server, typename Method::Response, typename Method::Request>;
  enum { StreamOut = Method::Kind == Kind::ServerStream || Method::Kind == Kind::Duplex };
public:
  using Output = Method::Response;
  using Ops = CallOps<Input, Output>;

  CallToken(uint64_t id, uint64_t generation, CallRoute rx, void *rxOwner,
    CallRoute tx, void *txOwner, unsigned maxQueue, const Ops &ops) :
    CallCore{id, generation, rx, rxOwner, tx, txOwner, maxQueue}, m_ops{&ops} { }

  template <bool S = Server, typename = ZuIfT<!S || StreamOut>>
  bool send(Message<Input> message) {
    if (!live()) return false;
    if (txRoute().invoked()) return send_(ZuMv(message));
    if (!sendPost()) return false;
    try {
      using Action = CallAction<Self, Message<Input>>;
      ZmRef<Action> action = new Action{self(), ZuMv(message)};
      txRoute().run([action = ZuMv(action)]() mutable {
	action->token->sendPosted();
	(void)action->token->send_(ZuMv(action->data));
      });
    } catch (...) { sendPosted(); return false; }
    return true;
  }

  template <bool S = Server, typename = ZuIfT<!S || StreamOut>>
  bool send_(Message<Input> message) {
    auto owner = txOwner_();
    if (!owner || !live()) return false;
    return m_ops->send(owner, id(), generation(), ZuMv(message));
  }

  template <bool S = Server, typename = ZuIfT<!S>>
  bool end() {
    if (!claimEnd()) return false;
    if (txRoute().invoked()) return end_();
    try {
      txRoute().run([token = ZmRef{self()}]() { (void)token->end_(); });
    } catch (...) { unclaimEnd(); return false; }
    return true;
  }

  template <bool S = Server, typename = ZuIfT<S>>
  bool finish(Status status = {}, Metadata trailers = Metadata{}) {
    if (!claimTerminal()) return false;
    if (txRoute().invoked()) return finish_({ZuMv(status), ZuMv(trailers)});
    try {
      using Action = CallAction<Self, FinishData>;
      ZmRef<Action> action = new Action{self(), {ZuMv(status), ZuMv(trailers)}};
      txRoute().run([action = ZuMv(action)]() mutable {
	(void)action->token->finish_(ZuMv(action->data));
      });
    } catch (...) { unclaimTerminal(); return false; }
    return true;
  }

  template <bool S = Server, typename = ZuIfT<S && !StreamOut>>
  bool complete(Reply<Output> reply) {
    if (!claimTerminal()) return false;
    if (rxRoute().invoked()) return complete_(ZuMv(reply));
    try {
      using Action = CallAction<Self, Reply<Output>>;
      ZmRef<Action> action = new Action{self(), ZuMv(reply)};
      rxRoute().run([action = ZuMv(action)]() mutable {
	(void)action->token->complete_(ZuMv(action->data));
      });
    } catch (...) { unclaimTerminal(); return false; }
    return true;
  }

  template <bool S = Server, typename = ZuIfT<S && !StreamOut>>
  bool reply(Message<Output> message, Status status = {}) {
    return complete(Reply<Output>{ZuMv(message), ZuMv(status)});
  }

  bool cancel() {
    if (!claimCancel()) return false;
    if (rxRoute().invoked()) { cancel_(); return true; }
    try {
      rxRoute().run([token = ZmRef{self()}]() { token->cancel_(); });
    } catch (...) { unclaimCancel(); return false; }
    return true;
  }

private:
  auto self() { return static_cast<Self *>(this); }
  bool end_() {
    auto owner = txOwner_();
    return owner && live() && m_ops->end(owner, id(), generation());
  }
  bool finish_(FinishData data) {
    auto owner = txOwner_();
    return owner && live() && m_ops->finish(owner, id(), generation(), ZuMv(data));
  }
  bool complete_(Reply<Output> reply) {
    auto owner = rxOwner_();
    return owner && m_ops->complete(owner, id(), generation(), ZuMv(reply));
  }
  void cancel_() {
    auto owner = rxOwner_();
    if (owner) m_ops->cancel(owner, id(), generation());
  }

  const Ops		*m_ops;
};

template <typename Method>
class ClientCall : public ZmNHeap<ClientCall<Method>>, public ZmObject,
  public CallToken<ClientCall<Method>, Method, false> {
friend inline ZuStringT<"Zgrpc.ClientCall"> ZmNHeapID(ClientCall *);
  using Base = CallToken<ClientCall<Method>, Method, false>;
public:
  using Base::Base;
};

template <typename Method>
class ServerCall : public ZmNHeap<ServerCall<Method>>, public ZmObject,
  public CallToken<ServerCall<Method>, Method, true> {
friend inline ZuStringT<"Zgrpc.ServerCall"> ZmNHeapID(ServerCall *);
  using Base = CallToken<ServerCall<Method>, Method, true>;
public:
  using Base::Base;
};

} // Zgrpc

#endif /* ZgrpcCall_HH */
