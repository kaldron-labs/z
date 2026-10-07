//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// One completion per ordinary JSON-RPC work item

#ifndef ZjrpcCompletion_HH
#define ZjrpcCompletion_HH

#ifndef ZjrpcLib_HH
#include <zlib/ZjrpcLib.hh>
#endif

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmScheduler.hh>

#include <zlib/Zjrpc.hh>

namespace Zjrpc {

namespace CompletionState {
  enum { Open, Closing, Closed };
}

struct CompletionEntry {
  using InvalidateFn = void (*)(void *);
  using CancelFn = void (*)(void *, ZuCSpan);
  using ReleaseFn = void (*)(void *);

  // The retained token owns the immutable ID for the entry's whole lifetime.
  const ID	*id = nullptr;
  uint64_t	generation = 0;
  void		*object = nullptr;
  InvalidateFn	invalidateFn = nullptr;
  CancelFn	cancelFn = nullptr;
  ReleaseFn	releaseFn = nullptr;

  CompletionEntry() = default;
  CompletionEntry(const CompletionEntry &) = delete;
  CompletionEntry &operator =(const CompletionEntry &) = delete;
  CompletionEntry(CompletionEntry &&entry) :
      id{entry.id}, generation{entry.generation},
      object{entry.object}, invalidateFn{entry.invalidateFn},
      cancelFn{entry.cancelFn}, releaseFn{entry.releaseFn} {
    entry.object = nullptr;
  }
  CompletionEntry &operator =(CompletionEntry &&entry) {
    if (this == &entry) return *this;
    release_();
    id = entry.id;
    generation = entry.generation;
    object = entry.object;
    invalidateFn = entry.invalidateFn;
    cancelFn = entry.cancelFn;
    releaseFn = entry.releaseFn;
    entry.object = nullptr;
    return *this;
  }

  template <typename Token>
  CompletionEntry(ZmRef<Token> token) :
      id{&token->id()}, generation{token->generation()},
      object{ZuMv(token).release()},
      invalidateFn{[](void *object_) {
	static_cast<Token *>(object_)->invalidate_();
      }},
      cancelFn{[](void *object_, ZuCSpan reason) {
	static_cast<Token *>(object_)->cancel_(reason);
      }},
      releaseFn{[](void *object_) {
	auto token_ = static_cast<Token *>(object_);
	if (token_->deref()) delete token_;
      }} { }

  ~CompletionEntry() { release_(); }

  void invalidate() { if (object) invalidateFn(object); }
  void cancel(ZuCSpan reason) { if (object) cancelFn(object, reason); }

  void release_() {
    if (!object) return;
    auto object_ = object;
    object = nullptr;
    releaseFn(object_);
  }

};

// Immutable executor snapshot. The application keeps its scheduler alive until
// off-shard token calls have returned. Posted actions retain tokens, never work.
struct CompletionRoute {
  ZmScheduler *scheduler = nullptr;
  unsigned thread = 0;

  bool invoked() const { return !scheduler || scheduler->invoked(thread); }
  template <typename L>
  void run(L &&l) const { scheduler->run(ZuFwd<L>(l), thread); }
};

template <typename Token, typename Res, typename Heap = ZuVoid>
struct CompleteAction_ : public Heap, public ZmObject {
  ZmRef<Token> token;
  Reply<Res> reply;
  CompleteAction_(Token *token_, Reply<Res> reply_) :
    token{token_}, reply{ZuMv(reply_)} { }
};
template <typename Token, typename Res>
using CompleteActionHeap = ZmHeap<"Zjrpc.Complete", CompleteAction_<Token, Res>>;
template <typename Token, typename Res>
using CompleteAction = CompleteAction_<Token, Res, CompleteActionHeap<Token, Res>>;

// Self preserves the concrete token type at application callback boundaries.
// Owner is a non-owning back-pointer; its slot invalidates tokens at teardown.
template <typename Self, typename Req, typename Owner>
class CompletionCore {
  friend struct CompletionEntry;
public:
  CompletionCore(Owner *owner, ID id, uint64_t generation) :
    m_route{owner->completionRoute()}, m_owner{owner},
    m_id{ZuMv(id)}, m_generation{generation} { }

  const ID &id() const { return m_id; }
  uint64_t generation() const { return m_generation; }
  bool live() const { return m_owner.load_(); }
  bool cancelled() const { return m_cancelled.load_(); }

  template <typename Res>
  bool complete(Reply<Res> reply) {
    ZuAssert((ZuTypeIn<Res, GetResponses<Req>>{}));
    if (!live()) return false;
    if (!m_route.invoked()) {
      using Action = CompleteAction<Self, Res>;
      ZmRef<Action> action = new Action{static_cast<Self *>(this), ZuMv(reply)};
      m_route.run([action = ZuMv(action)]() mutable {
	action->token->complete(ZuMv(action->reply));
      });
      return true;
    }
    auto owner = m_owner.load_();
    return owner && owner->template complete<Req, Res>(
      static_cast<Self *>(this), ZuMv(reply));
  }

protected:
  Owner *owner() const { return m_owner.load_(); }
  const CompletionRoute &route() const { return m_route; }

private:
  void invalidate_() { m_owner = nullptr; }
  void cancel_(ZuCSpan reason) {
    auto owner = m_owner.load_();
    if (m_cancelled.load_() || !owner) return;
    m_cancelled = true;
    owner->template cancelled<Req>(static_cast<Self *>(this), reason);
  }

  CompletionRoute m_route;
  ZmAtomic<Owner *> m_owner;
  ID m_id;
  uint64_t m_generation;
  ZmAtomic<unsigned> m_cancelled = 0;
};

template <typename Req, typename Owner, typename Heap = ZuVoid>
class Completion_ : public Heap, public ZmObject,
    public CompletionCore<Completion_<Req, Owner, Heap>, Req, Owner> {
  using Base = CompletionCore<Completion_, Req, Owner>;
public:
  using Base::Base;
};
template <typename Req, typename Owner>
using CompletionHeap = ZmHeap<"Zjrpc.Completion", Completion_<Req, Owner>>;
template <typename Req, typename Owner>
using Completion = Completion_<Req, Owner, CompletionHeap<Req, Owner>>;

// The entry retains the token while callbacks run; taking it clears the slot
// and invalidates the token before any application completion callback.
class CompletionSlot {
public:
  ~CompletionSlot() { drain(); }

  unsigned count() const { return bool(m_entry.object); }
  int state() const { return m_state; }

  bool cancel(const ID &id, ZuCSpan reason = {}) {
    if (!m_entry.object || *m_entry.id != id) return false;
    m_entry.cancel(reason);
    return true;
  }
  void cancelAll(ZuCSpan reason = {}) { m_entry.cancel(reason); }

  bool fail(const ID &id) {
    if (!m_entry.object || *m_entry.id != id) return false;
    auto entry = take();
    return true;
  }

  bool close(unsigned limit) {
    if (m_state == CompletionState::Open) m_state = CompletionState::Closing;
    if (m_entry.object && !limit) return false;
    auto entry = take();
    m_state = CompletionState::Closed;
    return true;
  }
  void drain() { (void)close(1); }

protected:
  bool available(const ID &id) const {
    return m_state == CompletionState::Open && !id.is<void>() && !count();
  }
  uint64_t generation() { return m_generation++; }

  template <typename Token>
  void attach(ZmRef<Token> token) {
    m_entry = CompletionEntry{ZuMv(token)};
  }

  template <typename Token>
  CompletionEntry take(Token *token) {
    if (m_entry.object != token ||
	m_entry.generation != token->generation()) return {};
    return take();
  }

private:
  CompletionEntry take() {
    CompletionEntry entry{ZuMv(m_entry)};
    entry.invalidate();
    return entry;
  }

  CompletionEntry m_entry;
  uint64_t m_generation = 1;
  int m_state = CompletionState::Open;
};

// Derived supplies protocol extensions; Impl supplies the owning responder.
// The generic endpoint uses this directly through its concrete responder.
template <typename Derived, typename Impl,
  template <typename, typename> class Token = Completion>
class CompletionSet : public CompletionSlot {
public:
  auto impl() { return static_cast<Impl *>(this); }
  CompletionRoute completionRoute() { return impl()->completionRoute_(); }
  CompletionRoute completionRoute_() const { return {}; }

  template <typename Req, typename ...Args>
  ZmRef<Token<Req, Derived>> make(const ID &id, Args &&...args) {
    if (!available(id)) return {};
    using T = Token<Req, Derived>;
    ZmRef<T> token = new T{static_cast<Derived *>(this), id,
      generation(), ZuFwd<Args>(args)...};
    attach(token);
    impl()->made(static_cast<Req *>(nullptr), token.ptr());
    return token;
  }

  template <typename Req, typename Res, typename T>
  bool complete(T *token, Reply<Res> reply) {
    return impl()->completion(token, ZuMv(reply));
  }

  template <typename Req, typename T>
  void cancelled(T *token, ZuCSpan reason) {
    try {
      impl()->cancelled(static_cast<Req *>(nullptr), token, reason);
    } catch (...) {
    }
  }

protected:
  template <typename T, typename Res>
  bool complete_(T *token, Reply<Res> reply) {
    auto removed = take(token);
    if (!removed.object) return false;
    try {
      impl()->complete(token->id(), ZuMv(reply));
    } catch (...) {
      return false;
    }
    return true;
  }
};

} // Zjrpc

#endif /* ZjrpcCompletion_HH */
