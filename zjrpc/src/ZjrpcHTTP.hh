//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// JSON-RPC HTTP and SSE wire binding

#ifndef ZjrpcHTTP_HH
#define ZjrpcHTTP_HH

#ifndef ZjrpcLib_HH
#include <zlib/ZjrpcLib.hh>
#endif

#include <zlib/ZuObject.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZuRef.hh>
#include <zlib/ZuTraits.hh>

#include <zlib/ZmFn.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmPolymorph.hh>

#include <zlib/ZiIOBuf.hh>

#include <zlib/Zhttp.hh>
#include <zlib/ZhttpClient.hh>
#include <zlib/ZhttpServer.hh>

#include <zlib/Zjrpc.hh>
#include <zlib/ZjrpcDispatch.hh>
#include <zlib/ZjrpcIO.hh>
#include <zlib/ZjrpcPending.hh>

namespace Zjrpc {

ZuDerive(SSELine, (ZtString<ZtStringHeapID<"Zjrpc.SSE.Line">>));
ZuDerive(SSEID, (ZtString<ZtStringHeapID<"Zjrpc.SSE.ID">>));

struct SSEEvent {
  // The consumer may move/parse body in place; id is borrowed for the callback.
  ZmRef<ZiIOBuf> body;
  ZuCSpan id;
  int64_t retry = -1;
  ZuCSpan data() const { if (body) return body->cspan(); return {}; }
};

class SSEDecoder {
public:
  enum { Open, Closed };

  SSEDecoder() = default;
  SSEDecoder(unsigned maxLine, unsigned maxData) { reset(maxLine, maxData); }

  void reset(unsigned maxLine, unsigned maxData) {
    m_line.length_(0);
    reset_();
    m_maxLine = maxLine;
    m_maxData = maxData;
    m_state = Open;
  }

  int state() const { return m_state; }
  void close() {
    m_state = Closed;
    m_line.length_(0);
    reset_();
  }

  template <typename L>
  bool feed(ZuCSpan input, L &&l) {
    if (m_state == Closed) return false;
    unsigned n = input.length();
    unsigned begin = 0;
    for (unsigned i = 0; i < n; ++i) {
      if (input[i] != '\n') continue;
      auto line = ZuCSpan{input}.offset(begin).trunc(i - begin);
      if (m_line.length()) {
	if (!append_(line)) return false;
	line_(m_line, l);
      } else {
	if (line.length() > m_maxLine) { close(); return false; }
	line_(line, l);
      }
      m_line.length_(0);
      if (m_state == Closed) return false;
      begin = i + 1;
    }
    if (begin == n) return true;
    return append_(ZuCSpan(input.data() + begin, n - begin));
  }

private:
  bool append_(ZuCSpan span) {
    unsigned lineLength = m_line.length();
    if (ZuUnlikely(lineLength > m_maxLine ||
	span.length() > m_maxLine - lineLength)) {
      close();
      return false;
    }
    m_line << span;
    return true;
  }

  void reset_() {
    m_data = nullptr;
    m_id.length_(0);
    m_retry = -1;
  }

  template <typename L>
  void line_(ZuCSpan line, L &l) {
    line.chomp([](char c) { return c == '\r'; });
    if (!line) {
      if (m_data && m_data->length) l(SSEEvent{ZuMv(m_data), m_id, m_retry});
      reset_();
      return;
    }
    if (line[0] == ':') {
      return;
    }
    unsigned lineLength = line.length();
    unsigned colon = 0;
    while (colon < lineLength && line[colon] != ':') ++colon;
    ZuCSpan field{line.data(), colon};
    ZuCSpan value;
    if (colon < lineLength) {
      value = ZuCSpan(line.data() + colon + 1,
	  lineLength - colon - 1);
      if (value && value[0] == ' ') value.offset(1);
    }
    if (field == "data") {
      unsigned dataLength = m_data ? m_data->length : 0;
      unsigned valueLength = value.length();
      unsigned separator = bool(dataLength);
      if (ZuUnlikely(separator > m_maxData ||
	  valueLength > m_maxData - separator ||
	  dataLength > m_maxData - separator - valueLength)) {
	close();
	return;
      }
      if (!m_data) m_data = new InputBuf{};
      if ((dataLength && !m_data->append(ZuBSpan{"\n"})) || !m_data->append(value))
	close();
    } else if (field == "id") {
      m_id = value;
    } else if (field == "retry") {
      int64_t retry = ZuBox<int64_t>{value};
      if (retry >= 0) m_retry = retry;
    }
  }

  SSELine	m_line;
  ZmRef<ZiIOBuf>	m_data;
  SSEID		m_id;
  unsigned	m_maxLine = 0;
  unsigned	m_maxData = 0;
  int64_t	m_retry = -1;
  int		m_state = Open;
};

template <typename S>
inline void saveSSE(S &s, ZuCSpan id, int64_t retry, ZuCSpan json)
{
  if (id) s << "id: " << id << '\n';
  if (retry >= 0) s << "retry: " << retry << '\n';
  s << "data: " << json << "\n\n";
}

ZuDerive(HTTPValue, (ZtString<ZtStringHeapID<"Zjrpc.HTTP.Value">>));

template <typename U, typename = void>
struct AppHeaders_ { using T = ZuTypeList<>; };
template <typename U>
struct AppHeaders_<U, decltype(sizeof(typename U::Headers), void())> {
  using T = typename U::Headers;
};
template <typename U> using AppHeaders = typename AppHeaders_<U>::T;

struct HTTPHeader {
  ZuCSpan value;
  unsigned count = 0;

  explicit operator bool() const { return count; }
};

struct HTTPHeaderSlot {
  HTTPValue value;
  unsigned count = 0;
};

template <typename> using HTTPHeaderSlot_ = HTTPHeaderSlot;

template <typename Headers>
class HTTPHeaders {
  using Keys = ZuTypeSlice<2, 0, Headers>;
  using Slots = ZuTypeApply<ZuTuple, ZuTypeMap<HTTPHeaderSlot_, Keys>>;

public:
  template <typename Key>
  void put(ZuCSpan value) {
    auto &slot = m_slots.template p<ZuTypeIndex<Key, Keys>{}>();
    slot.value = value;
    ++slot.count;
  }

  template <typename Key>
  HTTPHeader get() const {
    const auto &slot = m_slots.template p<ZuTypeIndex<Key, Keys>{}>();
    return {slot.value, slot.count};
  }

private:
  Slots m_slots;
};

template <typename Out, typename Write>
Zhttp::WriteOutcome::T writeHTTP(Out &stream, uint64_t max,
    uint64_t &length, Write &&write)
{
  try {
    Output out{stream, max};
    write(out);
    out.flush();
    length = stream.produced();
    return out ? Zhttp::WriteOutcome::End : Zhttp::WriteOutcome::Abort;
  } catch (...) {
    return Zhttp::WriteOutcome::Abort;
  }
}

template <typename Headers_>
struct HTTPHdrCatalog {
  using List = Headers_;
  static int nameMatch(ZuBSpan key) {
    return Zhttp::Fields::nameMatch<HTTPHdrCatalog>(key);
  }
  static int valueMatch(unsigned key, ZuBSpan value) {
    return Zhttp::Fields::valueMatch<HTTPHdrCatalog>(key, value);
  }
};

template <typename Message>
struct HTTPRequestBuilder : public Zhttp::ReqBuilder {
  using Headers = ZhttpHeaders(
    ("content-type", "application/json"),
    ("accept", "application/json, text/event-stream"), "content-length");
  using HdrCatalog = HTTPHdrCatalog<Headers>;
  using ContentLength = ZuStringT<"content-length">;
  using Zhttp::ReqBuilder::header;

  Message message;
  // The owning client keeps its endpoint alive until queued requests drain.
  ZuCSpan endpoint;
  uint64_t sequence = 0;
  uint64_t maxBodyBytes = Default::MaxJSONBytes;
  mutable uint64_t bodyLength = 0;

  Zhttp::BodyPolicy::T bodyPolicy() const { return Zhttp::BodyPolicy::Fixed; }
  uint64_t key() const { return sequence; }

  template <typename L>
  void operation(L &&l) const {
    l(Zhttp::Method::POST, [this](auto &&emit) {
      emit([this](auto &out) { out << endpoint; });
    });
  }

  template <typename Key, typename L>
  void header(L &&l) const {
    if constexpr (ZuIsSame<Key, ContentLength>{}) l(Zhttp::contentLengthPad());
  }
  template <typename L> void header(L &&) const { }

  template <typename Emit>
  void body(Emit &&emit) const {
    emit([this](auto &stream) {
      return writeHTTP(stream, maxBodyBytes, bodyLength, [this](auto &out) {
	message.write(out);
      });
    });
  }
  template <typename L>
  void bodyHdrs(L &&l) const { Zhttp::contentLengthSet(l, bodyLength); }
};

template <typename Message>
struct HTTPFixedBuilder : public Zhttp::ResBuilder {
  using Headers = ZhttpHeaders(
    ("content-type", "application/json"), "content-length");
  using HdrCatalog = HTTPHdrCatalog<Headers>;
  using ContentLength = ZuStringT<"content-length">;
  using Zhttp::ResBuilder::header;

  Message message;
  uint64_t maxBodyBytes = Default::MaxJSONBytes;
  mutable uint64_t bodyLength = 0;

  Zhttp::BodyPolicy::T bodyPolicy() const {
    return Zhttp::BodyPolicy::Fixed;
  }
  Zhttp::Method::T method() const { return Zhttp::Method::POST; }
  unsigned status() const { return 200; }

  template <typename Key, typename L>
  void header(L &&l) const {
    if constexpr (ZuIsSame<Key, ContentLength>{})
      l(Zhttp::contentLengthPad());
  }
  template <typename L> void header(L &&) const { }

  template <typename Emit>
  void body(Emit &&emit) const {
    emit([this](auto &s) {
      return writeHTTP(s, maxBodyBytes, bodyLength, [this](auto &out) {
	message.write(out);
      });
    });
  }

  template <typename L>
  void bodyHdrs(L &&l) const {
    Zhttp::contentLengthSet(l, bodyLength);
  }
};

using SSEBuf = ZiIOBufAlloc<ZiIOBuf_DefltSize,
  ZiIOBuf_DefltMaxSize, "Zjrpc.HTTP.SSE">;

struct SSERecord {
  // Shared pooled I/O buffers cannot carry this builder's private queue link.
  ZmRef<ZiIOBuf> buf;
  bool terminal = false;
};

ZmListDerive(SSEQueue, SSERecord,
  ZmListNode<SSERecord, ZmListHeapID<"Zjrpc.SSE.Queue">>);

using ResumeFn = ZmFn<bool(), ZmFnHeapID<"Zjrpc.SSE.Resume">>;
using SSECloseFn = ZmFn<void(), ZmFnHeapID<"Zjrpc.SSE.Close">>;

struct WriteProbe {
  template <typename Out>
  Zhttp::WriteOutcome::T operator ()(Out &) const {
    return Zhttp::WriteOutcome::End;
  }
};

struct SSEProducerRef {
  void *ptr = nullptr;
  void (*invalidateFn)(void *) = nullptr;

  template <typename P> SSEProducerRef &operator =(P *p) {
    ptr = p;
    invalidateFn = [](void *ptr_) { static_cast<P *>(ptr_)->invalidate(); };
    return *this;
  }
  explicit operator bool() const { return ptr; }
  void invalidate() { if (ptr) invalidateFn(ptr); }
  void clear() { ptr = nullptr; invalidateFn = nullptr; }
};

namespace SSEState {
  enum { Open, TerminalQueued, Failed, Closed };
}

template <typename Owner, typename Emit, typename Heap = ZuVoid>
class SSEProducer_ : public Heap, public ZmPolymorph {
public:
  SSEProducer_(Owner *owner_, Emit emit_) :
    m_owner{owner_}, m_emit{ZuMv(emit_)} { }

  bool resume() {
    auto owner = m_owner;
    if (!owner) return false;
    return m_emit([owner](auto &out) { return owner->write_(out); });
  }
  void invalidate() { m_owner = nullptr; }

private:
  Owner	*m_owner;
  Emit	m_emit;
};

template <typename Owner, typename Emit>
ZuDerive(SSEProducerHeap,
  (ZmHeap<"Zjrpc.SSE.Producer", SSEProducer_<Owner, Emit>>));

template <typename Owner, typename Emit>
ZuDerive(SSEProducer, (SSEProducer_<Owner, Emit,
  SSEProducerHeap<Owner, Emit>>));

class HTTPSSEBuilder : public Zhttp::ResBuilder {
public:
  // These providers also serve MCP's fixed/SSE union builder, whose catalog
  // has runtime-valued fields. Declare names only so direct use emits once.
  using Headers = ZhttpHeaders("content-type", "cache-control");
  using HdrCatalog = HTTPHdrCatalog<Headers>;
  using ContentType = ZuStringT<"content-type">;
  using CacheControl = ZuStringT<"cache-control">;
  using Zhttp::ResBuilder::header;

  HTTPSSEBuilder() = default;
  explicit HTTPSSEBuilder(Limits limits_) : m_limits{limits_} { }

  template <typename Owner,
    typename = decltype(ZuDeclVal<Owner * &>()->postSSE_(),
	ZuDeclVal<Owner * &>()->discardSSE_(SSEQueue{}), void())>
  void owner(Owner *owner_, int) {
    m_owner = owner_;
    m_postFn = [](void *owner__) {
      return static_cast<Owner *>(owner__)->postSSE_();
    };
    m_discardFn = [](void *owner__, SSEQueue queue) {
      return static_cast<Owner *>(owner__)->discardSSE_(ZuMv(queue));
    };
  }
  template <typename Owner>
  void owner(Owner *, ...) { }

  void disown() {
    m_owner = nullptr;
    m_postFn = nullptr;
    m_discardFn = nullptr;
  }

  int state() const { return m_state; }
  unsigned queued() const { return m_queued; }
  uint64_t queuedBytes() const { return m_queuedBytes; }

  Zhttp::BodyPolicy::T bodyPolicy() const {
    return Zhttp::BodyPolicy::Stream;
  }
  Zhttp::Method::T method() const { return Zhttp::Method::POST; }
  unsigned status() const { return 200; }
  bool disconnect() const { return m_state == SSEState::Failed; }

  template <typename Key, typename L>
  void header(L &&l) const {
    if constexpr (ZuIsSame<Key, ContentType>{})
      l("text/event-stream");
    else if constexpr (ZuIsSame<Key, CacheControl>{})
      l("no-cache");

  }
  template <typename L> void header(L &&) const { }

  template <typename M>
  bool emit(M message, bool terminal = true) {
    return enqueue_(ZuMv(message), terminal);
  }

  template <typename Emit>
  void body(Emit &&emit_) {
    using Emit_ = ZuDecay<Emit>;
    using Producer = SSEProducer<HTTPSSEBuilder, Emit_>;
    ZmRef<Producer> producer = new Producer{this, ZuFwd<Emit>(emit_)};
    m_producer = producer.ptr();
    m_resume = ResumeFn::fn(
      ZuMv(producer), [](Producer *producer_) {
	return producer_->resume();
      });
    if (!m_owner) {
      do {
	auto resume = m_resume;
	if (!resume || !resume()) {
	  fail_();
	  break;
	}
      } while (m_queued && m_state != SSEState::Closed);
      return;
    }
    {
      auto resume = m_resume;
      if (!resume || !resume()) {
	fail_();
	return;
      }
    }
    if (m_queued && m_state != SSEState::Closed) post_();
  }

  void resume_() {
    m_posted = false;
    if (!m_resume || m_state == SSEState::Closed) return;
    unsigned visited = 0;
    do {
      auto resume = m_resume;
      if (!resume || !resume()) {
	fail_();
	return;
      }
      ++visited;
    } while (visited < m_limits.workBatch && m_queued &&
	m_state != SSEState::Closed);
    if (m_queued && m_state != SSEState::Closed) post_();
  }

  void close() {
    m_producer.invalidate();
    m_producer.clear();
    m_resume = {};
    SSEQueue queue{ZuMv(m_queue)};
    m_queued = 0;
    m_queuedBytes = 0;
    m_state = SSEState::Closed;
    bool discarded = m_owner && m_discardFn &&
      m_discardFn(m_owner, ZuMv(queue));
    disown();
    if (!discarded)
      while (queue.shift()) { }
  }

  template <typename Out>
  Zhttp::WriteOutcome::T write_(Out &out) {
    if (m_state == SSEState::Failed)
      return Zhttp::WriteOutcome::Abort;
    auto record = m_queue.shift();
    if (!record) {
      return Zhttp::WriteOutcome::Stream;
    }
    --m_queued;
    m_queuedBytes -= record->buf->length;
    out << ZuCSpan{*record->buf};
    return record->terminal ?
      Zhttp::WriteOutcome::End : Zhttp::WriteOutcome::Stream;
  }

private:
  template <typename M>
  bool enqueue_(M message, bool terminal) {
    if (m_state != SSEState::Open) return false;
    if (m_queued >= m_limits.maxQueue) {
      abort_();
      return false;
    }
    ZmRef<ZiIOBuf> buf = new SSEBuf{};
    *buf << "id: " << m_sequence++ << "\ndata: ";
    unsigned jsonOffset = buf->length;
    unsigned jsonMax = m_limits.maxJSONBytes < m_limits.maxSSEEventBytes ?
      m_limits.maxJSONBytes : m_limits.maxSSEEventBytes;
    uint64_t outputMax = uint64_t(jsonOffset) + jsonMax;
    if (ZuUnlikely(outputMax > UINT_MAX)) {
      abort_();
      return false;
    }
    BufOutput out{*buf, outputMax};
    message.write(out);
    if (ZuUnlikely(!out)) {
      abort_();
      return false;
    }
    unsigned jsonLength = buf->length - jsonOffset;
    *buf << "\n\n";
    if (ZuUnlikely(buf->failed() || jsonLength > jsonMax ||
	m_queuedBytes > m_limits.maxQueueBytes ||
	buf->length > m_limits.maxQueueBytes - m_queuedBytes)) {
      abort_();
      return false;
    }
    m_queuedBytes += buf->length;
    ++m_queued;
    m_queue.push(SSERecord{ZuMv(buf), terminal});
    if (terminal) m_state = SSEState::TerminalQueued;
    if (m_resume &&
	(m_owner ? !post_() : !m_resume())) {
      fail_();
      return false;
    }
    return true;
  }

  void fail_() {
    if (m_state != SSEState::Closed)
      m_state = SSEState::Failed;
  }

  void abort_() {
    fail_();
    if (m_resume) {
      if (m_owner)
	(void)post_();
      else
	(void)m_resume();
    }
  }

  bool post_() {
    if (m_posted) return true;
    if (!m_owner || !m_postFn) return false;
    m_posted = true;
    if (m_postFn(m_owner)) return true;
    m_posted = false;
    return false;
  }

  Limits		m_limits;
  SSEQueue	m_queue;
  ResumeFn	m_resume;
  void		*m_owner = nullptr;
  bool		(*m_postFn)(void *) = nullptr;
  bool		(*m_discardFn)(void *, SSEQueue) = nullptr;
  SSEProducerRef m_producer;
  uint64_t	m_sequence = 1;
  uint64_t	m_queuedBytes = 0;
  unsigned	m_queued = 0;
  int		m_state = SSEState::Open;
  bool		m_posted = false;
};

namespace HTTPState {
  enum { Empty, Receiving, Accepted, OverLimit };
}

template <typename Impl, typename ExtraHeaders = ZuTypeList<>>
class HTTPRequestParser : public Zhttp::Parser {
  using AppKeys = ZuTypeSlice<2, 0, ExtraHeaders>;
public:
  using Headers = ZuTypeConcat<ZhttpHeaders(
    ("content-type", "application/json")), ExtraHeaders>;
  using HdrCatalog = HTTPHdrCatalog<Headers>;
  using Keys = ZuTypeSlice<2, 0, Headers>;
  ZuAssert(Keys::N == ZuTypeUnique<Keys>::N);

  auto impl() { return static_cast<Impl *>(this); }

  bool operation(Zhttp::Method::T method, Zhttp::Target &target) {
    return method == Zhttp::Method::POST && target.path == impl()->endpoint();
  }

  template <typename Key, typename Value>
  void header(Zhttp::FieldSection::T) {
    if constexpr (ZuTypeIn<Key, AppKeys>{}) m_headers.template put<Key>(Value{}());
  }
  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuSpan<uint8_t> value) {
    if constexpr (ZuTypeIn<Key, AppKeys>{}) m_headers.template put<Key>(value);
  }
  void header(Zhttp::FieldSection::T, ZuBSpan, ZuSpan<uint8_t>) { }

  bool bodyInfo(Zhttp::BodyType::T, uint64_t length) {
    m_state = m_input.begin(length, impl()->limits().maxJSONBytes) ?
      HTTPState::Receiving : HTTPState::OverLimit;
    return m_state == HTTPState::Receiving;
  }
  template <typename Rx>
  bool body(Rx &rx) {
    if (m_state != HTTPState::Receiving) return false;
    if (!m_input.body(rx)) m_state = HTTPState::OverLimit;
    return m_state == HTTPState::Receiving;
  }
  template <typename Link>
  void complete(Link *link, bool ok) {
    if (m_state == HTTPState::OverLimit) {
      impl()->corruptHTTP(link);
      return;
    }
    if (ok) impl()->receiveHTTP(link, m_input.buffer(), ZuMv(m_headers));
  }
  void reset() {
    m_input.reset();
    m_headers = {};
    m_state = HTTPState::Empty;
  }

private:
  using App = HTTPHeaders<ExtraHeaders>;

  Input	m_input;
  App		m_headers;
  int		m_state = HTTPState::Empty;
};

template <typename Impl, typename Meta = EmptyObject,
  typename ExtraHeaders = ZuTypeList<>>
class HTTPResponseParser : public Zhttp::Parser {
public:
  using Headers = ZuTypeConcat<ZhttpHeaders(
    ("content-type", ("application/json", "text/event-stream"))),
    ExtraHeaders>;
  using HdrCatalog = HTTPHdrCatalog<Headers>;
  using ContentType = ZuStringT<"content-type">;
  using JSONContent = ZuStringT<"application/json">;
  using SSEContent = ZuStringT<"text/event-stream">;

  auto impl() { return static_cast<Impl *>(this); }
  auto impl() const { return static_cast<const Impl *>(this); }

  void streaming(bool v) { m_streaming = v; }
  void status(unsigned status) { m_status = status; }

  template <typename Key, typename Value>
  void header(Zhttp::FieldSection::T) {
    if constexpr (ZuIsSame<Key, ContentType>{}) {
      if constexpr (ZuIsSame<Value, JSONContent>{})
	m_streaming = false;
      else if constexpr (ZuIsSame<Value, SSEContent>{})
	m_streaming = true;
    }
  }

  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuSpan<uint8_t>) { }

  Meta &meta() { return m_meta; }
  const Meta &meta() const { return m_meta; }
  bool acceptedStatus(unsigned) const { return false; }
  template <typename Link>
  void acceptedHTTPResponse(Link *, unsigned, const Meta &) { }

  void header(Zhttp::FieldSection::T, ZuBSpan, ZuSpan<uint8_t>) { }

  bool bodyInfo(Zhttp::BodyType::T, uint64_t length) {
    if (impl()->acceptedStatus(m_status) && !length) {
      m_state = HTTPState::Accepted;
      return true;
    }
    m_state = HTTPState::Receiving;
    if (m_streaming) {
      m_sse.reset(
	impl()->limits().maxSSELineBytes,
	impl()->limits().maxSSEEventBytes);
      return true;
    }
    if (!m_input.begin(length, impl()->limits().maxJSONBytes)) {
      m_state = HTTPState::OverLimit;
      return false;
    }
    return true;
  }

  template <typename Rx>
  bool body(Rx &rx) {
    if (m_state != HTTPState::Receiving) return false;
    if (!m_streaming) {
      if (!m_input.body(rx)) m_state = HTTPState::OverLimit;
      return m_state == HTTPState::Receiving;
    }
    rx.consume([](ZuSpan<uint8_t> span) -> int64_t { return span.length(); },
      [this](ZuSpan<uint8_t> span) {
	bool ok = m_sse.feed(span, [this](SSEEvent event) {
	  if (impl()->receiveSSE(ZuMv(event), m_meta)) return;
	  m_state = HTTPState::OverLimit;
	  m_sse.close();
	});
	if (!ok) m_state = HTTPState::OverLimit;
      });
    return m_state == HTTPState::Receiving;
  }

  template <typename Link>
  void complete(Link *link, bool ok) {
    if (m_state == HTTPState::OverLimit) {
      impl()->corruptHTTPResponse(link);
      return;
    }
    if (!ok) {
      impl()->failedHTTPResponse(link);
      return;
    }
    if (impl()->acceptedStatus(m_status) &&
	(m_state == HTTPState::Empty ||
	  m_state == HTTPState::Accepted)) {
      impl()->acceptedHTTPResponse(link, m_status, m_meta);
      return;
    }
    if (m_streaming)
      impl()->completeHTTPResponse(link, m_status, m_meta);
    else
      impl()->receiveHTTPResponse(link, m_status, m_input.buffer(), m_meta);
  }

  void reset() {
    m_input.reset();
    m_meta = {};
    m_status = 0;
    m_streaming = false;
    m_state = HTTPState::Empty;
    m_sse.close();
  }

private:
  Input	m_input;
  Meta		m_meta;
  SSEDecoder	m_sse;
  unsigned	m_status = 0;
  bool		m_streaming = false;
  int		m_state = HTTPState::Empty;

};

// An optional application-owned identity spanning HTTP request streams. Creation
// copies Server configuration; individual peers have no configuration setters.
template <typename Heap = ZuVoid>
class HTTPPeer_ : public Heap, public ZuObject {
public:
  template <typename Server>
  static ZuRef<HTTPPeer_> create(const Server &server) {
    return new HTTPPeer_{server.histSize()};
  }
  InboundCalls *inbound() { return &m_inbound; }
  bool close(unsigned workBatch) { return m_inbound.close(workBatch); }
private:
  explicit HTTPPeer_(unsigned histSize) : m_inbound{histSize} { }
  InboundCalls m_inbound;
};
using HTTPPeerHeap = ZmHeap<"Zjrpc.HTTP.Peer", HTTPPeer_<>>;
using HTTPPeer = HTTPPeer_<HTTPPeerHeap>;

template <typename Headers>
struct HTTPContext {
  HTTPHeaders<Headers> headers;
  ZuRef<HTTPPeer> peer;
};

template <typename Impl, typename Headers,
  typename = decltype(ZuDeclVal<Impl *>()->peer(ZuDeclVal<const Headers &>()), void())>
auto httpPeer(Impl *impl, const Headers &headers, int)
{
  return impl->peer(headers);
}
template <typename Impl, typename Headers>
ZuRef<HTTPPeer> httpPeer(Impl *, const Headers &, long) { return {}; }

class ServerConfig : public Zhttp::ServerConfig {
public:
  ZuCSpan endpoint() const { return m_endpoint; }
  const Limits &limits() const { return m_limits; }
  unsigned histSize() const { return m_histSize; }
  Zhttp::ServerConfig &http() { return *this; }
  const Zhttp::ServerConfig &http() const { return *this; }
  template <typename L>
  ServerConfig &&http(L &&l) {
    ZuFwd<L>(l)(static_cast<Zhttp::ServerConfig &>(*this));
    return ZuMv(*this);
  }
  ServerConfig &endpoint(ZuCSpan value) { m_endpoint = value; return *this; }
  ServerConfig &limits(Limits value) { m_limits = value; return *this; }
  ServerConfig &histSize(unsigned value) { m_histSize = value; return *this; }
private:
  HTTPValue m_endpoint;
  Limits m_limits;
  unsigned m_histSize = Default::HistSize;
};

class ClientConfig : public Zhttp::Config {
public:
  ZuCSpan endpoint() const { return m_endpoint; }
  const Limits &limits() const { return m_limits; }
  Zhttp::Config &http() { return *this; }
  const Zhttp::Config &http() const { return *this; }
  template <typename L>
  ClientConfig &&http(L &&l) {
    ZuFwd<L>(l)(static_cast<Zhttp::Config &>(*this));
    return ZuMv(*this);
  }
  ClientConfig &endpoint(ZuCSpan value) { m_endpoint = value; return *this; }
  ClientConfig &limits(Limits value) { m_limits = value; return *this; }
private:
  HTTPValue m_endpoint;
  Limits m_limits;
};

template <typename List>
using DeclaredHeaders = Zhttp::MergeHeaders<
  ZuTypeApply<ZuTypeConcat, ZuTypeMap<AppHeaders, List>>>;

template <typename Catalog>
using AllResponses = ZuTypeUnique<ZuTypeApply<ZuTypeConcat,
  ZuTypeMap<GetResponses, Catalog>>>;

template <typename Catalog, typename Impl>
using HTTPReqHeaders = Zhttp::MergeHeaders<
  ZuTypeConcat<DeclaredHeaders<Catalog>, AppHeaders<Impl>>>;

template <typename Catalog, typename Impl>
using HTTPResHeaders = Zhttp::MergeHeaders<
  ZuTypeConcat<DeclaredHeaders<AllResponses<Catalog>>, AppHeaders<Impl>>>;

// Fixed header declarations are selected by the concrete message type. Dynamic
// declared fields call the app's typed provider; undeclared fields are omitted.
template <typename Key, typename Value, typename Message, typename L>
void fixedHeader(const Message &, L &&l)
{
  using Values = Zhttp::HeaderValues<Key, AppHeaders<Message>>;
  if constexpr (ZuTypeIn<Value, Values>{}) l();
}

template <typename Key, typename Impl, typename Res, typename L>
void responseHeader(Impl *impl, const ResultMessage<Res> &message, L &&l)
{
  using Headers = AppHeaders<Res>;
  using Keys = ZuTypeSlice<2, 0, Headers>;
  using Values = Zhttp::HeaderValues<Key, Headers>;
  if constexpr (ZuTypeIn<Key, Keys>{} && !Values::N)
    impl->template responseHeader<Res, Key>(message.reply, ZuFwd<L>(l));
}
template <typename Key, typename Impl, typename Message, typename L>
void responseHeader(Impl *, const Message &, L &&) { }

template <typename Key, typename Impl, typename Req, typename L>
void requestHeader(Impl *impl, const RequestMessage<Req> &message, L &&l)
{
  using Headers = AppHeaders<Req>;
  using Keys = ZuTypeSlice<2, 0, Headers>;
  using Values = Zhttp::HeaderValues<Key, Headers>;
  if constexpr (ZuTypeIn<Key, Keys>{} && !Values::N)
    impl->template requestHeader<Req, Key>(message.object, ZuFwd<L>(l));
}

template <typename Key, typename Impl, typename Message, typename L>
void requestHeader(Impl *, const Message &, L &&) { }

template <typename Message>
struct MessageView {
  const Message &message;
  template <typename S> void write(S &out) const { message.write(out); }
};

template <typename Impl, typename Catalog>
class HTTPResponseData : public ZmObject, public Zhttp::ResBuilder {
  using Message = ServerMessage<Catalog>;
  using Fixed = HTTPFixedBuilder<Message>;
  struct SSE : public HTTPSSEBuilder {
    Message message;
    SSE(Limits limits, Message message_) :
      HTTPSSEBuilder{limits}, message{ZuMv(message_)} { }
  };
  using Builder = ZuUnion<void, Fixed, SSE>;
  using ContentType = ZuStringT<"content-type">;
  using ContentLength = ZuStringT<"content-length">;
  using CacheControl = ZuStringT<"cache-control">;
  using Protocol = ZhttpHeaders("content-type", "content-length", "cache-control");
  using Extra = HTTPResHeaders<Catalog, Impl>;
  using ProtocolKeys = ZuTypeSlice<2, 0, Protocol>;
  using ExtraKeys = ZuTypeSlice<2, 0, Extra>;
  using AppKeys = ZuTypeSlice<2, 0, AppHeaders<Impl>>;
  using AppAndProtocol = ZuTypeConcat<ProtocolKeys, ExtraKeys>;
  ZuAssert(AppAndProtocol::N == ZuTypeUnique<AppAndProtocol>::N);
public:
  using Headers = ZuTypeConcat<Protocol, Extra>;
  using HdrCatalog = HTTPHdrCatalog<Headers>;
  using Zhttp::ResBuilder::header;

  ~HTTPResponseData() { close(); }
  bool init(Impl *impl, Message message, int policy, const Limits &limits) {
    m_impl = impl;
    if (message.empty()) return true;
    if (policy == BodyPolicy::SSE) {
      auto builder = new (m_builder.template new_<SSE>()) SSE{limits, ZuMv(message)};
      return builder->emit(MessageView<Message>{builder->message});
    }
    new (m_builder.template new_<Fixed>()) Fixed{
      .message = ZuMv(message), .maxBodyBytes = limits.maxJSONBytes};
    return true;
  }
  unsigned status() const { return 200; }
  Zhttp::Method::T method() const { return Zhttp::Method::POST; }
  Zhttp::BodyPolicy::T bodyPolicy() const {
    if (!m_builder.type()) return Zhttp::BodyPolicy::None;
    return m_builder.cdispatch([](auto, const auto &builder) { return builder.bodyPolicy(); });
  }
  bool disconnect() const {
    return m_builder.template is<SSE>() && m_builder.template p<SSE>().disconnect();
  }

  template <typename Key, typename Value, typename L>
  void header(L &&l) const {
    using Values = Zhttp::HeaderValues<Key, AppHeaders<Impl>>;
    if constexpr (ZuTypeIn<Value, Values>{}) l();
    else if (m_builder.type())
      m_builder.cdispatch([&l](auto, const auto &builder) {
	builder.message.cdispatch([&l](auto, const auto &message) {
	  fixedHeader<Key, Value>(message, ZuFwd<L>(l));
	});
      });
  }
  template <typename Key, typename L>
  void header(L &&l) const {
    if constexpr (ZuIsSame<Key, ContentType>{}) {
      if (m_builder.template is<SSE>()) m_builder.template p<SSE>().template header<Key>(l);
      else if (m_builder.template is<Fixed>()) {
	using JSON = Zhttp::HeaderValue<ZuType<1, typename Fixed::Headers>>;
	l(JSON{}());
      }
    } else if constexpr (ZuIsSame<Key, ContentLength>{}) {
      if (m_builder.template is<Fixed>()) m_builder.template p<Fixed>().template header<Key>(l);
      else if (!m_builder.type()) l("0");
    } else if constexpr (ZuIsSame<Key, CacheControl>{}) {
      if (m_builder.template is<SSE>()) m_builder.template p<SSE>().template header<Key>(l);
    } else {
      using Values = Zhttp::HeaderValues<Key, AppHeaders<Impl>>;
      if constexpr (ZuTypeIn<Key, AppKeys>{} && !Values::N)
	m_impl->template responseHeader<Key>(l);
      else if (m_builder.type())
	m_builder.cdispatch([this, &l](auto, const auto &builder) {
	  builder.message.cdispatch([this, &l](auto, const auto &message) {
	    responseHeader<Key>(m_impl, message, ZuFwd<L>(l));
	  });
	});
    }
  }
  template <typename L> void header(L &&) const { }
  template <typename Emit>
  void body(Emit &&emit) {
    using Result = decltype(emit(WriteProbe{}));
    if constexpr (ZuIsSame<Result, bool>{}) {
      if (auto sse = m_builder.template ptr<SSE>()) sse->body(ZuFwd<Emit>(emit));
    } else {
      if (auto fixed = m_builder.template ptr<Fixed>()) fixed->body(ZuFwd<Emit>(emit));
    }
  }
  template <typename L>
  void bodyHdrs(L &&l) const {
    if (m_builder.template is<Fixed>())
      m_builder.template p<Fixed>().bodyHdrs(ZuFwd<L>(l));
  }
  void close() {
    if (auto sse = m_builder.template ptr<SSE>()) sse->close();
  }
private:
  Impl *m_impl = nullptr;
  Builder m_builder;
};

template <typename Impl, typename Catalog> class HTTPClient;

template <typename Impl, typename Catalog>
struct HTTPRequestData : public ZmObject, public HTTPRequestBuilder<ClientMessage<Catalog>> {
  using Base = HTTPRequestBuilder<ClientMessage<Catalog>>;
  using Extra = HTTPReqHeaders<Catalog, Impl>;
  using ProtocolKeys = ZuTypeSlice<2, 0, typename Base::Headers>;
  using AppKeys = ZuTypeSlice<2, 0, AppHeaders<Impl>>;
  using ExtraKeys = ZuTypeSlice<2, 0, Extra>;
  using AllKeys = ZuTypeConcat<ProtocolKeys, ExtraKeys>;
  using Headers = ZuTypeConcat<typename Base::Headers, Extra>;
  using HdrCatalog = HTTPHdrCatalog<Headers>;
  ZuAssert(AllKeys::N == ZuTypeUnique<AllKeys>::N);

  HTTPClient<Impl, Catalog> *client = nullptr;
  bool responseOK = false;

  using Base::header;
  template <typename Key, typename Value, typename L>
  void header(L &&l) const {
    if constexpr (ZuTypeIn<Key, ProtocolKeys>{}) Base::template header<Key, Value>(l);
    else {
      using Values = Zhttp::HeaderValues<Key, AppHeaders<Impl>>;
      if constexpr (ZuTypeIn<Value, Values>{}) l();
      else Base::message.cdispatch([&l](auto, const auto &message) {
	fixedHeader<Key, Value>(message, ZuFwd<L>(l));
      });
    }
  }
  template <typename Key, typename L>
  void header(L &&l) const {
    if constexpr (ZuTypeIn<Key, ProtocolKeys>{}) Base::template header<Key>(l);
    else {
      using Values = Zhttp::HeaderValues<Key, AppHeaders<Impl>>;
      if constexpr (ZuTypeIn<Key, AppKeys>{} && !Values::N)
	client->impl()->template requestHeader<Key>(l);
      else Base::message.cdispatch([this, &l](auto, const auto &message) {
	requestHeader<Key>(client->impl(), message, ZuFwd<L>(l));
      });
    }
  }
  template <typename L> void header(L &&) const { }
  void completed(const Zhttp::Result &result) { client->completed(this, result); }
};

template <typename Impl, typename Catalog>
class HTTPReplyParser : public HTTPResponseParser<HTTPReplyParser<Impl, Catalog>,
    HTTPHeaders<HTTPResHeaders<Catalog, Impl>>, HTTPResHeaders<Catalog, Impl>> {
  using Extra = HTTPResHeaders<Catalog, Impl>;
  using Keys = ZuTypeSlice<2, 0, Extra>;
  using Meta = HTTPHeaders<Extra>;
  using Base = HTTPResponseParser<HTTPReplyParser, Meta, Extra>;
  using Request = HTTPRequestData<Impl, Catalog>;
public:
  void init(Request &request) { m_request = &request; }
  const Limits &limits() const { return m_request->client->limits(); }
  bool acceptedStatus(unsigned status) const {
    return m_request->message.notification() && status == 200;
  }
  template <typename Key, typename Value>
  void header(Zhttp::FieldSection::T section) {
    Base::template header<Key, Value>(section);
    if constexpr (ZuTypeIn<Key, Keys>{}) this->meta().template put<Key>(Value{}());
  }
  template <typename Key>
  void header(Zhttp::FieldSection::T section, ZuSpan<uint8_t> value) {
    Base::template header<Key>(section, value);
    if constexpr (ZuTypeIn<Key, Keys>{}) this->meta().template put<Key>(value);
  }
  using Base::header;
  bool receiveSSE(SSEEvent event, const Meta &meta) {
    return m_request->client->receiveSSE(m_request, ZuMv(event), meta);
  }
  template <typename Link> void corruptHTTPResponse(Link *link) {
    m_request->responseOK = false;
    if (link) link->disconnect();
  }
  template <typename Link> void failedHTTPResponse(Link *) { m_request->responseOK = false; }
  template <typename Link>
  void acceptedHTTPResponse(Link *, unsigned, const Meta &meta) {
    m_request->responseOK = true;
    m_request->client->headers(m_request, meta);
  }
  template <typename Link>
  void completeHTTPResponse(Link *, unsigned status, const Meta &meta) {
    if (status != 200) m_request->responseOK = false;
    m_request->client->headers(m_request, meta);
  }
  template <typename Link>
  void receiveHTTPResponse(Link *link, unsigned status, ZmRef<ZiIOBuf> body, const Meta &meta) {
    m_request->responseOK = status == 200 && m_request->client->receive(m_request, ZuMv(body));
    m_request->client->headers(m_request, meta);
    if (!m_request->responseOK && link) link->disconnect();
  }
  void reset() { Base::reset(); m_request = nullptr; }
private:
  Request *m_request = nullptr;
};

template <typename Impl, typename Catalog>
struct HTTPPool;
template <typename Impl, typename Catalog>
using HTTPRequestQ = ZmPQueue<HTTPRequestData<Impl, Catalog>,
  ZmPQueueOverlap<false, ZmPQueueNode<HTTPRequestData<Impl, Catalog>,
    ZmPQueueHeapID<"Zjrpc.HTTP.Request">>>>;
template <typename Impl, typename Catalog>
using HTTPTxQ = ZmPQTx<HTTPPool<Impl, Catalog>, HTTPRequestQ<Impl, Catalog>,
  ZmPQTxOrdered<false>>;

template <typename Impl, typename Catalog, typename Heap = ZuVoid>
class HTTPPoolData : public Heap, public Zhttp::Pool<HTTPClient<Impl, Catalog>,
    HTTPTxQ<Impl, Catalog>, HTTPReplyParser<Impl, Catalog>> {
  using Base = Zhttp::Pool<HTTPClient<Impl, Catalog>,
    HTTPTxQ<Impl, Catalog>, HTTPReplyParser<Impl, Catalog>>;
public:
  explicit HTTPPoolData(HTTPClient<Impl, Catalog> *client) : Base{client} { }
  HTTPRequestQ<Impl, Catalog> *txQueue() { return &m_requests; }
  void archive_(typename HTTPRequestQ<Impl, Catalog>::Node *) { }
  ZmRef<typename HTTPRequestQ<Impl, Catalog>::Node> retrieve_(
      typename HTTPRequestQ<Impl, Catalog>::Key,
      typename HTTPRequestQ<Impl, Catalog>::Key) { return {}; }
private:
  HTTPRequestQ<Impl, Catalog> m_requests;
};
template <typename Impl, typename Catalog>
ZuDerive(HTTPPool, (HTTPPoolData<Impl, Catalog,
  ZmHeap<"Zjrpc.HTTP.Pool", HTTPPoolData<Impl, Catalog>>>));

} // Zjrpc

#endif /* ZjrpcHTTP_HH */
