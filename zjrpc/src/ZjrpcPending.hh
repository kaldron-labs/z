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

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmNoLock.hh>

#include <zlib/ZtArray.hh>

#include <zlib/ZiIOBuf.hh>

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

template <typename Heap = ZuVoid>
struct PendingTable_ : public Heap, public PendingHash {
  ZuDerive_(PendingTable_, PendingHash)
};
ZuDerive(PendingTableHeap, (ZmHeap<"Zjrpc.Pending.Table", PendingTable_<>>));
ZuDerive(PendingTable, (PendingTable_<PendingTableHeap>));

class PendingCalls {
public:
  enum { Open, Closing, Closed };

  PendingCalls(unsigned maxPending = Default::MaxPending) :
    m_maxPending{maxPending} { }

  unsigned count() const { return m_hash->count_(); }
  int state() const { return m_state; }
  bool contains(const ID &id) { return bool(m_hash->findPtr(id)); }

  bool init(unsigned maxPending) {
    if (!maxPending || m_state != Open || m_hash->count_()) return false;
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
  using Headers = typename Req::Headers;
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
  ZmRef<ZiIOBuf> body;
  template <typename S> void write(S &out) const { out << ZuCSpan{*body}; }
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
    ZmRef<ZiIOBuf> body = new BatchBuf{};
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

template <typename Req, typename Call, typename Heap = ZuVoid>
struct CallAction_ : public Heap, public ZmObject {
  ObjectValue<typename Req::Object> object;
  ZmRef<Call> call;
  CallAction_(ObjectValue<typename Req::Object> object_, ZmRef<Call> call_) :
    object{ZuMv(object_)}, call{ZuMv(call_)} { }
};
template <typename Req, typename Call>
using CallActionHeap = ZmHeap<"Zjrpc.Call", CallAction_<Req, Call>>;
template <typename Req, typename Call>
using CallAction = CallAction_<Req, Call, CallActionHeap<Req, Call>>;

template <typename Req, typename Heap = ZuVoid>
struct NotifyAction_ : public Heap, public ZmObject {
  ObjectValue<typename Req::Object> object;
  explicit NotifyAction_(ObjectValue<typename Req::Object> object_) : object{ZuMv(object_)} { }
};
template <typename Req>
using NotifyActionHeap = ZmHeap<"Zjrpc.Notify", NotifyAction_<Req>>;
template <typename Req>
using NotifyAction = NotifyAction_<Req, NotifyActionHeap<Req>>;

template <typename B, typename Heap = ZuVoid>
struct BatchAction_ : public Heap, public ZmObject {
  B batch;
  explicit BatchAction_(B batch_) : batch{ZuMv(batch_)} { }
};
template <typename B>
using BatchActionHeap = ZmHeap<"Zjrpc.Batch.Notify", BatchAction_<B>>;
template <typename B>
using BatchAction = BatchAction_<B, BatchActionHeap<B>>;

template <typename Derived>
class BatchCaller {
  template <typename, typename> friend class Caller;
public:
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
    if ((m_ingress += n) > derived()->limits().maxPending) { m_ingress -= n; return false; }
    bool posted = derived()->ownerRun([this, n, action = ZuMv(action)]() mutable {
      m_ingress -= n;
      action->start();
    });
    if (!posted) m_ingress -= n;
    return posted;
  }

  template <typename B>
  bool notifyBatch(B batch) {
    unsigned n = batch.count();
    if (!derived()->up() || !n || batch.calls() ||
	n >= derived()->limits().maxPending) return false;
    if (derived()->invoked()) return notifyBatch_(batch);
    if ((m_ingress += n) > derived()->limits().maxPending) { m_ingress -= n; return false; }
    ZmRef<BatchAction<B>> action = new BatchAction<B>{ZuMv(batch)};
    bool posted = derived()->ownerRun([this, n, action = ZuMv(action)]() mutable {
      m_ingress -= n;
      if (derived()->up() && !notifyBatch_(action->batch)) derived()->fail_();
    });
    if (!posted) m_ingress -= n;
    return posted;
  }


protected:
  PendingCalls &pendingCalls() { return m_pending; }
  ZmAtomic<unsigned> &ingress() { return m_ingress; }
  bool fail(const ID &id) { return m_pending.fail(id); }
  bool pending(const ID &id) { return m_pending.contains(id); }
  bool init(unsigned maxPending) { return m_pending.init(maxPending); }
  bool closeCalls() {
    if (m_ingress.load_()) return false;
    bool done = m_pending.close(derived()->limits().workBatch);
    return done && !m_ingress.load_();
  }

private:
  auto derived() { return static_cast<Derived *>(this); }

  template <typename B, typename Call, typename Heap = ZuVoid>
  class BatchCall_ : public Heap, public ZmObject {
    using IDs = ZtArray<ID, ZtArrayHeapID<"Zjrpc.Batch.IDs">>;
  public:
    BatchCall_(BatchCaller *caller, B batch, ZmRef<Call> call) :
      m_caller{caller}, m_items{ZuMv(batch)}, m_call{ZuMv(call)} { }

    void start() {
      auto batch = ZuMv(m_items.template p<B>());
      m_items.template p<IDs>(IDs{});
      if (!m_caller->derived()->up() ||
	  batch.count() > m_caller->derived()->limits().maxPending -
	    m_caller->pendingCount()) { failed(); return; }
      if (!m_caller->derived()->prepareBatch(batch)) { failed(); return; }
      m_body = batch.frame(m_caller->derived()->limits().maxJSONBytes);
      if (!m_body) { failed(); return; }
      // Serialized payloads are no longer needed. Retain only member IDs for
      // registration and deterministic cleanup, moving owned string IDs.
      ids().size(batch.count());
      for (auto &item : batch.items()) ids().push(ZuMv(item.id()));
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
    IDs &ids() { return m_items.template p<IDs>(); }
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
      if (!m_caller->derived()->send(BatchWire{ZuMv(m_first), ZuMv(m_body)})) {
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
    ZuUnion<B, IDs> m_items;
    ZmRef<Call> m_call;
    BatchReply m_reply;
    ZmRef<ZiIOBuf> m_body;
    ID m_first;
    unsigned m_left = 0;
    unsigned m_next = 0;
    bool m_finishing = false;
    bool m_failed = false;
  };
  template <typename B, typename Call>
  using BatchCallHeap = ZmHeap<"Zjrpc.Batch.Call", BatchCall_<B, Call>>;
  template <typename B, typename Call>
  using BatchCall = BatchCall_<B, Call, BatchCallHeap<B, Call>>;

  template <typename B>
  bool notifyBatch_(B &batch) {
    if (!derived()->prepareBatch(batch)) return false;
    auto body = batch.frame(derived()->limits().maxJSONBytes);
    return body && derived()->send(BatchWire{{}, ZuMv(body)});
  }

  unsigned pendingCount() const { return m_pending.count(); }
  template <typename Call>
  bool batchAdd(ID id, ZmRef<Call> call) { return m_pending.addBatch(ZuMv(id), ZuMv(call)); }
  void batchDrop(const ID &id, const void *call) { m_pending.drop(id, call); }
  template <typename L>
  void batchRun(L &&l) {
    ++m_ingress;
    derived()->continue_([this, l = ZuFwd<L>(l)]() mutable {
      --m_ingress;
      l();
    });
  }

private:
  PendingCalls m_pending;
  ZmAtomic<unsigned> m_ingress = 0;
};

template <typename Derived, typename Catalog>
class Caller : public BatchCaller<Derived> {
  using Base = BatchCaller<Derived>;
  using Base::m_pending;
  using Base::m_ingress;
public:
  template <typename Req, typename Call>
  bool call(ObjectValue<typename Req::Object> object, ZmRef<Call> call) {
    ZuAssert((ZuTypeIn<Req, Catalog>{}));
    if (!derived()->up() || !call) return false;
    if (derived()->invoked()) { call_<Req>(ZuMv(object), ZuMv(call)); return true; }
    if (++m_ingress > derived()->limits().maxPending) { --m_ingress; return false; }
    using Action = CallAction<Req, Call>;
    ZmRef<Action> action = new Action{ZuMv(object), ZuMv(call)};
    bool posted = derived()->ownerRun([this, action = ZuMv(action)]() mutable {
      --m_ingress;
      call_<Req>(ZuMv(action->object), ZuMv(action->call));
    });
    if (!posted) --m_ingress;
    return posted;
  }

  template <typename Req>
  bool notify(ObjectValue<typename Req::Object> object) {
    ZuAssert((ZuTypeIn<Req, Catalog>{}));
    if (!derived()->up()) return false;
    if (derived()->invoked()) return derived()->send(RequestMessage<Req>{{}, ZuMv(object)});
    if (++m_ingress > derived()->limits().maxPending) { --m_ingress; return false; }
    using Action = NotifyAction<Req>;
    ZmRef<Action> action = new Action{ZuMv(object)};
    bool posted = derived()->ownerRun([this, action = ZuMv(action)]() mutable {
      --m_ingress;
      if (derived()->up() && !derived()->send(RequestMessage<Req>{{}, ZuMv(action->object)}))
	derived()->fail_();
    });
    if (!posted) --m_ingress;
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
