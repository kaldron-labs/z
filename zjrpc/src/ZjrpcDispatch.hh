//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Shared typed inbound dispatch, work lifetime and batch aggregation

#ifndef ZjrpcDispatch_HH
#define ZjrpcDispatch_HH

#ifndef ZjrpcLib_HH
#include <zlib/ZjrpcLib.hh>
#endif

#include <zlib/ZuDerive.hh>
#include <zlib/ZuMatcher.hh>
#include <zlib/ZuSwitch.hh>

#include <zlib/ZmContext.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmPolymorph.hh>

#include <zlib/ZiAssert.hh>

#include <zlib/Zjrpc.hh>
#include <zlib/ZjrpcCompletion.hh>
#include <zlib/ZjrpcPending.hh>
#include <zlib/ZjrpcInbound.hh>
#include <zlib/ZjrpcIO.hh>

namespace Zjrpc {

namespace RoutePolicy { enum { Abort = -1 }; }

// A transport route can cancel its root work without another lookup table.
// Work retains the route through Emit and clears this back-pointer at finish.
class WorkControl {
public:
  WorkControl() = default;
  WorkControl(const WorkControl &) = delete;
  WorkControl &operator =(const WorkControl &) = delete;
  template <typename Work>
  void bind(Work *work) {
    m_work = work;
    m_close = [](void *work_) { static_cast<Work *>(work_)->closeGroup_(); };
  }
  void clear() { m_work = nullptr; }
  void close() { if (m_work) m_close(m_work); }
private:
  void *m_work = nullptr;
  void (*m_close)(void *) = nullptr;
};

template <typename T> using GetMethod = typename T::Method;
template <typename T> using GetBorrowed = ZuBool<T::Borrowed>;

template <typename Catalog>
struct Contract {
  using Keys = ZuTypeMap<GetMethod, Catalog>;
  ZuAssert(Keys::N == ZuTypeUnique<Keys>::N);
};

struct ErrorMessage {
  ID id;
  Error error;
  ErrorMessage(ID id_, int code, ZuCSpan message) :
    id{ZuMv(id_)}, error{ErrorString{message}, code} { }
  ErrorMessage(ID id_, Error error_) : id{ZuMv(id_)}, error{ZuMv(error_)} { }
  template <typename S>
  void write(S &out) const { saveError(out, id, error); }
};

template <typename Res>
struct ResultMessage {
  using Response = Res;
  using Headers = typename Res::Headers;
  ID id;
  Reply<Res> reply;
  template <typename S>
  void write(S &out) const {
    if constexpr (ZuIsSame<void, typename Res::Body>{})
      saveResult(out, id, EmptyObject{});
    else
      saveResult(out, id, reply.bodyObject());
  }
};

// Only a real batch needs a retained serialized response store.
struct BatchMessage {
  ZmRef<ZiIOBuf> body;
  template <typename S> void write(S &out) const { out << ZuCSpan{*body}; }
};

template <typename Catalog, template <typename> class Result = ResultMessage,
  typename Extra = ZuTypeList<>>
class ServerMessage {
  using Responses = ZuTypeUnique<ZuTypeApply<ZuTypeConcat, ZuTypeMap<GetResponses, Catalog>>>;
  using Messages = ZuTypeConcat<ZuTypeList<ErrorMessage, BatchMessage>, Extra,
    ZuTypeMap<Result, Responses>>;
  using Data = ZuTypeApply<ZuUnion, ZuTypeConcat<ZuTypeList<void>, Messages>>;
public:
  ServerMessage() = default;
  template <typename M> ServerMessage(M message) : m_data{ZuMv(message)} { }
  bool empty() const { return !m_data.type(); }
  template <typename Input>
  void input(ZmRef<Input> input) { m_input = ZmContext{ZuMv(input)}; }
  template <typename L>
  decltype(auto) dispatch(L &&l) { return m_data.dispatch(ZuFwd<L>(l)); }
  template <typename L>
  decltype(auto) cdispatch(L &&l) const { return m_data.cdispatch(ZuFwd<L>(l)); }
  template <typename S>
  void write(S &out) const {
    m_data.cdispatch([&out](auto, const auto &message) { message.write(out); });
  }
private:
  // Only borrowed replies pin their input work through asynchronous output.
  ZmContext m_input;
  Data m_data;
};

// A protocol extension supplies declarations and application payload policy;
// admission, correlation, work ownership and batch aggregation stay here.
template <typename Catalog>
struct DispatchPolicy {
  using Message = ServerMessage<Catalog>;
  template <typename Work> using Completions = CompletionSet<Work, Work>;
  template <typename Work> static void begin(Work &) { }
  template <typename Work> static void end(Work &) { }
  template <typename Work> static bool deliverable(Work &) { return true; }
  template <typename Work> static bool cancelOnClose(const Work &) { return true; }
  template <typename Work, typename Req, typename Token>
  static void made(Work &, Req *, Token *) { }
  static bool valid(const Envelope &) { return true; }
  template <typename Emit>
  static bool invalid(Emit &emit, const Parsed &parsed) {
    return emit(ErrorMessage{Null{},
	parsed.consumed < 0 ? ErrorCode::Parse : ErrorCode::InvalidRequest,
	"Invalid request"}, BodyPolicy::Fixed);
  }
  template <typename Dispatch, typename Work, typename Emit, typename Tool>
  static bool dispatch(Dispatch &dispatcher, const Envelope &envelope,
      Work &work, Emit &emit, Tool &tool) {
    if (!dispatcher.dispatch(envelope, work, emit, tool))
      emit(ErrorMessage{Null{}, ErrorCode::InvalidRequest, "Invalid request"});
    return true;
  }
  template <typename Work, typename Req, typename Object, typename Token>
  static void invoke(Work &work, Req *req, const Object &object, Token token) {
    work.request(req, object, ZuMv(token));
  }
  template <typename Work, typename Res>
  static void complete(Work &work, const ID &id, Reply<Res> reply) {
    work.publish(ResultMessage<Res>{id, ZuMv(reply)});
  }
};

// Owner provides shard posting, failure and response correlation. Emit retains
// the existing transport route, not another peer or connection abstraction.
template <typename Owner, typename Impl, typename Catalog, typename Context = ZuVoid,
  typename Policy = DispatchPolicy<Catalog>>
class Dispatcher {
public:
  using Message = typename Policy::Message;
  using Emit = ZmFn<bool(Message, int), ZmFnHeapID<"Zjrpc.Route">>;

  template <typename Completions, typename Emit, typename Tool>
  bool dispatch(const Envelope &envelope, Completions &completions,
      Emit &&emit, Tool &&tool) {
    if (envelope.kind != MessageKind::Request &&
	envelope.kind != MessageKind::Notification) return false;
    auto error = [&envelope, &emit](int code, ZuCSpan message) {
      if (envelope.kind == MessageKind::Request)
	emit(ErrorMessage{envelope.id(), code, message});
    };
    if constexpr (!Catalog::N) {
      error(ErrorCode::MethodNotFound, "Method not found");
      return true;
    } else {
      constexpr auto matcher = ZuMatcher<Contract<Catalog>>();
      int index = matcher.exact(envelope.method());
      if (index < 0) {
	error(ErrorCode::MethodNotFound, "Method not found");
	return true;
      }
      try {
	ZuSwitch::dispatch<Catalog::N>(index,
	  [&envelope, &completions, &error, &tool](auto I) {
	    using Req = ZuType<I, Catalog>;
	    using O = typename Req::Object;
	    auto invoke = [&envelope, &completions, &error, &tool](const O &object) {
	      using Token = decltype(completions.template make<Req>(envelope.id()));
	      Token token;
	      if (envelope.kind == MessageKind::Request) {
		token = completions.template make<Req>(envelope.id());
		if (!token) { error(ErrorCode::Internal, "Completion unavailable"); return; }
	      }
	      tool(static_cast<Req *>(nullptr), object, ZuMv(token));
	    };
	    if (!loadParams<O>(raw(envelope.params()), invoke))
	      error(ErrorCode::InvalidParams, "Invalid params");
	  });
      } catch (const Error &failure) {
	(void)completions.fail(envelope.id());
	if (envelope.kind == MessageKind::Request)
	  emit(ErrorMessage{envelope.id(), failure});
      } catch (...) {
	(void)completions.fail(envelope.id());
	error(ErrorCode::Internal, "Internal error");
      }
      return true;
    }
  }

protected:
  bool receive(ZmRef<ZiIOBuf> body, Emit emit, InboundCalls *inbound,
      Context *context = nullptr, WorkControl *control = nullptr) {
    if (!owner()->up()) return false;
    auto parsed = parse(body->span(), owner()->limits().maxJSONBytes);
    return receive(ZuMv(body), ZuMv(parsed), ZuMv(emit), inbound, context, control);
  }

  bool receive(ZmRef<ZiIOBuf> body, Parsed parsed, Emit emit, InboundCalls *inbound,
      Context *context = nullptr, WorkControl *control = nullptr) {
    if (!parsed) return Policy::invalid(emit, parsed);
    if (parsed.envelope.kind == MessageKind::Result ||
	parsed.envelope.kind == MessageKind::Error)
    {
      bool ok = owner()->response(parsed.envelope);
      return emit(Message{}, ok ? BodyPolicy::Fixed : RoutePolicy::Abort);
    }
    if (m_admitted >= owner()->limits().maxPending) return false;
    unsigned members = parsed.batch() ?
      parsed.value()->template data<ZfJSON::AnyNode::Array>().length() : 0;
    if (members >= owner()->limits().maxPending - m_admitted) return false;
    m_admitted += 1 + members;
    ZmRef<Work> work = new Work{this, ZuMv(body), ZuMv(parsed), ZuMv(emit), inbound, context};
    if (control) work->control(control);
    m_works.pushNode(work);
    if (work->batch()) work->runBatch_();
    else work->run();
    return true;
  }

  bool close() {
    unsigned left = owner()->limits().workBatch;
    while (left--) {
      // Members borrow their batch's tree; tear down children before parents.
      auto work = m_works.tailNode();
      if (!work) break;
      work->close_();
    }
    return !m_works.count_();
  }

public:
  // Protocol policy uses the real owner; no runtime adapter or registry.
  auto owner() { return static_cast<Owner *>(this); }

private:

  template <typename Req, typename Token, typename I = Impl,
    typename = decltype(ZuDeclVal<I *>()->cancelled(
      ZuDeclVal<Req *>(), ZuDeclVal<Token *>(), ZuDeclVal<ZuCSpan>()), void())>
  void cancelled(Req *req, Token *token, ZuCSpan reason, int) {
    owner()->impl()->cancelled(req, token, reason);
  }
  template <typename Req, typename Token>
  void cancelled(Req *, Token *, ZuCSpan, long) { }

  enum { Borrowed = bool(ZuTypeGrep<GetBorrowed, Catalog>::N) };
  // ZmContext needs a polymorphic destructor only for catalogs that borrow input.
  using WorkBase = ZuIf<Borrowed, ZmPolymorph, ZmObject>;
  bool response_(const Envelope &envelope, PendingHash::NodeMvRef *batch) {
    return response_(owner(), envelope, batch, 0);
  }
  template <typename O,
    typename = decltype(ZuDeclVal<O *>()->response(
      ZuDeclVal<const Envelope &>(), ZuDeclVal<PendingHash::NodeMvRef *>()), void())>
  static bool response_(O *owner, const Envelope &envelope, PendingHash::NodeMvRef *batch, int) {
    return owner->response(envelope, batch);
  }
  template <typename O>
  static bool response_(O *owner, const Envelope &envelope, PendingHash::NodeMvRef *, long) {
    return owner->response(envelope);
  }

  class WorkData : public WorkBase, public InboundHash::Node,
      public Policy::template Completions<WorkData> {
    using Base = typename Policy::template Completions<WorkData>;
    template <typename Heap = ZuVoid>
    struct BatchState_ : public Heap {
      WorkData *tail = nullptr;
      PendingHash::NodeMvRef response;
      ZmRef<ZiIOBuf> output;
      unsigned next = 0;
      unsigned left;

      explicit BatchState_(unsigned n) : left{n} { }
    };
    ZuDerive(BatchStateHeap, (ZmHeap<"Zjrpc.Batch.Work", BatchState_<>>));
    ZuDerive(BatchState, (BatchState_<BatchStateHeap>));
  public:
    using OwnerType = Owner;
    Owner *owner() { return m_dispatcher->owner(); }
    const Context *context() const { return m_context; }
    InboundCalls *inbound() const { return m_inbound; }
    bool active() const { return !m_closed && !m_terminal; }
    template <typename Req, typename Object, typename Token>
    void request(Req *req, const Object &object, Token token) {
      if constexpr (ZuIsSame<Context, ZuVoid>{})
	owner()->impl()->request(req, object, ZuMv(token));
      else
	owner()->impl()->request(req, object, *m_context, ZuMv(token));
    }
    WorkData(Dispatcher *dispatcher, ZmRef<ZiIOBuf> body, Parsed parsed,
	Emit emit, InboundCalls *inbound, Context *context) :
      m_dispatcher{dispatcher}, m_body{ZuMv(body)}, m_parsed{ZuMv(parsed)},
      m_emit{ZuMv(emit)}, m_inbound{inbound}, m_context{context} {
      if (batch()) m_batch = new BatchState{nodes().length()};
    }
    WorkData(WorkData *parent, Envelope envelope) :
      m_dispatcher{parent->m_dispatcher}, m_parent{parent},
      m_inbound{parent->m_inbound}, m_context{parent->m_context} {
      m_parsed.envelope = ZuMv(envelope);
      m_prev = parent->m_batch->tail;
      m_nextMember = nullptr;
      if (m_prev) m_prev->m_nextMember = this;
      parent->m_batch->tail = this;
    }

    ~WorkData() { if (!m_parent) delete m_batch; }

    bool batch() const { return m_parsed.batch(); }
    void control(WorkControl *control) {
      m_control = control;
      control->bind(this);
    }

    void run() {
      const auto &envelope = m_parsed.envelope;
      if (!Policy::valid(envelope)) { fail_(); finish(); return; }
      if (envelope.kind == MessageKind::Result || envelope.kind == MessageKind::Error) {
	if (!m_dispatcher->response_(envelope, m_parent ? &m_parent->m_batch->response : nullptr))
	  fail_();
	finish();
	return;
      }
      if (envelope.kind == MessageKind::Request && m_inbound) {
	if (!m_inbound->begin(envelope.id(), this)) {
	  emit(ErrorMessage{envelope.id(), ErrorCode::InvalidRequest, "Duplicate request ID"});
	  finish();
	  return;
	}
      }
      m_dispatching = true;
      try { Policy::begin(*this); }
      catch (...) { m_dispatching = false; fail_(); finish(); return; }
      auto reply = [this](auto &&message) {
	this->emit(ZuFwd<decltype(message)>(message));
      };
      auto invoke = [this](auto *req, const auto &object, auto token) {
	Policy::invoke(*this, req, object, ZuMv(token));
      };
      if (!Policy::dispatch(*m_dispatcher, envelope, *this, reply, invoke)) fail_();
      m_dispatching = false;
      if (!m_parent && !m_borrowed) {
	m_parsed.root = nullptr;
	m_body = nullptr;
      }
      if (!this->count()) finish();
    }

    void runBatch_() {
      if (m_closed) return;
      unsigned n = nodes().length();
      if (!n) {
	emit(ErrorMessage{Null{}, ErrorCode::InvalidRequest, "Empty batch"});
	finish();
	return;
      }
      unsigned end = n - m_batch->next;
      unsigned limit = m_dispatcher->owner()->limits().workBatch;
      if (end > limit) end = limit;
      end += m_batch->next;
      while (m_batch->next < end) {
	auto envelope = decode(nodes()[m_batch->next++].ptr());
	ZmRef<Work> member = new Work{this, ZuMv(envelope)};
	m_dispatcher->addWork(member);
	member->run();
      }
      if (m_batch->next < n) {
	auto self = ZmRef{static_cast<Work *>(this)};
	m_dispatcher->owner()->continue_([self = ZuMv(self)]() { self->runBatch_(); });
      }
    }

    using Base::complete;
    using Base::cancel;
    using Base::cancelled;
    template <typename Req, typename T>
    void made(Req *req, T *token) {
      m_policy = Req::ResponseBody;
      m_borrowed = Req::Borrowed;
      Policy::made(*this, req, token);
    }
    template <typename Req, typename T>
    void cancelled(Req *req, T *token, ZuCSpan reason) {
      m_dispatcher->cancelled(req, token, reason, 0);
    }

    CompletionRoute completionRoute_() const {
      return m_dispatcher->owner()->completionRoute();
    }
    template <typename T, typename Res>
    bool completion(T *token, Reply<Res> reply) {
      ZiAssert(m_dispatcher->owner()->invoked(), "Zjrpc", (),
	"completion outside owner shard", return false);
      if (!m_dispatcher->owner()->up()) return false;
      auto self = ZmRef{static_cast<Work *>(this)};
      return this->complete_(token, ZuMv(reply));
    }
    template <typename Res>
    void complete(const ID &id, Reply<Res> reply) {
      pendingDone();
      Policy::complete(*this, id, ZuMv(reply));
      if (!m_dispatching) finish();
    }
    bool cancel_(const ID &id, ZuCSpan reason) {
      auto self = ZmRef{static_cast<Work *>(this)};
      if (!CompletionSlot::cancel(id, reason)) return false;
      if (!Policy::deliverable(*this)) close_(reason);
      return true;
    }
    bool cancelPeer(const ID &id, ZuCSpan reason) {
      return m_inbound && m_inbound->cancel(id, reason);
    }
    template <typename M>
    bool publish(M message, bool terminal = true) {
      if (m_terminal || m_closed) return false;
      if (terminal) { emit(ZuMv(message)); return !m_closed; }
      auto root = this;
      while (root->m_parent) root = root->m_parent;
      bool ok = root->m_emit(Message{ZuMv(message)}, m_policy);
      if (!ok) fail_();
      return ok;
    }
    void close_(ZuCSpan reason = "peer closed") {
      if (m_closed) return;
      m_closed = true;
      pendingDone();
      if (m_parent) m_parent->close_(reason);
      else if (!m_terminal) fail_();
      if (!m_parent && m_batch && m_batch->response) m_batch->response->failed();
      if (Policy::cancelOnClose(*this)) this->cancelAll(reason);
      this->drain();
      if (batch()) {
	unsigned undispatched = nodes().length() - m_batch->next;
	m_dispatcher->releaseWork(undispatched);
	m_batch->left -= undispatched;
	m_batch->next += undispatched;
	if (m_batch->left) return; // Members still borrow this batch's input/tree.
      }
      if (m_dispatching) return; // The active dispatch frame performs final removal.
      finish();
    }

    void closeGroup_() {
      auto self = ZmRef{static_cast<Work *>(this)};
      close_("stream closed");
      if (!m_batch || !m_batch->tail) return;
      unsigned left = m_dispatcher->owner()->limits().workBatch;
      while (m_batch->tail && left--) m_batch->tail->close_("stream closed");
      if (m_batch->tail) m_dispatcher->owner()->continue_([
	self = ZuMv(self)]() { self->closeGroup_(); });
    }

  private:
    template <typename T, typename Res>
    bool complete_(T *token, Reply<Res> reply) {
      // Cancellation callbacks may try to complete while teardown owns work.
      if (m_closed) return false;
      bool deliverable = Policy::deliverable(*this);
      return Base::complete_(token, ZuMv(reply)) && deliverable;
    }

    const ZfJSON::NodeArray &nodes() const {
      return m_parsed.value()->template data<ZfJSON::AnyNode::Array>();
    }
    template <typename M>
    void emit(M message) {
      if (m_terminal || m_closed) return;
      m_terminal = true;
      pendingDone();
      if (m_parent) { m_parent->collect(message); return; }
      Message output{ZuMv(message)};
      if constexpr (Borrowed)
	if (m_borrowed) output.input(ZmRef{static_cast<Work *>(this)});
      if (!m_emit(ZuMv(output), m_policy)) fail_();
    }
    template <typename M>
    void collect(const M &message) {
      if (m_closed || m_terminal) return;
      if (!m_batch->output) {
	m_batch->output = new BatchBuf{};
	*m_batch->output << '[';
      }
      BufOutput out{*m_batch->output, m_dispatcher->owner()->limits().maxJSONBytes};
      if (m_batch->output->length > 1) out << ',';
      message.write(out);
      if (!out) fail_();
    }
    void fail_() {
      if (m_parent) { m_parent->fail_(); return; }
      m_terminal = true;
      if (!m_emit(Message{}, RoutePolicy::Abort)) m_dispatcher->owner()->fail_();
    }
    void pendingDone() {
      if (this->id.template is<void>()) return;
      m_inbound->finish(this->id, this);
      this->id.null();
    }
    void memberDone() {
      if (--m_batch->left) return;
      if (m_closed) { finish(); return; }
      if (m_batch->response) {
	auto self = ZmRef{static_cast<Work *>(this)};
	auto response = ZuMv(m_batch->response);
	bool failed = m_terminal || m_batch->output;
	BatchReply reply{ZuMv(m_body), ZuMv(m_parsed)};
	m_terminal = true;
	finish(); // Remove aggregate work before its application callback.
	if (failed) response->failed();
	else response->batchFn(response->object, ZuMv(reply));
	return;
      }
      if (m_batch->output && !m_terminal) {
	BufOutput out{*m_batch->output, m_dispatcher->owner()->limits().maxJSONBytes};
	out << ']';
	if (!out) fail_();
	else emit(BatchMessage{ZuMv(m_batch->output)});
      } else if (!m_terminal && !m_emit(Message{}, m_policy)) fail_();
      finish();
    }
    void finish() {
      pendingDone();
      Policy::end(*this);
      if (!m_parent && m_control) m_control->clear();
      if (m_parent) {
	if (m_prev) m_prev->m_nextMember = m_nextMember;
	if (m_nextMember) m_nextMember->m_prev = m_prev;
	else m_parent->m_batch->tail = m_prev;
      }
      auto removed = m_dispatcher->delWork(static_cast<Work *>(this));
      if (m_parent) m_parent->memberDone();
      else if (!m_closed && !m_terminal && !batch())
	if (!m_emit(Message{}, m_policy)) fail_();
    }

    Dispatcher *m_dispatcher;
    WorkData *m_parent = nullptr;
    // Root and member roles are disjoint; reuse their routing/link storage.
    union { BatchState *m_batch = nullptr; WorkData *m_prev; };
    union { WorkControl *m_control = nullptr; WorkData *m_nextMember; };
    ZmRef<ZiIOBuf> m_body;
    Parsed m_parsed;
    Emit m_emit;
    InboundCalls *m_inbound;
    Context *m_context;
    int m_policy = BodyPolicy::Fixed;
    bool m_dispatching = false;
    bool m_borrowed = false;
    bool m_terminal = false;
    bool m_closed = false;
  };
  using WorkQ = ZmList<WorkData, ZmListNode<WorkData,
    ZmListHeapID<"Zjrpc.Work">>>;
  using Work = typename WorkQ::Node;

  void addWork(ZmRef<Work> work) { m_works.pushNode(ZuMv(work)); }
  auto delWork(Work *work) {
    --m_admitted;
    return m_works.delNode(work);
  }
  void releaseWork(unsigned n) { m_admitted -= n; }

  WorkQ m_works;
  unsigned m_admitted = 0;
};

} // Zjrpc

#endif /* ZjrpcDispatch_HH */
