//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Typed gRPC adapters for the public HTTP message interface

#ifndef ZgrpcHTTP_HH
#define ZgrpcHTTP_HH

#ifndef ZgrpcLib_HH
#include <zlib/ZgrpcLib.hh>
#endif

#include <zlib/ZgrpcIO.hh>

namespace Zgrpc {

using ReqControl = ZhttpHeaders(
  ("content-type", "application/grpc+proto"), ("te", "trailers"),
  ("grpc-accept-encoding", "identity"), "grpc-timeout");
using ResControl = ZhttpHeaders(
  ("content-type", "application/grpc+proto"), ("grpc-accept-encoding", "identity"));
// Alt-Svc also lets the HTTP client update discovery before RPC dispatch.
using RxControl = ZhttpHeaders(
  "content-type", "te", "content-length", "alt-svc", "grpc-timeout", "grpc-status",
  "grpc-message", "grpc-status-details-bin", "grpc-encoding", "grpc-accept-encoding");

template <typename M, bool Response>
using RxHeaders = Zhttp::MergeHeaders<ZuTypeConcat<RxControl,
  ZuIf<Response, ZuTypeConcat<typename M::ResHeaders, typename M::Trailers>,
    typename M::ReqHeaders>>>;

template <typename Impl, typename M, bool Response>
class BodyBuilder {
  using Object = ZuIf<Response, typename M::Response, typename M::Request>;
  using Facet = ZuIf<Response, typename M::ResFacet, typename M::ReqFacet>;
  enum { Streamed = Response ?
    M::Kind == Kind::ServerStream || M::Kind == Kind::Duplex :
    M::Kind == Kind::ClientStream || M::Kind == Kind::Duplex };
public:
  BodyBuilder() : m_body{Streamed} { }
  void limits(Limits value) { m_limits = value; }
  bool producerReady() const { return m_body.ready(); }
  bool productionFailed() const { return m_body.failed(); }
  Code::T productionCode() const { return m_error == Code::OK ? m_body.code() : m_error; }
  uint64_t messages() const { return m_body.messages(); }
  unsigned queuedMessages() const { return m_frames.count(); }
  uint64_t queuedBytes() const { return m_frames.bytes(); }

  // Defaults are side-effect-safe. Endpoints override these for their own
  // destination-owned queues/failure path; they must not invoke Rx handlers.
  void producerStarted() { }
  void producerQueued() { (void)drain_(); }
  bool productionDrained() { return true; }
  void productionFailed(Code::T) { }
  void signMessage(ZuBSpan) const { }

  bool send_(Message<Object> message) {
    if (m_sealed) return fail_(Code::INTERNAL);
    if (m_frames.bytes() + PrefixSize > m_limits.maxQueueBytes) return fail_(Code::RESOURCE_EXHAUSTED);
    if (!m_body.message()) return fail_(Code::INTERNAL);
    if (m_frames.count() >= m_limits.maxQueue) return fail_(Code::RESOURCE_EXHAUSTED);
    Code::T error = Code::INTERNAL;
    ZmRef<ZiIOBuf> buf;
    bool ok = false;
    try {
      ok = m_body.write([this, &message, &error, &buf](auto &writer) {
	try {
	  auto first = writer.allocBuf(writer.headRoom());
	  if (!first) return true;
	  FrameError::T frameError;
	  buf = encodeFrame<Facet>(ZuMv(first), writer.tailRoom(), messageLimit_(),
	    message.object(), frameError, [this](ZuBSpan bytes) { impl()->signMessage(bytes); });
	  if (!buf) error = frameError == FrameError::Limit ? Code::RESOURCE_EXHAUSTED : Code::INTERNAL;
	} catch (...) { buf = nullptr; }
	return true;
      }, [this]() { noteFailure_(Code::INTERNAL); });
    } catch (...) { ok = false; }
    if (!ok || !buf) return fail_(error);
    if (!m_frames.push(ZuMv(buf), m_limits)) return fail_(Code::RESOURCE_EXHAUSTED);
    return queued_();
  }
  bool send_(ObjectValue<Object> object) { return send_(Message<Object>{ZuMv(object)}); }

  // Raw bytes are locally trusted protobuf or explicitly opaque forwarding.
  // Copy them directly into a final writer-allocated envelope buffer.
  bool sendRaw_(ZuBSpan bytes) {
    if (m_sealed) return fail_(Code::INTERNAL);
    if (m_frames.bytes() + PrefixSize > m_limits.maxQueueBytes || bytes.length() > messageLimit_())
      return fail_(Code::RESOURCE_EXHAUSTED);
    if (!m_body.message()) return fail_(Code::INTERNAL);
    if (m_frames.count() >= m_limits.maxQueue) return fail_(Code::RESOURCE_EXHAUSTED);
    ZmRef<ZiIOBuf> buf;
    Code::T error = Code::INTERNAL;
    bool ok = false;
    try {
      ok = m_body.write([this, bytes, &buf, &error](auto &writer) {
	try {
	  impl()->signMessage(bytes);
	  buf = writer.allocBuf(writer.headRoom());
	  if (!buf) return true;
	  uint64_t size = uint64_t(buf->skip) + PrefixSize + bytes.length() + writer.tailRoom();
	  if (size > INT_MAX) { error = Code::RESOURCE_EXHAUSTED; buf = nullptr; return true; }
	  if (!buf->ensure<rawGrow_>(unsigned(size) - buf->skip)) { buf = nullptr; return true; }
	  auto out = buf->data();
	  framePrefix(out, bytes.length());
	  if (bytes.length()) memcpy(out + PrefixSize, bytes.data(), bytes.length());
	  buf->length = PrefixSize + bytes.length();
	} catch (...) { buf = nullptr; }
	return true;
      }, [this]() { noteFailure_(Code::INTERNAL); });
    } catch (...) { ok = false; }
    if (!ok || !buf) return fail_(error);
    if (!m_frames.push(ZuMv(buf), m_limits)) return fail_(Code::RESOURCE_EXHAUSTED);
    return queued_();
  }
  // Producers transfer mutable unpublished output with the lower writer's
  // head/tail room plus PrefixSize reserved before encoding raw protobuf.
  // The destination writer handles concrete storage authority and geometry;
  // published payloads use the borrowed-span overload.
  bool sendRaw_(ZmRef<ZiIOBuf> source) {
    if (m_sealed) return fail_(Code::INTERNAL);
    if (!source || source->failed() ||
	m_frames.bytes() + PrefixSize > m_limits.maxQueueBytes ||
	source->length > messageLimit_()) return fail_(Code::RESOURCE_EXHAUSTED);
    if (!m_body.message()) return fail_(Code::INTERNAL);
    if (m_frames.count() >= m_limits.maxQueue) return fail_(Code::RESOURCE_EXHAUSTED);
    bool prepared = false;
    bool ok = false;
    try {
      ok = m_body.write([this, &source, &prepared](auto &writer) {
	if (uint64_t(writer.headRoom()) + PrefixSize > source->skip ||
	    source->avail() < writer.tailRoom()) return true;
	impl()->signMessage(source->cspan());
	source->skip -= PrefixSize;
	source->length += PrefixSize;
	framePrefix(source->data(), source->length - PrefixSize);
	prepared = true;
	return true;
      }, [this]() { noteFailure_(Code::INTERNAL); });
    } catch (...) { ok = false; }
    if (!ok || !prepared) return fail_(Code::INTERNAL);
    if (!m_frames.push(ZuMv(source), m_limits))
      return fail_(Code::RESOURCE_EXHAUSTED);
    return queued_();
  }

  bool drain_() {
    if (m_draining) return true;
    if (!m_body.ready()) return false;
    m_draining = true;
    bool ok = true;
    if (m_frames.count()) {
      ok = m_body.write([this](auto &writer) {
	unsigned n = 0;
	while (m_frames.count() && n++ < m_limits.workBatch) {
	  auto frame = m_frames.shift();
	  if (!writer.appendBuf(ZuMv(frame))) return false;
	}
	return true;
      }, [this]() { noteFailure_(Code::INTERNAL); });
    }
    m_draining = false;
    if (!ok) return fail_(Code::INTERNAL);
    if (m_frames.count()) return queued_();
    return impl()->productionDrained();
  }
  void close() { m_body.close(); m_frames.clear(); }

protected:
  auto impl() { return static_cast<Impl *>(this); }
  bool attach_(Zhttp::BodyEmitter emit) {
    if (!valid(m_limits) || !m_body.attach(ZuMv(emit))) return fail_(Code::INTERNAL);
    impl()->producerStarted();
    return true;
  }
  bool queued_() { impl()->producerQueued(); return !m_body.failed(); }
  bool seal_(bool success) {
    if (m_sealed || !m_body.ready() || (!Streamed && success && m_body.messages() != 1))
      return fail_(Code::INTERNAL);
    m_sealed = true;
    return true;
  }
  bool end_() { return m_body.end() ? true : fail_(m_body.code()); }
  template <typename Fields> bool finish_(Code::T code, Fields &fields) {
    return m_body.finish(code, fields) ? true : fail_(m_body.code());
  }
  bool fail_(Code::T code) {
    noteFailure_(code);
    m_frames.clear();
    m_body.abort(m_error);
    return false;
  }

private:
  void noteFailure_(Code::T code) {
    if (m_error != Code::OK) return;
    m_error = code;
    impl()->productionFailed(code);
  }
  static unsigned rawGrow_(unsigned, unsigned size) { return size; }
  unsigned messageLimit_() const {
    unsigned max = M::MessageLimit ? M::MessageLimit : m_limits.maxMessageBytes;
    uint64_t used = m_frames.bytes() + PrefixSize;
    unsigned queued = m_limits.maxQueueBytes > used ? unsigned(m_limits.maxQueueBytes - used) : 0;
    return max < queued ? max : queued;
  }
  Limits m_limits;
  TxBody m_body;
  TxFrames m_frames;
  Code::T m_error = Code::OK;
  bool m_sealed = false, m_draining = false;
};

template <typename Impl, typename M>
class ReqBuilder : public Zhttp::ReqBuilder, public BodyBuilder<Impl, M, false> {
  using Body = BodyBuilder<Impl, M, false>;
public:
  using Headers = ZuTypeConcat<ReqControl, typename M::ReqHeaders>;
  using HdrCatalog = WireCatalog<Headers>;
  using Zhttp::ReqBuilder::header;
  using Body::close;
  ZuAssert((MethodValid<M>{}));

  Metadata metadata;
  int64_t timeout = -1;
  uint64_t sequence = 0;

  uint64_t key() const { return sequence; }
  bool replayable() const { return false; }
  bool idempotent(Zhttp::Method::T) const { return false; }
  Zhttp::BodyPolicy::T bodyPolicy() const { return Zhttp::BodyPolicy::Stream; }
  template <typename L> void operation(L &&l) const {
    l(Zhttp::Method::POST, [](auto &&emit) {
      emit([](auto &writer) { writer << MethodPath<M>{}(); });
    });
  }
  template <typename Key, typename L> void header(L &&l) const {
    if constexpr (ZuIsSame<Key, ZuStringT<"grpc-timeout">>{})
      if (timeout >= 0) l(encodeTimeout(timeout));
  }
  template <typename L> void header(L &&l) const {
    MetaFields<> fields{metadata};
    fields.header(ZuFwd<L>(l));
  }
  void body(Zhttp::BodyEmitter emit) {
    if (metadata.failed()) { Body::fail_(Code::INTERNAL); return; }
    Body::attach_(ZuMv(emit));
  }
  bool end_() {
    if (!Body::seal_(true)) return false;
    m_ending = true;
    return Body::queuedMessages() ? Body::queued_() : productionDrained();
  }
  bool productionDrained() {
    if (!m_ending) return true;
    m_ending = false;
    return Body::end_();
  }
private:
  bool m_ending = false;

};

template <typename Impl, typename M>
class ResBuilder : public Zhttp::ResBuilder, public BodyBuilder<Impl, M, true> {
  using Body = BodyBuilder<Impl, M, true>;
  using Terminal = EndFields<typename M::Trailers, Impl>;
public:
  using Headers = ZuTypeConcat<ResControl, typename M::ResHeaders,
    ZhttpHeaders("grpc-status", "grpc-message", "grpc-status-details-bin")>;
  using HdrCatalog = WireCatalog<Headers>;
  using Zhttp::ResBuilder::header;
  using Body::close;
  ZuAssert((MethodValid<M>{}));

  Metadata metadata;

  Zhttp::Method::T method() const { return Zhttp::Method::POST; }
  unsigned status() const { return 200; }
  Zhttp::BodyPolicy::T bodyPolicy() const {
    return m_trailersOnly ? Zhttp::BodyPolicy::None : Zhttp::BodyPolicy::Stream;
  }
  bool trailersOnly(Status status, Metadata trailers = Metadata{}) {
    if (status.code == Code::OK || Body::producerReady() || Body::messages()) return false;
    m_status = ZuMv(status); m_trailers = ZuMv(trailers); m_trailersOnly = true;
    return !m_trailers.failed();
  }

  template <typename Key, typename Value, typename L> void header(L &&l) const {
    // Trailer defaults belong in the terminating section, not initial fields.
    using Initial = Zhttp::HeaderValues<Key, typename M::ResHeaders>;
    using Final = Zhttp::HeaderValues<Key, typename M::Trailers>;
    using Control = Zhttp::HeaderValues<Key, ResControl>;
    if constexpr (ZuTypeIn<Value, Control>{} || ZuTypeIn<Value, Initial>{}) l();
    else if constexpr (ZuTypeIn<Value, Final>{}) { if (m_trailersOnly) l(); }
  }
  template <typename Key, typename L> void header(L &&l) const {
    if (m_trailersOnly) {
      Terminal fields{m_status, m_trailers, *static_cast<const Impl *>(this)};
      fields.template header<Key>(ZuFwd<L>(l));
    }
  }
  template <typename L> void header(L &&l) const {
    MetaFields<> initial{metadata};
    initial.header(l);
    if (m_trailersOnly) {
      using Decl = WireCatalog<typename M::Trailers>;
      using List = Zhttp::HeaderList<Decl>;
      ZuUnroll::all<List::N>([&l](auto i) {
	using Key = typename List::template Key<i>;
	using Values = typename List::template Value<i>;
	if constexpr (Values::N) l(Key{}(), typename Zhttp::HeaderValue<Values>{}());
      });
      Terminal fields{m_status, m_trailers, *static_cast<const Impl *>(this)};
      fields.header(ZuFwd<L>(l));
    }
  }
  void body(Zhttp::BodyEmitter emit) {
    if (metadata.failed()) { Body::fail_(Code::INTERNAL); return; }
    Body::attach_(ZuMv(emit));
  }
  bool finish_(Status status = {}, Metadata trailers = Metadata{}) {
    if (m_trailersOnly || trailers.failed()) return Body::fail_(Code::INTERNAL);
    if (!Body::seal_(status.code == Code::OK)) return false;
    m_status = ZuMv(status); m_trailers = ZuMv(trailers); m_finishing = true;
    return Body::queuedMessages() ? Body::queued_() : productionDrained();
  }
  bool productionDrained() {
    if (!m_finishing) return true;
    m_finishing = false;
    Terminal fields{m_status, m_trailers, *static_cast<const Impl *>(this)};
    return Body::finish_(m_status.code, fields);
  }

private:
  Status m_status;
  Metadata m_trailers;
  bool m_trailersOnly = false, m_finishing = false;
};

// One framework callback context per call, shared by its three callbacks.
// Arbitrary intrusive receiver types need not derive from ZmPolymorph.
template <typename Receiver>
struct RxReceiver : ZmPolymorph, ZmNHeap<RxReceiver<Receiver>> {
friend inline ZuStringT<"Zgrpc.RxReceiver"> ZmNHeapID(RxReceiver *);
  explicit RxReceiver(ZmRef<Receiver> value) : object{ZuMv(value)} { }
  ZmRef<Receiver> object;
};

// The decoder callbacks retain their receiver independently of HTTP parser
// storage. Only the method-specific codec and cardinality are templated.
template <typename M, bool Response, bool Raw = false, typename Receiver>
ZmRef<RxInput> makeInput(ZmRef<Receiver> receiver, Limits limits,
    unsigned headerMax = Zhttp::DefltMaxHeaderSection)
{
  using Object = ZuIf<Response, typename M::Response, typename M::Request>;
  using Facet = ZuIf<Response, typename M::ResFacet, typename M::ReqFacet>;
  constexpr bool Streamed = Response ?
    M::Kind == Kind::ServerStream || M::Kind == Kind::Duplex :
    M::Kind == Kind::ClientStream || M::Kind == Kind::Duplex;
  constexpr bool Borrowed = Response ? M::ResBorrowed : M::ReqBorrowed;
  if (!receiver || !valid(limits)) return nullptr;
  if constexpr (M::MessageLimit) limits.maxMessageBytes = M::MessageLimit;
  ZmRef<RxInput> input = new RxInput{Response, Streamed, limits, headerMax};
  using Context = RxReceiver<Receiver>;
  ZmRef<Context> context = new Context{ZuMv(receiver)};
  input->callbacks(
    RxInput::FrameFn{RxInput::FrameFn::fn(context, [](Context *ctx, Frame frame) {
	auto r = ctx->object.ptr();
	if constexpr (Raw) return r->message(ZuMv(frame));
	else {
	  auto decoded = decodeFrame<Object, Facet, Borrowed>(frame);
	  if (!decoded.template is<Message<Object>>()) return false;
	  return r->message(ZuMv(decoded.template p<Message<Object>>()));
	}
    })},
    RxInput::AcceptFn{RxInput::AcceptFn::fn(context,
	[](Context *ctx, Zhttp::Channel *channel, RxInput &input) { ctx->object->accepted(channel, input); })},
    RxInput::EndFn{RxInput::EndFn::fn(ZuMv(context),
	[](Context *ctx, RxInput &input, bool ok) { ctx->object->finished(input, ok); })});
  return input;
}

template <typename Catalog>
class ParserCore : public Zhttp::Parser {
public:
  using HdrCatalog = Catalog;
  using Headers = Catalog::List;
  using Keys = Zhttp::HeaderList<Catalog>::Keys;

  RxInput *input() const { return m_input.ptr(); }
  void retainedBodyMax(uint64_t max) {
    m_retainedMax = max;
    if (m_input) m_input->retainedMax(max);
  }
  void status(unsigned code) { if (m_input) m_input->meta().httpStatus(code); }
  template <typename Key, typename Value> void header(Zhttp::FieldSection::T section) {
    if (m_input) m_input->meta().template header<Key, ZuTypeIndex<Key, Keys>{}>(section, Value{}());
  }
  template <typename Key> void header(Zhttp::FieldSection::T section, ZuSpan<uint8_t> value) {
    if (m_input) m_input->meta().template header<Key, ZuTypeIndex<Key, Keys>{}>(section, value);
  }
  void header(Zhttp::FieldSection::T section, ZuBSpan key, ZuSpan<uint8_t> value) {
    if (m_input) m_input->meta().header(section, key, value);
  }
  bool bodyInfo(Zhttp::Channel *channel, Zhttp::BodyType::T, uint64_t) {
    return m_input && m_input->accept(channel);
  }
  template <typename Rx> bool body(Rx &rx) { return m_input && m_input->body(rx); }
  void complete(Zhttp::Channel *channel, bool ok) {
    if (m_input) m_input->complete(channel, ok);
  }
  void reset() { m_input = nullptr; m_retainedMax = Default::MaxQueueBytes; }

protected:
  bool input_(ZmRef<RxInput> input) {
    m_input = ZuMv(input);
    if (m_input) m_input->retainedMax(m_retainedMax);
    return bool(m_input);
  }

private:
  ZmRef<RxInput> m_input;
  uint64_t m_retainedMax = Default::MaxQueueBytes;
};

template <typename Impl, typename M, bool Response, bool Raw = false>
class MessageParser : public ParserCore<WireCatalog<RxHeaders<M, Response>>> {
  using Base = ParserCore<WireCatalog<RxHeaders<M, Response>>>;
public:
  ZuAssert((MethodValid<M>{}));
  template <typename Receiver>
  bool init(ZmRef<Receiver> receiver, Limits limits = {},
      unsigned headerMax = Zhttp::DefltMaxHeaderSection) {
    return Base::input_(makeInput<M, Response, Raw>(ZuMv(receiver), limits, headerMax));
  }
  bool operation(Zhttp::Method::T method, Zhttp::Target &target) {
    return method == Zhttp::Method::POST && target.path == MethodPath<M>{}();
  }
};

template <typename Impl, typename M>
struct ReqParser : MessageParser<Impl, M, false> { };
template <typename Impl, typename M>
struct ResParser : MessageParser<Impl, M, true> { };
template <typename Impl, typename M>
struct RawReqParser : MessageParser<Impl, M, false, true> { };
template <typename Impl, typename M>
struct RawResParser : MessageParser<Impl, M, true, true> { };

} // Zgrpc

#endif /* ZgrpcHTTP_HH */
