//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZgrpcMulti.hh>

using namespace ZuTestUtil;
using namespace Zgrpc;

struct Packet { uint64_t seq = 0; ZuBSpan payload; };
ZfStruct(, (Packet, PB), (seq, (Ctor<0>, PB::FieldID<1>), UInt64),
  (payload, (Ctor<1>, PB::FieldID<2>), Bytes));
Zpb::AsMessage<"test.Packet"> Zpb_Fmt(Packet *);
struct Unary : Method {
  using Service = ZuStringT<"test.Matrix">;
  using Name = ZuStringT<"Unary">;
  using Request = Packet;
  using Response = Packet;
  using ReqHeaders = ZhttpHeaders(("x-mode", "first"));
  using ResHeaders = ZhttpHeaders(("x-tag", "initial"));
  using Trailers = ZhttpHeaders(("x-tag", "terminal"));
  enum { ReqBorrowed = 1, ResBorrowed = 1 };
};
struct Duplex : Unary { enum { Kind = Zgrpc::Kind::Duplex }; };

struct Other : Unary {
  using Name = ZuStringT<"Other">;
  using ReqHeaders = ZhttpHeaders(("x-mode", "other"), ("x-only", "two"));
  using ResHeaders = ZhttpHeaders(("x-tag", "other"));
};
using Methods = ZuTypeList<Unary, Other>;
ZgrpcCatalogDerive(Matrix, Methods);
ZgrpcCatalogImpl(Matrix);

struct Sink : ZiTxStream<Sink> {
  Sink() : ZiTxStream<Sink>{1U << 20, 8, 4} { }
  ZmRef<ZiIOBuf> allocBuf_(unsigned head) {
    ZmRef<ZiIOBuf> buf = new Zi::IOBufAlloc<ZiIOBuf, 32, 1U << 20,
      ZuStringT<"Zgrpc.HTTPTest">>{nullptr, 0};
    buf->skip = head;
    allocated = buf;
    return buf;
  }
  bool sendBuf_(ZmRef<ZiIOBuf> buf, bool) {
    if (reject) return false;
    data << buf->cspan();
    lastRange = buf;
    ++buffers;
    return true;
  }
  bool sendRange_(Zi::RxRange range) {
    if (reject) return false;
    data << range.cspan();
    lastRange = ZuMv(range.buf);
    ++buffers;
    return true;
  }
  Bytes data;
  ZmRef<ZiIOBuf> allocated;
  ZmRef<ZiIOBuf> lastRange;
  unsigned buffers = 0;
  bool reject = false;
};
static const Zhttp::WriterOps writerOps{
  [](void *p, ZuBSpan v) { *static_cast<Sink *>(p) << v; },
  [](void *p, const Zhttp::BufferPrint &v) { *static_cast<Sink *>(p) << v; },
  [](void *p, ZmRef<ZiIOBuf> v) { *static_cast<Sink *>(p) << ZuMv(v); },
  [](void *p) { return static_cast<Sink *>(p)->flush(); },
  [](const void *p) { return static_cast<const Sink *>(p)->failed(); },
  [](const void *p) -> uint64_t { return static_cast<const Sink *>(p)->data.length(); },
  [](const void *p) { return static_cast<const Sink *>(p)->maxSize(); },
  [](const void *p) { return static_cast<const Sink *>(p)->headRoom(); },
  [](const void *p) { return static_cast<const Sink *>(p)->tailRoom(); },
  [](void *p, unsigned n) { return static_cast<Sink *>(p)->allocBuf(n); },
  [](void *p, unsigned n) { return static_cast<Sink *>(p)->reserve(n); },
  [](void *p, unsigned n) { static_cast<Sink *>(p)->commit(n); },
  [](void *p, Zi::RxRange range) {
    return static_cast<Sink *>(p)->appendRange(ZuMv(range));
  },
  [](const void *p, const ZiIOBuf &buf, unsigned head, unsigned tail) {
    return static_cast<const Sink *>(p)->canAdopt_(buf, head, tail);
  },
  [](const void *p, const ZiIOBuf *buf) {
    return static_cast<const Sink *>(p)->allocated.ptr() == buf;
  },
  [](void *p, ZmRef<ZiIOBuf> buf) {
    static_cast<Sink *>(p)->allocated = ZuMv(buf);
    return true;
  }
};

struct Field { String name, value; bool never; };
using FieldArray = ZtArray<Field, ZtArrayHeapID<"Zgrpc.HTTPTest.Fields">>;
struct Emitter : ZmObject, ZmNHeap<Emitter> {
friend inline ZuStringT<"Zgrpc.HTTPTest.Emit"> ZmNHeapID(Emitter *);
  Emitter() {
    record.bind(this);
    record.emit = [](void *ptr, Zhttp::BodyWrite write) {
      auto self = static_cast<Emitter *>(ptr);
      if (!self->open) return false;
      Zhttp::Writer out{&self->sink, writerOps};
      auto result = write(out);
      self->outcomes.push(result);
      if (result != Zhttp::WriteOutcome::Stream) self->open = false;
      return !out.failed() && result != Zhttp::WriteOutcome::Failed;
    };
    record.trailers = [](void *ptr, const Zhttp::CatalogView &, Zhttp::FieldProvider &fields) {
      auto self = static_cast<Emitter *>(ptr);
      if (!self->open || self->rejectTrailers) return false;
      self->render(fields);
      self->open = false;
      return true;
    };
  }
  static String print(Zhttp::PrintView value) {
    Sink sink;
    Zhttp::Writer out{&sink, writerOps};
    value.write(out);
    out.flush();
    return String{sink.data};
  }
  void render(Zhttp::FieldProvider &fields) {
    Zhttp::FieldOutput out{this,
      [](void *ptr, Zhttp::FieldForm::T, int, const Zhttp::FieldClass *,
	  Zhttp::PrintView key, Zhttp::PrintView value, bool never) {
	auto self = static_cast<Emitter *>(ptr);
	self->fields.push(Field{print(key), print(value), never});
	return Zhttp::FieldOffset{};
      }};
    fields.fields(fields.ctx, out);
  }
  template <typename Builder> void initial(Builder &builder) {
    const auto &catalog = Zhttp::CatalogData<typename Builder::HdrCatalog>::configure();
    Zhttp::BuilderFields<Builder, false> adapter{&builder, &catalog};
    auto provider = adapter.provider();
    render(provider);
  }
  bool field(ZuCSpan name, ZuCSpan value, bool never = false) const {
    for (const auto &field : fields)
      if (field.name == name && field.value == value && field.never == never) return true;
    return false;
  }
  Zhttp::BodyEmitRecord record;
  Sink sink;
  Bytes outcomes;
  FieldArray fields;
  bool open = true, rejectTrailers = false;
};

struct Request : ReqBuilder<Request, Unary> {
  using Base = ReqBuilder<Request, Unary>;
  void producerStarted() { sent = send_(Packet{}); ended = end_(); }
  void productionFailed(Code::T) { ++failures; }
  void signMessage(ZuBSpan bytes) { ++signs; signedBytes = bytes.length(); }
  unsigned failures = 0, signs = 0, signedBytes = 1;
  bool sent = false, ended = false;
};
struct Response : ResBuilder<Response, Duplex> {
  using Base = ResBuilder<Response, Duplex>;
  using Base::productionFailed;
  void productionFailed(Code::T code) { failure = code; }
  bool neverIndex(ZuBSpan name) const { return name == "x-tag"; }
  Code::T failure = Code::OK;
};

static void request()
{
  ZuTestScope(request);
  ZmRef<Emitter> emit = new Emitter{};
  Request req;
  req.timeout = 123456789;
  req.metadata.add("x-runtime", "value");
  emit->initial(req);
  ZuCheck(emit->field("content-type", "application/grpc+proto"));
  ZuCheck(emit->field("te", "trailers") && emit->field("grpc-timeout", "123457u"));
  ZuCheck(emit->field("x-runtime", "value"));
  req.body(Zhttp::BodyEmitter{&emit->record});
  ZuCheck(req.sent && req.ended && !req.failures && req.signs == 1);
  ZuCheck(emit->sink.data.length() == PrefixSize + req.signedBytes);
  FrameRx framed;
  Frame frame;
  unsigned used;
  ZuCheck(framed.feed(emit->sink.data, nullptr, used, frame) == FrameResult::Message &&
    frame.bytes.length() == req.signedBytes && Zpb::verify<Packet>(frame.bytes));
  ZuCheck(!emit->open && !req.replayable() && !req.idempotent(Zhttp::Method::POST));
  ZtString<> path;
  req.operation([&path](auto method, auto &&operation) {
    if (method == Zhttp::Method::POST)
      operation([&path](auto &&write) { write(path); });
  });
  ZuCheck(path == "/test.Matrix/Unary");
  req.close();
  ZuCheck(!req.producerReady());
}

static void response()
{
  ZuTestScope(response);
  ZmRef<Emitter> emit = new Emitter{};
  Response res;
  emit->initial(res);
  ZuCheck(emit->field("x-tag", "initial", true));
  ZuCheck(!emit->field("x-tag", "terminal", true));
  res.body(Zhttp::BodyEmitter{&emit->record});
  ZuCheck(res.send_(Packet{1, {"first"}}) && res.sendRaw_({"opaque"}));
  const uint8_t details[] = {0, 255};
  Status status{Code::PERMISSION_DENIED, "denied %", Bytes{ZuBSpan{details}}};
  Metadata trailers;
  trailers.add("x-tag", "runtime");
  ZuCheck(res.finish_(ZuMv(status), ZuMv(trailers)));
  ZuCheck(!emit->open && emit->field("grpc-status", "7"));
  ZuCheck(emit->field("grpc-message", "denied %25"));
  ZuCheck(emit->field("grpc-status-details-bin", "AP8"));
  ZuCheck(emit->field("x-tag", "terminal", true) && emit->field("x-tag", "runtime", true));
  ZuCheck(!res.finish_() && !res.send_(Packet{2, {}}));
}

static void trailersOnly()
{
  ZuTestScope(trailersOnly);
  Response res;
  ZuCheck(!res.trailersOnly(Status{}));
  ZuCheck(res.trailersOnly(Status{Code::NOT_FOUND, "unknown"}));
  ZmRef<Emitter> emit = new Emitter{};
  emit->initial(res);
  ZuCheck(res.bodyPolicy() == Zhttp::BodyPolicy::None && !res.producerReady());
  ZuCheck(emit->field("grpc-status", "5") && emit->field("grpc-message", "unknown"));
  ZuCheck(emit->field("x-tag", "initial", true) && emit->field("x-tag", "terminal", true));
}

static void outputFailures()
{
  ZuTestScope(outputFailures);
  ZmRef<Emitter> emit = new Emitter{};
  Response res;
  res.body(Zhttp::BodyEmitter{&emit->record});
  emit->sink.reject = true;
  ZuCheck(!res.send_(Packet{1, {"body"}}));
  ZuCheck(res.failure == Code::INTERNAL && res.productionFailed());
  ZuCheck(!res.send_(Packet{2, {}}) && !res.finish_());
  ZmRef<Emitter> trailers = new Emitter{};
  Response other;
  other.body(Zhttp::BodyEmitter{&trailers->record});
  trailers->rejectTrailers = true;
  ZuCheck(!other.finish_() && other.failure == Code::INTERNAL);
}

struct QueuedRequest : ReqBuilder<QueuedRequest, Duplex> {
  unsigned queued = 0, signs = 0;
  Code::T failure = Code::OK;
  void producerQueued() { ++queued; }
  void signMessage(ZuBSpan) { ++signs; }
  void productionFailed(Code::T code) { failure = code; }
};
struct QueuedResponse : ResBuilder<QueuedResponse, Duplex> {
  void producerQueued() { }
  bool neverIndex(ZuBSpan name) const { return name == "x-tag"; }
};
static void queuedOutput()
{
  ZuTestScope(queuedOutput);
  Limits limits;
  limits.maxQueue = 2;
  limits.maxQueueBytes = 12;
  limits.workBatch = 1;
  ZmRef<Emitter> emit = new Emitter{};
  QueuedRequest req;
  req.limits(limits);
  req.body(Zhttp::BodyEmitter{&emit->record});
  ZuCheck(req.send_(Packet{1, {}}) && req.sendRaw_(ZuBSpan{}));
  ZuCheck(req.signs == 2 && req.queuedMessages() == 2 && req.queuedBytes() == 12);
  ZuCheck(!emit->sink.data.length() && emit->open);
  ZuCheck(req.end_() && emit->open);
  ZuCheck(req.drain_() && req.queuedMessages() == 1 && req.queuedBytes() == 5);
  ZuCheck(emit->open && emit->sink.data.length() == 7);
  ZuCheck(req.drain_() && !req.queuedMessages() && !req.queuedBytes());
  ZuCheck(req.signs == 2 && !emit->open && !req.producerReady());
  ZuCheck((emit->sink.data == ZuBSpan{"\0\0\0\0\2\10\1\0\0\0\0\0", 12}));

  ZmRef<Emitter> out = new Emitter{};
  QueuedResponse res;
  out->initial(res);
  res.body(Zhttp::BodyEmitter{&out->record});
  ZuCheck(res.send_(Packet{1, {"data"}}));
  {
    Status status{Code::PERMISSION_DENIED, "finished"};
    Metadata trailers;
    trailers.add("x-tag", "owned");
    ZuCheck(res.finish_(ZuMv(status), ZuMv(trailers)));
  }
  ZuCheck(out->open && !out->sink.data.length() && !out->field("grpc-status", "7"));
  ZuCheck(res.drain_() && !out->open && out->sink.data.length());
  ZuCheck(out->field("grpc-status", "7") && out->field("grpc-message", "finished"));
  ZuCheck(out->field("x-tag", "terminal", true) && out->field("x-tag", "owned", true));
}
static void queueRefusal()
{
  ZuTestScope(queueRefusal);
  Limits limits;
  limits.maxQueue = 1;
  ZmRef<Emitter> emit = new Emitter{};
  QueuedRequest req;
  req.limits(limits);
  req.body(Zhttp::BodyEmitter{&emit->record});
  ZuCheck(req.sendRaw_(ZuBSpan{}));
  ZuCheck(!req.sendRaw_(ZuBSpan{}) && req.failure == Code::RESOURCE_EXHAUSTED);
  ZuCheck(!req.queuedMessages() && !req.queuedBytes() && !emit->sink.data.length());
  ZuCheck(!req.end_() && req.productionCode() == Code::RESOURCE_EXHAUSTED);
  ZuCheck(req.failure == Code::RESOURCE_EXHAUSTED && !emit->open);

  limits.maxQueue = 2;
  limits.maxQueueBytes = 12;
  ZmRef<Emitter> bytes = new Emitter{};
  QueuedRequest other;
  other.limits(limits);
  other.body(Zhttp::BodyEmitter{&bytes->record});
  ZuCheck(other.sendRaw_(ZuBSpan{}));
  ZuCheck(!other.send_(Packet{1, {"data"}}));
  ZuCheck(other.failure == Code::RESOURCE_EXHAUSTED && other.signs == 1);
  ZuCheck(!other.queuedMessages() && !other.queuedBytes() && !bytes->sink.data.length());
  ZuCheck(!bytes->open);
}

struct Receiver : ZmObject, ZmNHeap<Receiver> {
friend inline ZuStringT<"Zgrpc.HTTPTest.Recv"> ZmNHeapID(Receiver *);
  bool message(Message<Packet> message) {
    ++messages;
    good &= message.object().seq == messages && message.object().payload == ZuBSpan{"data"} &&
      bool(message.backing);
    last = ZuMv(message);
    return good;
  }
  void accepted(Zhttp::Channel *, RxInput &) { ++accepts; }
  void finished(RxInput &input, bool ok) {
    good &= ok && input.code() == Code::OK;
    ++ends;
    done.post();
  }
  Decoded<Packet> last;
  unsigned messages = 0, ends = 0, accepts = 0;
  bool good = true;
  ZmSemaphore done;
};
struct Parser : ResParser<Parser, Duplex> { };

static void deferredReceive(bool advance)
{
  ZuTestScope(deferredReceive);
  ZmScheduler scheduler{ZmSchedParams{}.nThreads(1)};
  ZmRef<Receiver> receiver = new Receiver{};
  Parser parser;
  Limits limits; limits.workBatch = 1;
  ZuCheck(parser.init(receiver, limits));
  parser.input()->route({&scheduler, 1});
  parser.status(200);
  auto field = [&parser](auto section, ZuCSpan name, ZuCSpan value) {
    parser.header(section, name, ZuSpan<uint8_t>{const_cast<uint8_t *>(ZuBSpan{value}.data()), value.length()});
  };
  field(Zhttp::FieldSection::Final, "content-type", "application/grpc");
  ZuCheck(parser.bodyInfo(nullptr, Zhttp::BodyType::Streamed, 0));
  ZmRef<ZiRxBuf> coalesced = new ZiRxBufAlloc<>{nullptr, 0};
  coalesced->append(ZuBSpan{"prefix"});
  unsigned prefix = coalesced->length;
  const uint8_t *payload = nullptr;
  for (unsigned i = 1; i <= 3; ++i) {
    FrameError::T error;
    auto buf = encodeFrame(ZmRef<ZiIOBuf>{new Zi::IOBufAlloc<ZiIOBuf, ZiIOBuf_DefltSize,
      ZiIOBuf_DefltMaxSize, ZiIOBuf_HeapID>{nullptr, 0}}, 0, 100,
      Packet{i, {"data"}}, error);
    auto offset = coalesced->length;
    coalesced->append(buf->cspan());
    if (i == 3)
      payload = Zpb::handler<Packet>(ZuBSpan{
	coalesced->cspan().data() + offset + PrefixSize,
	buf->length - PrefixSize}).ctor().payload.data();
  }
  coalesced->advance(prefix);
  auto backing = coalesced;
  auto storage = backing->data_();
  auto size = backing->size;
  ZiRxStream<ZiRxQueue> incoming;
  incoming.push(ZuMv(coalesced));
  ZuCheck(parser.body(incoming) && incoming.empty());
  if (advance) backing->advance(backing->length);
  ZuCheck(backing->data_() == storage && backing->size == size);
  bool yielded = false;
  scheduler.run([receiver, &yielded]() { yielded = receiver->messages == 1 && !receiver->ends; }, 1);
  field(Zhttp::FieldSection::Trailers, "grpc-status", "0");
  parser.complete(nullptr, true);
  parser.reset(); // HTTP parser storage is gone before any decode turn runs
  ZuCheck(!parser.input());
  ZuCheck(scheduler.start(), (return));
  receiver->done.wait();
  ZuCheck(scheduler.stop());
  ZuCheck(yielded && receiver->good && receiver->messages == 3 && receiver->ends == 1);
  ZuCheck(receiver->last.is<Message<Packet>>() &&
    receiver->last.p<Message<Packet>>().object().payload == ZuBSpan{"data"});
  ZuCheck(receiver->last.is<Message<Packet>>() &&
    receiver->last.p<Message<Packet>>().backing.ptr() == backing.ptr() &&
    receiver->last.p<Message<Packet>>().object().payload.data() == payload);
}

struct MultiRequest : MReqBuilder<MultiRequest, Matrix> {
  template <typename M, typename B> void producerStarted(M *, B *b) {
    ok = b->send_(Packet{}) && b->end_();
  }
  bool neverIndex(ZuBSpan key) const { return key == "x-mode"; }
  bool ok = false;
};
struct MultiResponse : MResBuilder<MultiResponse, Matrix> {
  template <typename M, typename B> void producerStarted(M *, B *b) {
    ok = b->send_(Packet{}) && b->finish_();
  }
  bool ok = false;
};

static void multiBuilders()
{
  ZuTestScope(multiBuilders);
  MultiRequest request;
  auto &selected = request.init<Other>();
  selected.metadata.add("x-mode", "runtime");
  ZmRef<Emitter> emit = new Emitter{};
  emit->initial(request);
  ZuCheck(request.methodID() == 1);
  ZuCheck(emit->field("x-mode", "other", true) && emit->field("x-mode", "runtime", true));
  ZuCheck(!emit->field("x-mode", "first", true) && emit->field("x-only", "two"));
  request.body(Zhttp::BodyEmitter{&emit->record});
  ZuCheck(request.ok && !emit->open);
  MultiRequest first;
  first.init<Unary>();
  ZmRef<Emitter> initial = new Emitter{};
  initial->initial(first);
  ZuCheck(initial->field("x-mode", "first", true) && !initial->field("x-only", "two"));
  MultiResponse response;
  response.init<Other>();
  ZmRef<Emitter> output = new Emitter{};
  output->initial(response);
  ZuCheck(output->field("x-tag", "other") && !output->field("x-tag", "initial"));
  response.body(Zhttp::BodyEmitter{&output->record});
  ZuCheck(response.ok && output->field("grpc-status", "0") && !output->open);
}

struct MultiRequestParser : MReqParser<MultiRequestParser, Matrix> {
  ZmRef<Receiver> target;
  template <typename M> auto receiver(M *) { return target; }
};
struct MultiReplyParser : MResParser<MultiReplyParser, Matrix> { };

static void multiParsers()
{
  ZuTestScope(multiParsers);
  ZmRef<Receiver> receiver = new Receiver{};
  MultiRequestParser parser;
  parser.target = receiver;
  ZtString<> path = "/test.Matrix/Other";
  Zhttp::Target target;
  target.path = path.span();
  ZuCheck(parser.operation(Zhttp::Method::POST, target) && parser.input());
  uint8_t value[] = {'m'};
  parser.header<ZuStringT<"x-mode">>(Zhttp::FieldSection::Final, value);
  auto &meta = parser.input()->meta().metadata();
  using Keys = Zhttp::HeaderList<MultiRequestParser::HdrCatalog>::Keys;
  ZuCheck(meta.fields().length() == 1 && meta.fields()[0].key ==
    (ZuTypeIndex<ZuStringT<"x-mode">, Keys>{}));
  ZuCheck(meta.value(meta.fields()[0]) == ZuBSpan{"m"});
  parser.reset();
  path = "/test.Matrix/OtherX";
  target.path = path.span();
  ZuCheck(!parser.operation(Zhttp::Method::POST, target) && !parser.input());
  path = "/test.Matrix/Other";
  target.path = path.span();
  ZuCheck(!parser.operation(Zhttp::Method::GET, target));
  MultiReplyParser reply;
  ZuCheck(reply.init<Other>(receiver) && reply.input());
  reply.reset();
  ZuCheck(!reply.input());
}

struct Terminal : ZmPolymorph, ZmNHeap<Terminal> {
friend inline ZuStringT<"Zgrpc.HTTPTest.Terminal"> ZmNHeapID(Terminal *);
  unsigned frames = 0, ends = 0;
  Code::T code = Code::OK;
  bool ok = true;
  ZmSemaphore done;
};

static void failedInput(ZuBSpan wire, Code::T expected, bool overflow = false,
    bool response = false)
{
  ZuTestScopeRT(failedInput);
  ZmScheduler scheduler{ZmSchedParams{}.nThreads(1)};
  Limits limits;
  limits.maxMessageBytes = 2;
  limits.maxQueue = 1;
  ZmRef<RxInput> input = new RxInput{response, true, limits};
  ZmRef<Terminal> terminal = new Terminal{};
  input->route({&scheduler, 1});
  input->callbacks(RxInput::FrameFn{RxInput::FrameFn::fn(terminal,
    [](Terminal *self, Frame) { ++self->frames; return true; })}, {},
    RxInput::EndFn{RxInput::EndFn::fn(terminal,
      [](Terminal *self, RxInput &input, bool ok) {
	++self->ends; self->code = input.code(); self->ok = ok; self->done.post();
      })});
  using S = Zhttp::FieldSection::T;
  constexpr S initial = Zhttp::FieldSection::Final;
  input->meta().httpStatus(404);
  input->meta().header(initial, "content-type", "application/grpc");
  if (!response) input->meta().header(initial, "te", "trailers");
  ZuCheckRT(input->accept(nullptr));
  if (wire.length()) {
    ZmRef<ZiIOBuf> buf = new ZiRxBufAlloc<>{nullptr, 0};
    buf->append(wire);
    Zi::RxRange range{ZuMv(buf), 0, unsigned(wire.length())};
    ZuCheckRT(input->enqueue(range));
    if (overflow) ZuCheckRT(!input->enqueue(ZuMv(range)));
  }
  input->complete(nullptr, true);
  input->complete(nullptr, true);
  ZuCheckRT(scheduler.start(), (return));
  terminal->done.wait();
  ZuCheckRT(scheduler.stop());
  ZuCheckRT(input->finished() && input->failed() && !input->retainedBytes());
  ZuCheckRT(!terminal->ok && terminal->code == expected && terminal->ends == 1);
  ZuCheckRT(!terminal->frames);
  input->complete(nullptr, false);
  input->fail(Code::CANCELLED);
  ZuCheckRT(terminal->ends == 1 && input->code() == expected);
}

static void inputFailures()
{
  ZuTestScope(inputFailures);
  const uint8_t truncated[] = {0, 0, 0, 0, 2, 0};
  const uint8_t compressed[] = {1, 0, 0, 0, 0};
  const uint8_t tooLong[] = {0, 0, 0, 0, 3};
  const uint8_t partial[] = {0};
  ZuTestCall(failedInput, ZuBSpan{truncated}, Code::INTERNAL);
  ZuTestCall(failedInput, ZuBSpan{compressed}, Code::UNIMPLEMENTED);
  ZuTestCall(failedInput, ZuBSpan{tooLong}, Code::RESOURCE_EXHAUSTED);
  ZuTestCall(failedInput, ZuBSpan{partial}, Code::RESOURCE_EXHAUSTED, true);
  ZuTestCall(failedInput, ZuBSpan{}, Code::UNIMPLEMENTED, false, true);
}

// A failed HTTP completion may be the server disposing request input after
// selecting its response. Deferred cleanup must not reset that response.
static void completedInput()
{
  ZuTestScopeRT(completedInput);
  for (unsigned abort = 0; abort != 2; ++abort) {
    unsigned resets = 0, ends = 0;
    Zhttp::ChannelOps ops{};
    ops.mx = [](const void *) -> ZiMultiplex * { return nullptr; };
    ops.rxThread = [](const void *) -> unsigned { return 0; };
    ops.disconnect = [](void *ptr) { ++*static_cast<unsigned *>(ptr); };
    const Zhttp::RecordOps refs{
      [](const void *, const void *) { },
      [](const void *, const void *) { },
      [](const void *, const void *, const void *) { }};
    Zhttp::Channel channel{ops};
    channel.ctx = &resets;
    channel.ops = &refs;
    ZmRef<RxInput> input = new RxInput{false, true, Limits{}};
    input->meta().header(Zhttp::FieldSection::Final,
	"content-type", "application/grpc");
    input->meta().header(Zhttp::FieldSection::Final, "te", "trailers");
    input->callbacks({}, {}, [&ends](RxInput &, bool) { ++ends; });
    ZuCheckRT(input->accept(&channel));
    if (abort) input->fail(Code::CANCELLED);
    else input->complete(&channel, false);
    ZuCheckRT(input->finished() && input->failed() && ends == 1);
    ZuCheckRT(resets == abort);
    input->complete(&channel, false);
    ZuCheckRT(ends == 1 && resets == abort);
  }
}

struct CorruptReceiver : ZmObject, ZmNHeap<CorruptReceiver> {
friend inline ZuStringT<"Zgrpc.HTTPTest.Corrupt"> ZmNHeapID(CorruptReceiver *);
  unsigned messages = 0, ends = 0;
  Code::T code = Code::OK;
  bool ok = true;

  bool message(Message<Packet>) { ++messages; return true; }
  void accepted(Zhttp::Channel *, RxInput &) { }
  void finished(RxInput &input, bool ok_) {
    ++ends;
    code = input.code();
    ok = ok_;
  }
};

static void corruptInput()
{
  ZuTestScopeRT(corruptInput);
  // Complete gRPC envelopes with corrupt protobuf scalar and bytes fields.
  const uint8_t scalar[] = {0, 0, 0, 0, 2, 0x08, 0x80};
  const uint8_t bytes[] = {0, 0, 0, 0, 3, 0x12, 0x02, 'a'};
  for (unsigned i = 0; i != 2; ++i) {
    unsigned resets = 0;
    Zhttp::ChannelOps ops{};
    ops.mx = [](const void *) -> ZiMultiplex * { return nullptr; };
    ops.rxThread = [](const void *) -> unsigned { return 0; };
    ops.disconnect = [](void *ptr) { ++*static_cast<unsigned *>(ptr); };
    const Zhttp::RecordOps refs{
      [](const void *, const void *) { },
      [](const void *, const void *) { },
      [](const void *, const void *, const void *) { }};
    Zhttp::Channel channel{ops};
    channel.ctx = &resets;
    channel.ops = &refs;
    ZmRef<CorruptReceiver> receiver = new CorruptReceiver{};
    auto input = makeInput<Duplex, false>(receiver, Limits{});
    input->meta().header(Zhttp::FieldSection::Final,
	"content-type", "application/grpc");
    input->meta().header(Zhttp::FieldSection::Final, "te", "trailers");
    ZuCheckRT(input->accept(&channel));
    ZmRef<ZiIOBuf> buf = new Zi::IOBufAlloc<ZiIOBuf, ZiIOBuf_DefltSize,
      ZiIOBuf_DefltMaxSize, ZiIOBuf_HeapID>{nullptr, 0};
    buf->append(i ? ZuBSpan{bytes} : ZuBSpan{scalar});
    auto length = buf->length;
    ZuCheckRT(input->enqueue({ZuMv(buf), 0, length}));
    ZuCheckRT(input->finished() && input->failed() && !input->retainedBytes());
    ZuCheckRT(resets == 1 && receiver->ends == 1 && !receiver->ok &&
	receiver->code == Code::INTERNAL && !receiver->messages);
    input->complete(&channel, false);
    input->fail(Code::CANCELLED);
    ZuCheckRT(resets == 1 && receiver->ends == 1);
    ZuCheckRT(!input->enqueue({new Zi::IOBufAlloc<ZiIOBuf, ZiIOBuf_DefltSize,
      ZiIOBuf_DefltMaxSize, ZiIOBuf_HeapID>{nullptr, 0}, 0, 0}));
  }
}

static void ownedRawOutput()
{
  ZuTestScope(ownedRawOutput);
  Response res;
  ZmRef<Emitter> emit = new Emitter{};
  emit->initial(res);
  res.body(Zhttp::BodyEmitter{&emit->record});
  auto buf = emit->sink.allocBuf(emit->sink.headRoom() + PrefixSize);
  buf->ensure(3 + emit->sink.tailRoom());
  buf->append(ZuBSpan{"abc"});
  auto identity = buf.ptr();
  ZuCheck(res.sendRaw_(ZuMv(buf), true));
  ZuCheck(emit->sink.lastRange.ptr() == identity);
  FrameRx parser;
  Frame frame;
  unsigned used;
  ZuCheck(parser.feed(emit->sink.data, emit->sink.lastRange, used, frame) ==
    FrameResult::Message);
  ZuCheck(frame.bytes == ZuBSpan{"abc"});
  ZuCheck(emit->sink.lastRange->skip >= emit->sink.headRoom());
  ZuCheck(emit->sink.lastRange->avail() >= emit->sink.tailRoom());
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(request);
  ZuTestCall(response);
  ZuTestCall(ownedRawOutput);
  ZuTestCall(trailersOnly);
  ZuTestCall(outputFailures);
  ZuTestCall(queuedOutput);
  ZuTestCall(queueRefusal);
  ZuTestCall(deferredReceive, false);
  ZuTestCall(deferredReceive, true);
  ZuTestCall(inputFailures);
  ZuTestCall(completedInput);
  ZuTestCall(corruptInput);
  ZuTestCall(multiBuilders);
  ZuTestCall(multiParsers);
}
