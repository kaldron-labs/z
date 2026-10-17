//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z HTTP core types and parsing infrastructure

#ifndef ZhttpCore_HH
#define ZhttpCore_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <zlib/ZuAssert.hh>
#include <zlib/ZuBox.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuString.hh>
#include <zlib/ZuSwitch.hh>
#include <zlib/ZuTL.hh>
#include <zlib/ZuUnroll.hh>

#include <zlib/ZmFn.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZtEnum.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZiIP.hh>
#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiRxStream.hh>
#include <zlib/ZiTxStream.hh>

// Headers typelist definition.  Each key is followed by a value typelist;
// headers without fixed values use ZuTypeList<>.  For example:
// - keys (variable values):
//   ZhttpHeaders("content-type", "server");
// - keys + values:
//   ZhttpHeaders(("user-agent", "zhttp"), ("accept", "*/*"));
#define Zhttp_HdrValue(Value) ZuStringT<Value>
#define Zhttp_HdrValues_(...) \
  ZuPP_Eval__(ZuPP_MapComma(Zhttp_HdrValue,  __VA_ARGS__))
#define Zhttp_HdrValues(Values) \
  ZuPP_Defer(Zhttp_HdrValues_)(ZuPP_Strip(Values))
#define Zhttp_Header_1(Key) \
  ZuStringT<Key>, ZuTypeList<>
#define Zhttp_Header_2(Key, Values) \
  ZuStringT<Key>, ZuTypeList<Zhttp_HdrValues(Values)>
#define Zhttp_Header_N(_0, _1, Fn, ...) Fn
#define Zhttp_Header_(...) \
  Zhttp_Header_N(__VA_ARGS__, \
    Zhttp_Header_2(__VA_ARGS__), \
    Zhttp_Header_1(__VA_ARGS__))
#define Zhttp_Header(KV) \
  ZuPP_Defer(Zhttp_Header_)(ZuPP_Strip(KV))
#define ZhttpHeaders(...) \
  ZuTypeList<ZuPP_Eval_(ZuPP_MapComma(Zhttp_Header,  __VA_ARGS__))>

class ZiMultiplex;

namespace Zhttp {

ZuDerive(MessageString, ZtBArray<ZtArrayHeapID<"Zhttp.Message">>);


namespace Builder_ {

template <typename Write>
struct Writer {
  Write	&write;

  template <typename S>
  void print(S &s) const { write(s); }
  friend ZuPrintFn ZuPrintType(Writer *);
};

template <typename Write>
Writer<Write> writer(Write &write) { return {write}; }

} // namespace Builder_

constexpr unsigned DefltMaxBody = (1<<20);	// 1M default
constexpr unsigned DefltMaxStartLine = (1<<13);	// 8K
constexpr unsigned DefltMaxHeaderSection = (1<<16);	// 64K

using BodyBufAlloc = Zi::IOBufAlloc<ZiRxQueue::Node,
  ZiIOBuf_DefltSize, ZiIOBuf_DefltMaxSize, ZuStringT<"Zhttp.Body.Rx">>;

ZtEnumStruct(ZhttpAPI, Version, int8_t, H1, H2, H3);
ZtEnumStruct(ZhttpAPI, Transport, int8_t, TCP, TLS, QUIC);
ZuDerive(ALPNString, ZtString<ZtStringHeapID<"Zhttp.ALPN">>);

struct ConnectedInfo {
  ALPNString	alpn;
  uint32_t	version = 0;
  Transport::T	transport = Transport::TCP;
  Version::T	httpVersion = Version::H1;
  bool		secure = false;
  bool		multiplexed = false;
};

// Stable opaque identity for one native transport connection.  Multiplexed
// logical streams on the same connection expose the same value.
struct Session {
  uintptr_t	id = 0;
  Transport::T	transport = Transport::TCP;

  explicit operator bool() const { return id; }
  bool operator ==(const Session &v) const { return id == v.id; }
};



ZtEnumNS(ZhttpAPI, Method, int8_t,
  GET, POST, PUT, DELETE, PATCH, HEAD, OPTIONS, CONNECT, TRACE);

inline bool earlyDataSafeMethod(Method::T method)
{
  switch (method) {
    case Method::GET:
    case Method::HEAD:
    case Method::OPTIONS:
      return true;
    default:
      return false;
  }
}

inline bool idempotentMethod(Method::T method)
{
  switch (method) {
    case Method::GET:
    case Method::PUT:
    case Method::DELETE:
    case Method::HEAD:
    case Method::OPTIONS:
    case Method::TRACE:
      return true;
    default:
      return false;
  }
}

inline bool earlyDataSafeRequest(Method::T method, bool hasBody)
{
  return !hasBody && earlyDataSafeMethod(method);
}

ZtEnumStruct(ZhttpAPI, RequestErrorCode, int8_t,
  Malformed, ContentTooLarge, TargetTooLong, HeadersTooLarge,
  NotImplemented, VersionUnsupported, OperationRejected, BodyRejected);

ZtEnumStruct(ZhttpAPI, RequestErrorScope, int8_t,
  Request, Stream, Connection);

struct RequestError {
  RequestErrorCode::T code = RequestErrorCode::Malformed;
  RequestErrorScope::T scope = RequestErrorScope::Request;
  bool responsePossible = false;
};

struct BodyCommit {
  uint64_t	produced = 0;
  uint64_t	committed = 0;
  uint64_t	reset = 0;
  uint64_t	discarded = 0;
  bool		headers = false;
  bool		final = false;
};

ZtEnumNS(ZhttpAPI, BodyPolicy, int8_t,
  None, Fixed, OptionalFixed, Stream, OptionalStream);

ZtEnumStruct(ZhttpAPI, BodyType, int8_t, None, Streamed, Fixed);
ZtEnumStruct(ZhttpAPI, WriteOutcome, int8_t, End, Stream, Abort, Failed);

namespace BodyPolicy {

ZuInline bool hasBody(T v) { return v != None; }
ZuInline bool optional(T v) {
  return v == OptionalFixed || v == OptionalStream;
}
ZuInline bool streaming(T v) {
  return v == Stream || v == OptionalStream;
}

} // namespace BodyPolicy

template <typename HdrCatalog>
struct HeaderList {
  using Headers = HdrCatalog::List;
  ZuAssert(!(Headers::N & 1), "header list must contain key/value pairs");
  enum { N = Headers::N >> 1 };
  template <unsigned I> using Key = ZuType<I << 1, Headers>;
  template <unsigned I> using Value = ZuType<(I << 1) + 1, Headers>;
  using Keys = ZuTypeSlice<2, 0, Headers>;
  using Values = ZuTypeSlice<2, 1, Headers>;
};

// Compile-time field aggregation shared by application bindings.
template <typename K, typename ...Ts> struct HeaderValues_;
template <typename K>
struct HeaderValues_<K> { using T = ZuTypeList<>; };
template <typename K, typename K0, typename V0, typename ...Ts>
struct HeaderValues_<K, K0, V0, Ts...> {
  using Tail = HeaderValues_<K, Ts...>::T;
  using T = ZuTypeUnique<ZuIf<ZuIsSame<K, K0>{},
    typename V0::template Push<Tail>, Tail>>;
};
template <typename K, typename ...Ts>
struct HeaderValues_<K, ZuTypeList<Ts...>> : public HeaderValues_<K, Ts...> { };
template <typename K, typename List>
using HeaderValues = HeaderValues_<K, List>::T;

template <typename Hdrs, typename ...Keys> struct MergeHeaders_;
template <typename Hdrs>
struct MergeHeaders_<Hdrs> { using T = ZuTypeList<>; };
template <typename Hdrs, typename K0, typename ...Keys>
struct MergeHeaders_<Hdrs, K0, Keys...> {
  using T = ZuTypeList<K0, HeaderValues<K0, Hdrs>>::template Push<
    typename MergeHeaders_<Hdrs, Keys...>::T>;
};
template <typename Hdrs, typename ...Keys>
struct MergeHeaders_<Hdrs, ZuTypeList<Keys...>> :
  public MergeHeaders_<Hdrs, Keys...> { };
template <typename Headers>
using MergeHeaders = MergeHeaders_<Headers,
  ZuTypeUnique<ZuTypeSlice<2, 0, Headers>>>::T;

template <typename Values>
struct HeaderValue_ {
  ZuAssert(Values::N == 1,
    "builder header must have exactly one fixed value");
  using T = ZuType<0, Values>;
};
template <typename Values>
using HeaderValue = HeaderValue_<Values>::T;

ZhttpAPI unsigned requestErrorStatus(RequestErrorCode::T);

// A buffer printable remains borrowed through the synchronous destination call.
// The destination reserves its own final storage before invoking print.
struct BufferPrint {
  const void	*ctx;
  unsigned	size;
  unsigned	(*put)(const void *, char *, unsigned);
};

struct BufferPrintOps : ZuPrintBuffer {
  static unsigned length(const BufferPrint &v) { return v.size; }
  static unsigned print(char *p, unsigned n, const BufferPrint &v) {
    return v.put(v.ctx, p, n);
  }
};
inline BufferPrintOps ZuPrintType(BufferPrint *);

// Operations are synchronous on the destination's Tx shard. The underlying
// stream owns its buffers and sticky failure state, including flush errors.
struct WriterOps {
  void		(*append)(void *, ZuBSpan);
  void		(*print)(void *, const BufferPrint &);
  void		(*buffer)(void *, ZmRef<ZiIOBuf>);
  bool		(*flush)(void *);
  bool		(*failed)(const void *);
  uint64_t	(*produced)(const void *);
  unsigned	(*maxSize)(const void *);
  unsigned	(*headRoom)(const void *);
  unsigned	(*tailRoom)(const void *);
  ZmRef<ZiIOBuf> (*alloc)(void *, unsigned);
  ZuSpan<uint8_t> (*reserve)(void *, unsigned);
  void (*commit)(void *, unsigned);
};

class Writer {
public:
  Writer(void *ctx, const WriterOps &ops) : m_ctx{ctx}, m_ops{&ops} { }
  Writer(const Writer &) = delete;
  Writer &operator =(const Writer &) = delete;
  ~Writer() { (void)flush(); }

  bool flush() { m_span = {}; return m_ops->flush(m_ctx); }
  bool failed() const { return m_ops->failed(m_ctx); }
  uint64_t produced() const { return m_ops->produced(m_ctx); }
  unsigned maxSize() const { return m_ops->maxSize(m_ctx); }
  unsigned headRoom() const { return m_ops->headRoom(m_ctx); }
  unsigned tailRoom() const { return m_ops->tailRoom(m_ctx); }
  ZmRef<ZiIOBuf> allocBuf_(unsigned headRoom) {
    return m_ops->alloc(m_ctx, headRoom);
  }
  bool reserve(unsigned length) {
    m_span = m_ops->reserve(m_ctx, length);
    return !failed();
  }
  ZuSpan<uint8_t> span() const { return m_span; }
  void commit(unsigned length) {
    m_ops->commit(m_ctx, length);
    m_span = {};
  }
  bool operator !() const { return failed(); }
  ZuOpBool

  Writer &operator <<(ZuBSpan v) {
    m_span = {};
    m_ops->append(m_ctx, v);
    return *this;
  }
  Writer &operator <<(ZmRef<ZiIOBuf> buf) {
    m_span = {};
    m_ops->buffer(m_ctx, ZuMv(buf));
    return *this;
  }
  Writer &operator <<(Zi::Flush) { (void)flush(); return *this; }
  friend ZuTrue ZiIsTxStream_(Writer *);
  template <typename C, typename = ZuIfT<ZuEquiv<C, char>{}>>
  Writer &operator <<(C c) { return *this << ZuSpan{&c, 1}; }

  template <typename P, typename = ZuIfT<
    (ZuTraits<P>::IsPrimitive && ZuTraits<P>::IsReal && !ZuEquiv<P, char>{}) ||
    (ZuPrint<P>::OK && !ZuPrint<P>::String)>>
  Writer &operator <<(const P &v) {
    m_span = {};
    if (failed()) return *this;
    if constexpr (ZuTraits<P>::IsPrimitive && ZuTraits<P>::IsReal &&
	!ZuEquiv<P, char>{}) {
      return *this << ZuBoxed(v);
    } else if constexpr (ZuPrint<P>::Delegate) {
      ZuPrint<P>::print(*this, v);
    } else {
      m_ops->print(m_ctx, BufferPrint{
	&v, ZuPrint<P>::length(v),
	[](const void *ctx, char *p, unsigned n) {
	  return ZuPrint<P>::print(p, n, *static_cast<const P *>(ctx));
	}});
    }
    return *this;
  }

private:
  void		*m_ctx;
  const WriterOps *m_ops;
  ZuSpan<uint8_t> m_span;
};

// Used for temporary values/keys from a provider. The provider cannot return
// before the encoder consumes the view; no printable is rendered twice.
struct PrintView {
  ZuBSpan span;
  const void *ctx;
  void (*put)(const void *, Writer &);

  template <typename P>
  static PrintView borrow(const P &v) {
    if constexpr (ZuTraits<P>::IsSpan) {
      if constexpr (sizeof(typename ZuTraits<P>::Elem) == 1)
	return {ZuBSpan{v}, nullptr, nullptr};
    }
    if constexpr (ZuPrint<P>::String) return {ZuBSpan{v}, nullptr, nullptr};
    else return {{}, &v, [](const void *ctx, Writer &out) {
      out << *static_cast<const P *>(ctx);
    }};
  }
  bool isSpan() const { return !put; }
  void write(Writer &out) const {
    if (put) put(ctx, out);
    else out << span;
  }
};

struct RecordOps {
  void (*retain)(const void *, const void *);
  void (*release)(const void *, const void *);
  void (*move)(const void *, const void *, const void *);
};

// Embedded in the actual typed message. ctx is that allocation's correctly
// adjusted Node pointer, supplied by the typed queue/route adapter. This record
// owns no independent reference count and is never itself deleted.
struct MessageRecord {
  void *ctx = nullptr;
  const RecordOps *ops = nullptr;

  template <typename Node>
  void bind(Node *node) {
    static const RecordOps table{
      [](const void *ptr, const void *owner) {
	auto node = static_cast<const Node *>(ptr);
#ifdef ZmObject_DEBUG
	ZmRef__::ZmREF_(node, owner);
#else
	node->ref();
#endif
      },
      [](const void *ptr, const void *owner) {
	auto node = static_cast<const Node *>(ptr);
#ifdef ZmObject_DEBUG
	ZmRef__::ZmDEREF_(node, owner);
#else
	ZmRef__::ZmDEREF_(node);
#endif
      },
      [](const void *ptr, const void *from, const void *to) {
#ifdef ZmObject_DEBUG
	ZmRef__::ZmMVREF_(static_cast<const Node *>(ptr), from, to);
#endif
      }};
    ctx = node;
    ops = &table;
  }
};

// Small ownership adapter only; concrete algorithms use fixed record types.
// It mirrors existing intrusive reference transfer, including debug ownership.
template <typename Record>
class RecordRef {
public:
  RecordRef() = default;
  RecordRef(Record *record) : m_record{record} {
    if (m_record) m_record->ops->retain(m_record->ctx, this);
  }
  RecordRef(const RecordRef &other) : RecordRef{other.m_record} { }
  RecordRef(RecordRef &&other) : m_record{other.m_record} {
    if (m_record) m_record->ops->move(m_record->ctx, &other, this);
    other.m_record = nullptr;
  }
  ~RecordRef() { reset(); }
  RecordRef &operator =(const RecordRef &other) {
    RecordRef copy{other};
    return *this = ZuMv(copy);
  }
  RecordRef &operator =(RecordRef &&other) {
    if (this == &other) return *this;
    reset();
    m_record = other.m_record;
    if (m_record) m_record->ops->move(m_record->ctx, &other, this);
    other.m_record = nullptr;
    return *this;
  }
  template <typename Node>
  static RecordRef take(Record *record, ZmRef<Node> &&node) {
    RecordRef ref;
    ref.m_record = record;
    record->ops->move(record->ctx, &node, &ref);
    (void)ZuMv(node).release();
    return ref;
  }
  static RecordRef acquire(Record *record, const void *owner) {
    RecordRef ref;
    ref.m_record = record;
    if (record) record->ops->move(record->ctx, owner, &ref);
    return ref;
  }
  Record *release(const void *owner) {
    auto record = m_record;
    m_record = nullptr;
    if (record) record->ops->move(record->ctx, this, owner);
    return record;
  }
  Record *ptr() const { return m_record; }
  Record *operator ->() const { return m_record; }
  Record &operator *() const { return *m_record; }
  bool operator !() const { return !m_record; }
  ZuOpBool
  void reset() {
    if (!m_record) return;
    auto record = m_record;
    m_record = nullptr;
    record->ops->release(record->ctx, this);
  }

private:
  Record *m_record = nullptr;
};

struct FieldClass {
  int8_t	pseudo;
  bool		forbidden;
  int8_t	client;

  enum { None = -1, AltSvc, Connection, Location };
};

struct FieldMeta {
  ZuBSpan	name;
  ZuSpan<const ZuBSpan> values;
  const FieldClass *classification;
};

struct FieldName {
  int declared;
  FieldClass classification;
};


struct ZhttpAPI CatalogView {
  ZuSpan<const FieldMeta> fields;
  int		(*nameMatch)(ZuBSpan);
  int		(*valueMatch)(unsigned, ZuBSpan);
  int		(*fieldMatch)(ZuBSpan);
  ZuSpan<const int8_t> extra;

  FieldName match(ZuBSpan) const;
};

ZtEnumNS(ZhttpAPI, FieldForm, int8_t, Fixed, Name, Literal, Mutable);

struct FieldOffset {
  uint64_t offset = 0;
  unsigned length = 0;
};

// One synchronous field call. Declared names borrow catalog storage. Literal
// names and all printables are consumed before the originating provider returns.
// The encoder retains only mutable-field offsets, never printable contexts.
struct FieldOutput {
  void *ctx;
  FieldOffset (*put)(void *, FieldForm::T, int,
    const FieldClass *, PrintView, PrintView);

  FieldOffset field(FieldForm::T form, int id, const FieldClass *classification,
      PrintView key, PrintView value) {
    return put(ctx, form, id, classification, key, value);
  }
};

// The core invokes the provider synchronously while its encoder is active.
// Adapter-local patch spans live until the encoder resolves and patches them.
struct FieldProvider {
  void *ctx;
  void (*fields)(void *, FieldOutput &);
  void (*base)(void *, uint8_t *);
  void (*patch)(void *);
};

template <typename Write, typename Stream>
WriteOutcome::T invokeBodyWriter(Write &&write, Stream &stream) {
  return ZuFwd<Write>(write)(stream);
}

struct BodyWrite {
  const void *ctx;
  WriteOutcome::T (*put)(const void *, Writer &);

  template <typename Write>
  static BodyWrite borrow(Write &&write) {
    return {&write, [](const void *ctx, Writer &out) {
      return invokeBodyWriter(
	ZuFwd<Write>(*static_cast<ZuDeref<Write> *>(const_cast<void *>(ctx))), out);
    }};
  }
  WriteOutcome::T operator ()(Writer &out) const { return put(ctx, out); }
};

// The existing pooled producer owns this embedded record. Fixed-body turns
// instead borrow an adapter-local record for their synchronous callback.
struct BodyEmitRecord : MessageRecord {
  bool (*emit)(void *, BodyWrite);
};

class BodyEmitter {
public:
  BodyEmitter(BodyEmitRecord *emit) : m_emit{emit} { }

  template <typename Write>
  bool operator ()(Write &&write) {
    return m_emit->emit(m_emit->ctx, BodyWrite::borrow(ZuFwd<Write>(write)));
  }

private:
  RecordRef<BodyEmitRecord> m_emit;
};

ZtEnumStruct(ZhttpAPI, ResponseCode, int8_t, Submitted, Failed, Discarded);

struct ResponseResult {
  ResponseCode::T code = ResponseCode::Discarded;
  bool ok() const { return code == ResponseCode::Submitted; }
};

struct FieldContext {
  ZuBSpan altSvc;
  void *rejectCtx = nullptr;
  void (*reject)(void *) = nullptr;
  bool rejectContentLength = false;
  bool invalidHeader = false;
};

struct ResponseOps {
  unsigned (*status)(const void *);
  Method::T (*method)(const void *);
  BodyPolicy::T (*bodyPolicy)(const void *);
  bool (*streamResponse)(const void *);
  bool (*disconnect)(const void *);
  void (*body)(void *, BodyEmitter);
  void (*close)(void *);
  void (*completed)(void *, const ResponseResult &);
  void (*encode)(void *, FieldContext &, void *, void (*)(void *, FieldProvider &));
};

struct ResponseRecord : MessageRecord, ZmList_NodeExt<ResponseRecord> {
  using Ext = ZmList_NodeExt<ResponseRecord>;

  const ResponseOps *responseOps = nullptr;
  const CatalogView *catalog = nullptr;
  uint64_t sequence = 0;
  unsigned hdrBufSize = 0;
  unsigned hdrPlan = unsigned(-1);

  ResponseRecord &data() { return *this; }
  const ResponseRecord &data() const { return *this; }
  uint64_t routeSequence() const { return sequence; }
  void routeSequence(uint64_t value) { sequence = value; }
  unsigned status() const { return responseOps->status(ctx); }
  Method::T method() const { return responseOps->method(ctx); }
  BodyPolicy::T bodyPolicy() const { return responseOps->bodyPolicy(ctx); }
  bool streamResponse() const { return responseOps->streamResponse(ctx); }
  bool disconnect() const { return responseOps->disconnect(ctx); }
  void body(BodyEmitter emit) { responseOps->body(ctx, ZuMv(emit)); }
  void close() { responseOps->close(ctx); }
  void completed(const ResponseResult &result) {
    responseOps->completed(ctx, result);
  }
  void encode(FieldContext &fields, void *encoder,
      void (*put)(void *, FieldProvider &)) {
    responseOps->encode(ctx, fields, encoder, put);
  }
};

using ResponseRef = RecordRef<ResponseRecord>;



class Channel;
using Input = ZiRxStream<ZiRxQueue>;

// Borrowed framing callbacks owned by the application's stream/session.
using StreamCancelFn = ZmFn<void(), ZmFnHeapID<"Zhttp.StreamCancel">>;

struct FrameSink {
  void *ctx = nullptr;
  int (*process)(void *, Channel *, Input &) = nullptr;
  void (*peerEnd)(void *, Channel *) = nullptr;
  void (*error)(void *, Channel *) = nullptr;
};

struct ChannelOps {
  // Either shard: dispatch to the indicated owner before touching its state.
  ZiMultiplex *(*mx)(const void *);
  unsigned (*rxThread)(const void *);
  unsigned (*txThread)(const void *);
  void (*disconnect)(void *);
  void (*send)(void *, uint64_t, bool, ResponseRef);
  const CatalogView &(*responseCatalog)(const void *);
  // Rx: handshake capability/acceptance and buffered input handoff.
  bool (*streamEnable)(void *, bool);
  bool (*streamAccept)(void *);
  bool (*streamLocalCap)(void *);
  bool (*streamPeerCap)(void *);
  uint64_t (*sequence)(const void *);
  // Tx: the writer view remains borrowed until this synchronous call returns.
  bool (*streamWrite_)(void *, PrintView);
  void (*streamEnd)(void *);
  void (*streamReset)(void *);
  // Stable connection metadata; session identity is distinct from stream ID.
  Session (*session)(const void *);
  uint64_t (*streamID)(const void *);
  const ZiIP &(*remoteIP)(const void *);
  uint16_t (*remotePort)(const void *);
  // Tx-owned response cancellation; same hook and drain as retained producers.
  void (*responseCancel)(void *, StreamCancelFn);
  // Rx-owned HTTP/1 handshake metadata from the current wire parser.
  bool (*http10)(const void *);
  bool (*bodyFramed)(const void *);
  // Synchronous pooled-buffer production. The existing native application
  // stream transfers completed buffers to Tx; no printer context escapes.
  bool (*streamWrite)(void *, PrintView);
};

class Channel : public MessageRecord {
public:
  Channel(const ChannelOps &ops) : m_channelOps{&ops} { }

  Session session() const { return m_channelOps->session(ctx); }
  uint64_t streamID() const { return m_channelOps->streamID(ctx); }
  uint64_t requestSequence() const { return m_channelOps->sequence(ctx); }
  const ZiIP &remoteIP() const { return m_channelOps->remoteIP(ctx); }
  uint16_t remotePort() const { return m_channelOps->remotePort(ctx); }
  bool http10() const { return m_channelOps->http10(ctx); }
  bool bodyFramed() const { return m_channelOps->bodyFramed(ctx); }
  void responseCancel(StreamCancelFn fn) {
    m_channelOps->responseCancel(ctx, ZuMv(fn));
  }

  ZiMultiplex *mx() const { return m_channelOps->mx(ctx); }
  unsigned rxThread() const { return m_channelOps->rxThread(ctx); }
  unsigned txThread() const { return m_channelOps->txThread(ctx); }
  template <typename L> void rxRun(L &&);
  template <typename L> void txRun(L &&);
  void disconnect() { m_channelOps->disconnect(ctx); }
  void routeSend(uint64_t sequence, ResponseRef response) {
    m_channelOps->send(ctx, sequence, true, ZuMv(response));
  }
  template <typename Node> void send(ZmRef<Node>);
  const CatalogView &responseCatalog() const {
    return m_channelOps->responseCatalog(ctx);
  }
  void send(ResponseRef response) {
    m_channelOps->send(ctx, 0, false, ZuMv(response));
  }
  bool streamEnable(bool enabled = true) {
    return m_channelOps->streamEnable(ctx, enabled);
  }
  bool streamAccept(FrameSink sink = {}) {
    auto previous = m_frames;
    m_frames = sink;
    bool accepted = m_channelOps->streamAccept(ctx);
    if (!accepted) m_frames = previous;
    return accepted;
  }
  bool frameMode() const { return bool(m_frames.process); }
  int frameInput(Input &input) {
    return m_frames.process ? m_frames.process(m_frames.ctx, this, input) : 0;
  }
  void frameEnd() { if (m_frames.peerEnd) m_frames.peerEnd(m_frames.ctx, this); }
  void frameError() { if (m_frames.error) m_frames.error(m_frames.ctx, this); }
  bool streamLocalCap() const { return m_channelOps->streamLocalCap(ctx); }
  bool streamPeerCap() const { return m_channelOps->streamPeerCap(ctx); }
  void streamTxEnd() { m_channelOps->streamEnd(ctx); }
  void streamTxReset() { m_channelOps->streamReset(ctx); }

  template <typename Write>
  bool streamTx_(Write &&write) {
    auto printer = Builder_::writer(write);
    return m_channelOps->streamWrite_(ctx, PrintView::borrow(printer));
  }
  template <typename Write> bool streamTx(Write &&);

private:
  friend struct BoundReqParser;
  friend struct BoundResParser;
  void resetFrames() { m_frames = {}; }

  const ChannelOps *m_channelOps;
  FrameSink m_frames;
};

using ChannelRef = RecordRef<Channel>;

} // namespace Zhttp

#endif /* ZhttpCore_HH */
