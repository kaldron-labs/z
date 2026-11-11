//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// JSON-RPC outbound correlation

#ifndef ZjrpcPending_HH
#define ZjrpcPending_HH

#ifndef ZjrpcLib_HH
#include <zlib/ZjrpcLib.hh>
#endif

#include <zlib/ZmSingleton.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmNoLock.hh>
#include <zlib/ZmPlatform.hh>

#include <zlib/ZtArray.hh>

#include <zlib/ZiIOBuf.hh>
#include <zlib/ZuVStream.hh>

#include <zlib/Zjrpc.hh>
#include <zlib/ZjrpcIO.hh>

namespace Zjrpc {

// The aggregate callback may retain the original response buffer/tree.
struct BatchReply {
  ZmRef<ZiIOBuf> body;
  Parsed parsed;
  const ZfJSON::NodeArray &entries() const {
    return parsed.value()->template data<ZfJSON::AnyNode::Array>();
  }
};

struct BatchTag { };

// Applications may replace result decoding without replacing correlation.
struct ReplyDecode {
  template <typename Req, typename Call>
  static bool process(Call &call, const ZfJSON::AnyNode *node) {
    // JSON-RPC has no status-code discriminator inside a result. Applications
    // with several result alternatives supply their own metadata decoder.
    ZuAssert(GetResponses<Req>::N == 1);
    using Res = ZuType<0, GetResponses<Req>>;
    if constexpr (ZuIsSame<void, typename Res::Body>{}) {
      call.process(Reply<Res>{});
    } else {
      auto handler = ZfJSON::handler<typename Res::Body>(node);
      if (!handler.valid) return false;
      call.process(Reply<Res>{handler.ctor()});
    }
    return true;
  }
};

struct PendingEntry {
  using ProcessFn = bool (*)(void *, const ZfJSON::AnyNode *);
  using ErrorFn = void (*)(void *, const Error &);
  using FailedFn = void (*)(void *);
  using ReleaseFn = void (*)(void *);
  using BatchFn = void (*)(void *, BatchReply);

  ID		id;
  void		*object = nullptr;
  ProcessFn	processFn = nullptr;
  ErrorFn	errorFn = nullptr;
  FailedFn	failedFn = nullptr;
  ReleaseFn	releaseFn = nullptr;
  BatchFn	batchFn = nullptr;

  PendingEntry() = default;
  PendingEntry(const PendingEntry &) = delete;
  PendingEntry &operator =(const PendingEntry &) = delete;
  PendingEntry(PendingEntry &&entry) :
      id{ZuMv(entry.id)}, object{entry.object}, processFn{entry.processFn},
      errorFn{entry.errorFn}, failedFn{entry.failedFn},
      releaseFn{entry.releaseFn}, batchFn{entry.batchFn} {
    entry.object = nullptr;
  }
  PendingEntry &operator =(PendingEntry &&entry) {
    if (this == &entry) return *this;
    release_();
    id = ZuMv(entry.id);
    object = entry.object;
    processFn = entry.processFn;
    errorFn = entry.errorFn;
    failedFn = entry.failedFn;
    releaseFn = entry.releaseFn;
    batchFn = entry.batchFn;
    entry.object = nullptr;
    return *this;
  }

  template <typename Req, typename Decode, typename Call>
  PendingEntry(Req *, Decode *, ID id_, ZmRef<Call> call) :
      id{ZuMv(id_)}, object{ZuMv(call).release()},
      processFn{[](void *object_, const ZfJSON::AnyNode *node) {
	return Decode::template process<Req>(*static_cast<Call *>(object_), node);
      }},
      errorFn{error_<Call>}, failedFn{failed_<Call>},
      releaseFn{release_<Call>} { }

  template <typename Call>
  PendingEntry(ID id_, ZmRef<Call> call) :
      id{ZuMv(id_)}, object{ZuMv(call).release()},
      processFn{[](void *object_, const ZfJSON::AnyNode *) {
	static_cast<Call *>(object_)->process();
	return true;
      }},
      errorFn{error_<Call>}, failedFn{failed_<Call>},
      releaseFn{release_<Call>} { }

  template <typename Call>
  PendingEntry(ID id_, ZmRef<Call> call, BatchTag) :
      id{ZuMv(id_)}, object{ZuMv(call).release()},
      processFn{[](void *object_, const ZfJSON::AnyNode *) {
	return static_cast<Call *>(object_)->received();
      }}, errorFn{error_<Call>}, failedFn{failed_<Call>},
      releaseFn{release_<Call>},
      batchFn{[](void *object_, BatchReply reply) {
	static_cast<Call *>(object_)->process(ZuMv(reply));
      }} { }

  ~PendingEntry() { release_(); }

  bool process(const ZfJSON::AnyNode *node) {
    return object && processFn(object, node);
  }
  void error(const Error &error_) {
    if (object) errorFn(object, error_);
  }
  void failed() {
    if (object) failedFn(object);
  }

  template <typename Call>
  static void error_(void *object, const Error &error) {
    static_cast<Call *>(object)->failed(error);
  }
  template <typename Call>
  static void failed_(void *object) {
    static_cast<Call *>(object)->failed();
  }
  template <typename Call>
  static void release_(void *object) {
    auto call = static_cast<Call *>(object);
    if (call->deref()) delete call;
  }

  void release_() {
    if (!object) return;
    auto object_ = object;
    object = nullptr;
    releaseFn(object_);
  }

};

inline const ID &PendingEntry_KeyAxor(const PendingEntry &entry) {
  return entry.id;
}

ZmHashDerive(PendingHash, PendingEntry,
  (ZmHashNode<PendingEntry,
    ZmHashKey<PendingEntry_KeyAxor,
	ZmHashLock<ZmNoLock,
	  ZmHashHeapID<"Zjrpc.Pending">>>>));

struct PendingTable : public ZmNHeap<PendingTable>, public PendingHash {
friend inline ZuStringT<"Zjrpc.Pending.Table"> ZmNHeapID(PendingTable *);

  ZuDerive_(PendingTable, PendingHash)
};

class PendingCalls {
public:
  enum { Open, Closing, Closed };

  PendingCalls(unsigned maxPending = Default::MaxPending) :
    m_maxPending{maxPending} { }

  unsigned count() const { return m_hash->count_(); }
  int state() const { return m_state; }
  bool contains(const ID &id) { return bool(m_hash->findPtr(id)); }

  bool init(unsigned maxPending) {
    if (!maxPending || m_state == Closing || m_hash->count_()) return false;
    m_state = Open;
    m_maxPending = maxPending;
    return true;
  }

  template <typename Req, typename Decode = ReplyDecode, typename Call>
  bool add(ID id, ZmRef<Call> call) {
    if (m_state != Open || id.is<void>() || !call ||
	m_hash->count_() >= m_maxPending ||
	m_hash->findPtr(id)) return false;
    m_hash->add(PendingEntry{
      static_cast<Req *>(nullptr), static_cast<Decode *>(nullptr),
      ZuMv(id), ZuMv(call)});
    return true;
  }

  template <typename Call>
  bool add(ID id, ZmRef<Call> call) {
    if (m_state != Open || id.is<void>() || !call ||
	m_hash->count_() >= m_maxPending ||
	m_hash->findPtr(id)) return false;
    m_hash->add(PendingEntry{ZuMv(id), ZuMv(call)});
    return true;
  }

  template <typename Call>
  bool addBatch(ID id, ZmRef<Call> call) {
    if (m_state != Open || id.is<void>() || !call ||
	m_hash->count_() >= m_maxPending || m_hash->findPtr(id)) return false;
    m_hash->add(PendingEntry{ZuMv(id), ZuMv(call), BatchTag{}});
    return true;
  }
  void drop(const ID &id, const void *object) {
    auto entry = m_hash->findPtr(id);
    if (entry && entry->object == object) (void)m_hash->delNode(entry);
  }

  bool receive(const Envelope &envelope, PendingHash::NodeMvRef *batch = nullptr) {
    if (envelope.kind != MessageKind::Result &&
	envelope.kind != MessageKind::Error) return true;
    auto entry = m_hash->del(envelope.id());
    if (!entry) return true;
    try {
      if (entry->batchFn) {
	if (!batch || (*batch && ((*batch)->object != entry->object ||
	    (*batch)->batchFn != entry->batchFn))) {
	  entry->failed();
	  return false;
	}
	if (!entry->process(nullptr)) { entry->failed(); return false; }
	if (!*batch) *batch = ZuMv(entry);
	return true;
      }
      if (envelope.kind == MessageKind::Error) {
	entry->error(loadError(raw(envelope.error())));
	return true;
      }
      if (entry->process(raw(envelope.result()))) return true;
      entry->failed();
    } catch (...) {
      return false;
    }
    return false;
  }

  bool fail(const ID &id) {
    auto entry = m_hash->del(id);
    if (!entry) return false;
    try {
      entry->failed();
    } catch (...) {
    }
    return true;
  }

  bool close(unsigned limit) {
    if (m_state == Open) m_state = Closing;
    unsigned visited = 0;
    while (visited < limit) {
      auto entry = take_();
      if (!entry) {
	m_state = Closed;
	return true;
      }
      ++visited;
      try {
	entry->failed();
      } catch (...) {
      }
    }
    if (m_hash->count_()) return false;
    m_state = Closed;
    return true;
  }

private:
  PendingHash::NodeMvRef take_() {
    auto i = m_hash->iter();
    if (!i()) return decltype(i.del()){};
    return i.del();
  }

  ZmRef<PendingTable>	m_hash = new PendingTable{};
  unsigned	m_maxPending;
  int		m_state = Open;
};

template <typename Req>
struct RequestMessage {
  using Request = Req;
  using Headers = Req::Headers;
  ID id;
  ObjectValue<typename Req::Object> object;
  template <typename S>
  void write(S &out) const {
    if (id.template is<void>()) {
      if constexpr (ZuIsBase<typename Req::Object, ZmObject>{})
	saveNotification(out, typename Req::Method{}(), *object);
      else
	saveNotification(out, typename Req::Method{}(), object);
      return;
    }
    if constexpr (ZuIsBase<typename Req::Object, ZmObject>{})
      saveRequest(out, id, typename Req::Method{}(), *object);
    else
      saveRequest(out, id, typename Req::Method{}(), object);
  }
};

// id is an actual member ID used only by an HTTP reply route, never a batch wire ID.
struct BatchWire {
  ID id;
  ZmFn<void(ZuVStream &), ZmFnHeapID<"Zjrpc.Batch.Request">> model;
  unsigned max;

  template <typename M>
  BatchWire(ID id_, M message, unsigned max_) : id{ZuMv(id_)},
    model{[message = ZuMv(message)](ZuVStream &out) { message.write(out); }}, max{max_} { }
  template <typename S> void write(S &out) const {
    writeOwned(out, max, [this](auto &wire) {
      ZuVStream stream{wire};
      model(stream);
    }, 0);
  }
};

struct MessageFmt {
  template <typename O, typename Facet>
  struct Handler {
    template <template <typename> class Filter, typename S>
    static void save(S &out, const O &message) { message.write(out); }
  };
};

template <typename Catalog, template <typename> class Request = RequestMessage>
class ClientMessage {
  using Messages = ZuTypeConcat<ZuTypeList<BatchWire>, ZuTypeMap<Request, Catalog>>;
  using Data = ZuTypeApply<ZuUnion, ZuTypeConcat<ZuTypeList<void>, Messages>>;
  friend MessageFmt ZfJSON_Fmt(ClientMessage *);
public:
  ClientMessage() = default;
  template <typename M> ClientMessage(M message) : m_data{ZuMv(message)} { }
  template <typename L>
  decltype(auto) dispatch(L &&l) { return m_data.dispatch(ZuFwd<L>(l)); }
  template <typename L>
  decltype(auto) cdispatch(L &&l) const { return m_data.cdispatch(ZuFwd<L>(l)); }
  template <typename S>
  void write(S &out) const {
    cdispatch([&out](auto, const auto &message) { message.write(out); });
  }
  const ID &id() const {
    return *cdispatch([](auto, const auto &message) { return &message.id; });
  }
  ID &id() {
    return *dispatch([](auto, auto &message) { return &message.id; });
  }
  bool notification() const { return id().template is<void>(); }
  bool batch() const { return m_data.template is<BatchWire>(); }
private:
  Data m_data;
};

template <typename Catalog, template <typename> class Request = RequestMessage>
class Batch {
public:
  using Message = ClientMessage<Catalog, Request>;
  using Items = ZtArray<Message, ZtArrayHeapID<"Zjrpc.Batch.Items">>;

  template <typename Req, typename ...Args>
  bool request(ID id, ObjectValue<typename Req::Object> object, Args &&...args) {
    ZuAssert((ZuTypeIn<Req, Catalog>{}));
    if (id.template is<void>()) return false;
    m_items.push(Request<Req>{ZuMv(id), ZuMv(object), ZuFwd<Args>(args)...});
    return true;
  }
  template <typename Req, typename ...Args>
  void notify(ObjectValue<typename Req::Object> object, Args &&...args) {
    ZuAssert((ZuTypeIn<Req, Catalog>{}));
    m_items.push(Request<Req>{{}, ZuMv(object), ZuFwd<Args>(args)...});
  }
  const Items &items() const { return m_items; }
  Items &items() { return m_items; }
  unsigned count() const { return m_items.length(); }
  unsigned calls() const {
    unsigned n = 0;
    for (const auto &item : m_items) if (!item.notification()) ++n;
    return n;
  }
  ZmRef<ZiIOBuf> frame(unsigned maxBytes) const {
    ZmRef<ZiIOBuf> body = batchBufAllocator()->allocBuf();
    BufOutput out{*body, maxBytes};
    write(out);
    return out ? ZuMv(body) : ZmRef<ZiIOBuf>{};
  }
  template <typename S>
  void write(S &out) const {
    using Codec = ZfJSON::AsArray<ZfFieldTC::UDT>::Handler<Items, ZuFacet::JSON>;
    Codec::template save<ZfFieldFilter::Save>(out, m_items);
  }
private:
  Items m_items;
};

template <typename Req, typename Call>
struct CallAction : public ZmNHeap<CallAction<Req, Call>>, public ZmObject {
friend inline ZuStringT<"Zjrpc.Call"> ZmNHeapID(CallAction *);

  ObjectValue<typename Req::Object> object;
  ZmRef<Call> call;
  CallAction(ObjectValue<typename Req::Object> object_, ZmRef<Call> call_) :
    object{ZuMv(object_)}, call{ZuMv(call_)} { }
};

template <typename Req>
struct NotifyAction : public ZmNHeap<NotifyAction<Req>>, public ZmObject {
friend inline ZuStringT<"Zjrpc.Notify"> ZmNHeapID(NotifyAction *);

  ObjectValue<typename Req::Object> object;
  explicit NotifyAction(ObjectValue<typename Req::Object> object_) : object{ZuMv(object_)} { }
};

template <typename B>
struct BatchAction : public ZmNHeap<BatchAction<B>>, public ZmObject {
friend inline ZuStringT<"Zjrpc.Batch.Notify"> ZmNHeapID(BatchAction *);

  B batch;
  explicit BatchAction(B batch_) : batch{ZuMv(batch_)} { }
};

// Derived::continue_ must enqueue an owner turn, even on the owner thread.
// Credit release precedes the admitted body's execution; its ready notification
// must run after that body returns, rather than invoke completion inline.
template <typename Derived>
class BatchCaller {
  template <typename, typename> friend class Caller;
public:
  using ReadyFn = ZmFn<void(), ZmFnHeapID<"Zjrpc.Calls.Ready">>;

  template <typename B> bool prepareBatch(B &) { return true; }

  bool response(const Envelope &envelope, PendingHash::NodeMvRef *batch = nullptr) {
    return m_pending.receive(envelope, batch);
  }

  template <typename B, typename Call>
  bool callBatch(B batch, ZmRef<Call> call) {
    unsigned n = batch.count();
    if (!derived()->up() || !call || !n || !batch.calls() ||
	n >= derived()->limits().maxPending) return false;
    using Action = BatchCall<B, Call>;
    ZmRef<Action> action = new Action{this, ZuMv(batch), ZuMv(call)};
    if (derived()->invoked()) { action->start(); return true; }
    if (!admitCalls(n)) return false;
    bool posted = this->sendRun([this, n, action = ZuMv(action)]() mutable {
      releaseCalls(n);
      action->start();
    });
    if (!posted) releaseCalls(n);
    return posted;
  }

  template <typename B>
  bool notifyBatch(B batch) {
    unsigned n = batch.count();
    if (!derived()->up() || !n || batch.calls() ||
	n >= derived()->limits().maxPending) return false;
    if (derived()->invoked()) return notifyBatch_(batch);
    if (!admitCalls(n)) return false;
    ZmRef<BatchAction<B>> action = new BatchAction<B>{ZuMv(batch)};
    bool posted = this->sendRun([this, n, action = ZuMv(action)]() mutable {
      releaseCalls(n);
      if (derived()->up() && !notifyBatch_(action->batch)) derived()->fail_();
    });
    if (!posted) releaseCalls(n);
    return posted;
  }


protected:
  template <typename L>
  bool sendRun(L &&l) {
    if (derived()->invoked()) return derived()->ownerRun(ZuFwd<L>(l));
    if (++m_sendPosts > derived()->limits().maxQueue) {
      --m_sendPosts;
      return false;
    }
    bool posted = derived()->ownerRun([this, l = ZuFwd<L>(l)]() mutable {
      --m_sendPosts;
      l();
    });
    if (!posted) --m_sendPosts;
    return posted;
  }

  PendingCalls &pendingCalls() { return m_pending; }
  bool admitCalls(unsigned n = 1) {
    uint64_t ingress = m_ingress.load_();
    for (;;) {
      if ((ingress & Closed) || !derived()->up() ||
	  (ingress & CountMask) + n > derived()->limits().maxPending) return false;
      uint64_t prev = m_ingress.cmpXch(ingress + n, ingress);
      if (prev == ingress) return true;
      ingress = prev;
    }
  }
  void releaseCalls(unsigned n = 1) {
    uint64_t ingress = m_ingress -= n;
    if ((ingress & Waiting) && !(ingress & CountMask)) waitCalls_1(ingress);
  }
  bool callsWaiting() const { return bool(m_callsReady); }
  bool fail(const ID &id) { return m_pending.fail(id); }
  bool pending(const ID &id) { return m_pending.contains(id); }
  bool init(unsigned maxPending) {
    ZmAssert_(!(m_ingress.load_() & CountMask) && !m_sendPosts.load_() && !m_callsReady);
    m_ingress = 0;
    return m_pending.init(maxPending);
  }
  template <typename L>
  bool closeCalls(L &&ready) {
    // Seal admission before observing credits, including callers which have
    // acquired admission but have not yet posted their owner work.
    m_ingress |= Closed;
    // The claimed final credit may still be publishing its owner post.
    // Its registered callback remains a prerequisite until that post runs.
    if (m_callsReady) return false;
    if (m_ingress.load_() & CountMask) {
      waitCalls_0(ZuFwd<L>(ready));
      return false;
    }
    bool done = m_pending.close(derived()->limits().workBatch);
    if (m_ingress.load_() & CountMask) {
      waitCalls_0(ZuFwd<L>(ready));
      return false;
    }
    ZmAssert_(!m_sendPosts.load_());
    return done;
  }

private:
  static constexpr uint64_t Closed = uint64_t(1)<<63;
  static constexpr uint64_t Waiting = uint64_t(1)<<62;
  static constexpr uint64_t CountMask = ~(Closed | Waiting);

  template <typename L>
  void waitCalls_0(L &&ready) {
    m_callsReady = ZuFwd<L>(ready);
    uint64_t ingress = m_ingress |= Waiting;
    if (!(ingress & CountMask)) waitCalls_1(ingress);
  }
  void waitCalls_1(uint64_t ingress) {
    if (m_ingress.cmpXch(ingress & ~Waiting, ingress) != ingress) return;
    derived()->continue_([this]() {
      auto ready = ZuMv(m_callsReady);
      m_callsReady = {};
      ready();
    });
  }
  auto derived() { return static_cast<Derived *>(this); }

  template <typename B, typename Call>
  class BatchCall : public ZmNHeap<BatchCall<B, Call>>, public ZmObject {
  friend inline ZuStringT<"Zjrpc.Batch.Call"> ZmNHeapID(BatchCall *);

    using IDs = ZtArray<ID, ZtArrayHeapID<"Zjrpc.Batch.IDs">>;
  public:
    BatchCall(BatchCaller *caller, B batch, ZmRef<Call> call) :
      m_caller{caller}, m_items{ZuMv(batch)}, m_call{ZuMv(call)} { }

    void start() {
      auto &batch = m_items;
      if (!m_caller->derived()->up() ||
	  batch.count() > m_caller->derived()->limits().maxPending -
	    m_caller->pendingCount()) { failed(); return; }
      if (!m_caller->derived()->prepareBatch(batch)) { failed(); return; }
      CountOutput count;
      Output measure{count, m_caller->derived()->limits().maxJSONBytes};
      batch.write(measure);
      if (!measure) { failed(); return; }
      ids().size(batch.count());
      for (const auto &item : batch.items()) ids().push(item.id());
      register_();
    }
    bool received() {
      if (m_finishing) return true; // A late member during failure cleanup.
      if (!m_left) return false;
      --m_left;
      return true;
    }
    void process(BatchReply reply) {
      if (m_finishing) return;
      m_finishing = true;
      if (m_left) { m_failed = true; m_next = 0; drain_(); return; }
      m_reply = ZuMv(reply);
      done_();
    }
    void failed() {
      if (m_finishing) return;
      m_finishing = true;
      m_failed = true;
      m_next = 0;
      drain_();
    }
    void failed(const Error &) { failed(); }

  private:
    IDs &ids() { return m_ids; }
    void register_() {
      if (m_finishing) return;
      if (!m_caller->derived()->up()) { failed(); return; }
      unsigned n = m_caller->derived()->limits().workBatch;
      unsigned count = ids().length();
      while (n-- && m_next < count) {
	const auto &id = ids()[m_next++];
	if (id.template is<void>()) continue;
	if (!m_caller->batchAdd(id, ZmRef{this})) { failed(); return; }
	++m_left;
	if (m_first.template is<void>()) m_first = id;
      }
      if (m_next < count) {
	m_caller->batchRun([self = ZmRef{this}]() { self->register_(); });
	return;
      }
      m_next = 0;
      if (!m_caller->derived()->send(BatchWire{ZuMv(m_first), ZuMv(m_items),
	  m_caller->derived()->limits().maxJSONBytes})) {
	failed();
	m_caller->derived()->fail_();
      }
    }
    void drain_() {
      auto self = ZmRef{this};
      unsigned n = m_caller->derived()->limits().workBatch;
      unsigned count = ids().length();
      while (n-- && m_next < count) {
	const auto &id = ids()[m_next++];
	if (!id.template is<void>()) m_caller->batchDrop(id, this);
      }
      if (m_next < count) {
	m_caller->batchRun([self = ZuMv(self)]() { self->drain_(); });
	return;
      }
      done_();
    }
    void done_() {
      auto call = ZuMv(m_call);
      if (!call) return;
      try {
	if (m_failed) call->failed();
	else call->process(ZuMv(m_reply));
      } catch (...) { m_caller->derived()->fail_(); }
    }

    BatchCaller *m_caller;
    B m_items;
    IDs m_ids;
    ZmRef<Call> m_call;
    BatchReply m_reply;
    ID m_first;
    unsigned m_left = 0;
    unsigned m_next = 0;
    bool m_finishing = false;
    bool m_failed = false;
  };

  template <typename B>
  bool notifyBatch_(B &batch) {
    if (!derived()->prepareBatch(batch)) return false;
    CountOutput count;
    Output measure{count, derived()->limits().maxJSONBytes};
    batch.write(measure);
    if (!measure) return false;
    return derived()->send(BatchWire{{}, ZuMv(batch), derived()->limits().maxJSONBytes});
  }

  unsigned pendingCount() const { return m_pending.count(); }
  template <typename Call>
  bool batchAdd(ID id, ZmRef<Call> call) { return m_pending.addBatch(ZuMv(id), ZuMv(call)); }
  void batchDrop(const ID &id, const void *call) { m_pending.drop(id, call); }
  template <typename L>
  void batchRun(L &&l) {
    ++m_ingress;
    derived()->continue_([this, l = ZuFwd<L>(l)]() mutable {
      this->releaseCalls();
      l();
    });
  }

private:
  // Outgoing call state; send admission is shared with posting threads.
  alignas(Zm::CacheLineSize)
  PendingCalls m_pending;
  ZmAtomic<uint64_t> m_ingress = 0;
  ReadyFn m_callsReady;
  ZmAtomic<unsigned> m_sendPosts = 0;
};

template <typename Derived, typename Catalog>
class Caller : public BatchCaller<Derived> {
  using Base = BatchCaller<Derived>;
  using Base::m_pending;
  using Base::admitCalls;
  using Base::releaseCalls;
public:
  template <typename Req, typename Call>
  bool call(ObjectValue<typename Req::Object> object, ZmRef<Call> call) {
    ZuAssert((ZuTypeIn<Req, Catalog>{}));
    if (!derived()->up() || !call) return false;
    if (derived()->invoked()) { call_<Req>(ZuMv(object), ZuMv(call)); return true; }
    if (!this->admitCalls()) return false;
    using Action = CallAction<Req, Call>;
    ZmRef<Action> action = new Action{ZuMv(object), ZuMv(call)};
    bool posted = this->sendRun([this, action = ZuMv(action)]() mutable {
      this->releaseCalls();
      call_<Req>(ZuMv(action->object), ZuMv(action->call));
    });
    if (!posted) this->releaseCalls();
    return posted;
  }

  template <typename Req>
  bool notify(ObjectValue<typename Req::Object> object) {
    ZuAssert((ZuTypeIn<Req, Catalog>{}));
    if (!derived()->up()) return false;
    if (derived()->invoked()) return derived()->send(RequestMessage<Req>{{}, ZuMv(object)});
    if (!this->admitCalls()) return false;
    using Action = NotifyAction<Req>;
    ZmRef<Action> action = new Action{ZuMv(object)};
    bool posted = this->sendRun([this, action = ZuMv(action)]() mutable {
      this->releaseCalls();
      if (derived()->up() && !derived()->send(RequestMessage<Req>{{}, ZuMv(action->object)}))
	derived()->fail_();
    });
    if (!posted) this->releaseCalls();
    return posted;
  }

private:
  auto derived() { return static_cast<Derived *>(this); }

  template <typename Req, typename Call>
  void call_(ObjectValue<typename Req::Object> object, ZmRef<Call> call) {
    if (!derived()->up()) {
      try { call->failed(); } catch (...) { }
      return;
    }
    ID id;
    do {
      id = m_next;
      m_next = m_next == INT64_MAX ? 1 : m_next + 1;
    } while (m_pending.contains(id));
    if (!m_pending.template add<Req>(id, call)) {
      try { call->failed(); } catch (...) { }
      return;
    }
    if (!derived()->send(RequestMessage<Req>{id, ZuMv(object)})) {
      (void)m_pending.fail(id);
      derived()->fail_();
      return;
    }
    try { started(call.ptr(), id, 0); } catch (...) { derived()->fail_(); }
  }

  template <typename Call,
    typename = decltype(ZuDeclVal<Call *>()->started(ZuDeclVal<const ID &>()), void())>
  static void started(Call *call, const ID &id, int) { call->started(id); }
  template <typename Call>
  static void started(Call *, const ID &, long) { }

  int64_t m_next = 1;
};

} // Zjrpc

#endif /* ZjrpcPending_HH */
