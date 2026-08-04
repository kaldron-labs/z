//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z HTTP/2 application message parser and builder

#ifndef ZhttpH2Message_HH
#define ZhttpH2Message_HH

#ifndef Zhttp_HH
#include <zlib/Zhttp.hh>
#endif

namespace Zhttp {

namespace H2 {

enum {
  StatusSize = 3,		// HTTP status is exactly three decimal digits
  UInt64BufSize = 20		// maximum decimal width of uint64_t
};

ZtEnumStruct(ZhttpAPI, ParserState, int8_t,
  Initial, Body, Stream, RemoteClosed, Trailers, Complete, Error);

template <
  typename Impl,
  bool Request_ = false,
  typename Headers_ = ZuTypeList<>,
  uint64_t MaxBody_ = DefltMaxBody>
class Parser {
public:
  Parser() : m_bodyRx{MaxBody} { }

  auto impl() { return static_cast<Impl *>(this); }

  enum { Request = Request_ };
  using Headers = typename Headers_::template Unshift<
    ZuStringT<"content-length">, void>;
  using State = ParserState;
  static constexpr uint64_t MaxBody = MaxBody_;

  void reset() {
    m_bodyRx.reset(MaxBody);
    m_fields = {};
    m_state = State::Initial;
    m_contentLength = -1;
    m_bodyLength = 0;
    m_complete = false;
    m_headers = false;
    m_bodyAllowed = true;
    m_extendedConnect = false;
  }

  void requestMethod(Method::T method) { m_requestMethod = method; }
  void extendedConnect(bool value) { m_extendedConnect = value; }
  void stream() {
    m_state = State::Stream;
    m_bodyRx.reset(uint64_t(-1));
  }

  bool beginHeaders(bool trailers = false) {
    if (m_headers || m_complete) return fail_();
    if (trailers && m_state != State::Body) return fail_();
    if (!trailers && m_state != State::Initial) return fail_();
    if (!trailers) m_contentLength = -1;
    m_fields = {};
    m_fields.trailers(trailers);
    m_fields.extendedConnect(m_extendedConnect);
    if constexpr (!Request) m_fields.requestMethod(m_requestMethod);
    m_headers = true;
    return true;
  }

  bool field(ZuCSpan name, ZuCSpan value) {
    if (!m_headers) return fail_();
    if (name && name[0] != ':' && !start_()) return fail_();
    if (!m_fields.field(name, value,
      [this](ZuBSpan key, ZuBSpan value_) {
	this->header_(key, value_);
      }) || m_state == State::Error)
      return fail_();
    return true;
  }

  bool endHeaders(bool endStream) {
    if (!m_headers) return fail_();
    m_headers = false;
    auto section = m_fields.finish(
      [this](Method::T method, const RequestTarget &target) {
	impl()->operation(method, target);
      },
      [this](unsigned status) { impl()->status(status); },
      [this](ZuBSpan key, ZuBSpan value) { header_(key, value); });
    if (section == Fields::Invalid) return fail_();
    if (section == Fields::Informational) {
      if (endStream) return fail_();
      m_state = State::Initial;
      return true;
    }
    if (section == Fields::Trailers) {
      if (!endStream || !bodyComplete_()) return fail_();
      m_state = State::Trailers;
      complete_();
      return true;
    }
    m_bodyAllowed = m_fields.bodyAllowed();
    impl()->headers(section, endStream);
    if (m_state == State::Stream) {
      if (endStream) return fail_();
      return true;
    }
    if (endStream) {
      if (m_bodyAllowed && !bodyComplete_()) return fail_();
      complete_();
    } else {
      if (!m_bodyAllowed) return fail_();
      m_state = State::Body;
    }
    return true;
  }

  template <typename Rx>
  bool data(
    Rx &wire, uint64_t frameLength, unsigned headLen, unsigned tailLen,
    bool endStream, bool &transferred)
  {
    transferred = false;
    if (ZuUnlikely(
	headLen > frameLength || tailLen > frameLength - headLen))
      return fail_();
    uint64_t length = frameLength - headLen - tailLen;
    if (!dataLength(length, endStream)) return false;
    uint64_t remaining = frameLength;
    int64_t n = m_bodyRx.splice(
      wire, length,
      [&remaining](ZuBSpan span) -> int64_t {
	if (remaining > span.length()) {
	  remaining -= span.length();
	  return 0;
	}
	return remaining;
      }, wireAlloc_, alloc_, headLen, tailLen,
      [this](auto &rx) {
	if (m_state == State::Stream)
	  impl()->streamRx_(rx);
	else
	  impl()->body(rx);
      });
    if (ZuUnlikely(n <= 0)) return fail_();
    transferred = true;
    if (m_state == State::Stream) {
      if (endStream) {
	impl()->streamPeerEnd_();
	m_bodyRx.discard();
	m_state = State::RemoteClosed;
      }
      return true;
    }
    m_bodyLength += length;
    if (endStream) complete_();
    return true;
  }

  bool dataLength(uint64_t length, bool endStream) {
    if (m_state == State::Stream) return true;
    if (m_state != State::Body || !m_bodyAllowed ||
	m_bodyLength > MaxBody || length > MaxBody - m_bodyLength)
      return fail_();
    if (m_contentLength >= 0 &&
	(m_bodyLength > uint64_t(m_contentLength) ||
	 length > uint64_t(m_contentLength) - m_bodyLength ||
	 (endStream && m_bodyLength + length != uint64_t(m_contentLength))))
      return fail_();
    return true;
  }

  template <typename Rx>
  State::T process(Rx &rx) {
    rx.process(*this);
    return m_state;
  }

  State::T state() const { return m_state; }
  uint64_t consumed() const { return m_bodyRx.consumed(); }
  bool cancel() {
    if (m_state == State::Stream) impl()->streamError_();
    return fail_();
  }

  void headers(Fields::Section, bool) { }
  template <typename Rx>
  void body(Rx &rx) { bodyDrain(rx); }
  template <typename Rx>
  void streamRx_(Rx &rx) { bodyDrain(rx); }
  void streamPeerEnd_() { }
  void streamError_() { }

private:
  bool start_() {
    return m_fields.start(
      [this](Method::T method, const RequestTarget &target) {
	impl()->operation(method, target);
      },
      [this](unsigned status) { impl()->status(status); },
      [this](ZuBSpan key, ZuBSpan value) { header_(key, value); });
  }

  void header_(ZuBSpan key, ZuBSpan value) {
    if (key == "content-length") {
      uint64_t length = 0;
      if (!atou(value, length) || length > MaxBody) {
	fail_();
	return;
      }
      m_contentLength = length;
      impl()->contentLength(length);
      return;
    }
    Fields::dispatch<Headers>(
      key, value,
      [this](auto key_, ZuBSpan value_) {
	impl()->template header<ZuDecay<decltype(key_)>>(value_);
      },
      [this](auto key_, auto value_) {
	impl()->template header<
	  ZuDecay<decltype(key_)>, ZuDecay<decltype(value_)>>();
      },
      [this](ZuBSpan key_, ZuBSpan value_) {
	runtimeHeader_(key_, value_);
      });
  }

  void runtimeHeader_(ZuBSpan key, ZuBSpan value) {
    if constexpr (Fields::HasRuntime<Impl>{})
      impl()->header(key, value);
  }

  bool bodyComplete_() const {
    return m_contentLength < 0 ||
      m_bodyLength == uint64_t(m_contentLength);
  }
  bool fail_() {
    m_state = State::Error;
    complete_();
    return false;
  }
  void complete_() {
    if (m_complete) return;
    m_complete = true;
    if (m_state != State::Error) m_state = State::Complete;
    impl()->complete(m_state);
    m_bodyRx.discard();
  }

  static ZmRef<ZiRxQueue::Node> alloc_() {
    return new BodyRx::BufAlloc{};
  }
  static ZmRef<ZiRxQueue::Node> wireAlloc_() {
    return new BodyRx::WireBufAlloc{};
  }

  Fields::Semantics<Request> m_fields;
  BodyRx		m_bodyRx;
  State::T		m_state = State::Initial;
  Method::T		m_requestMethod = -1;
  int64_t		m_contentLength = -1;
  uint64_t		m_bodyLength = 0;
  bool			m_complete = false;
  bool			m_headers = false;
  bool			m_bodyAllowed = true;
  bool			m_extendedConnect = false;
};

template <
  typename Impl,
  typename Headers_ = ZuTypeList<>,
  typename Trailers_ = ZuTypeList<>,
  bool HasBody_ = false,
  bool Streaming_ = false>
class Builder_ {
public:
  auto impl() { return static_cast<Impl *>(this); }

  using Headers = Headers_;
  using Trailers = Trailers_;
  enum { HasBody = HasBody_ };
  enum { Streaming = Streaming_ };

protected:
  template <typename Stream>
  bool request_(Stream &stream) {
    bool sent = false;
    bool endStream = false;
    bool streamMode = false;
    impl()->operation(
      [this, &stream, &sent, &endStream, &streamMode]
      <typename Target>(Method::T method, Target &&target) {
      ZuCSpan protocol;
      if (method == Method::CONNECT)
	impl()->protocol([&protocol]<typename P>(P &&value) {
	  protocol = ZuCSpan{ZuFwd<P>(value)};
	});
      if (protocol && !stream.extendedConnect()) return;
      streamMode = bool(protocol);
      endStream = !HasBody && !Trailers::N && !protocol;
      stream.beginHeaders(endStream);
      Builder_::field_(stream, ":method", Method::name(method));
      if (method != Method::CONNECT || protocol) {
      Builder_::field_(stream, ":scheme", "https");
	Builder_::field_(stream, ":path", ZuFwd<Target>(target));
      }
      if (protocol) Builder_::field_(stream, ":protocol", protocol);
      sent = true;
    });
    if (!sent) return false;
    impl()->host([&stream]<typename Host>(Host &&host) {
      Builder_::field_(stream, ":authority", ZuFwd<Host>(host));
    });
    if (streamMode)
      headers_<Headers, false>(stream);
    else
      headers_(stream);
    stream.endHeaders(endStream);
    return true;
  }

  template <typename Stream>
  void response_(Stream &stream) {
    unsigned value = impl()->status();
    bool informational = value >= 100 && value < 200;
    bool streamMode = impl()->streamResponse();
    bool endStream =
      !informational && !streamMode && !HasBody && !Trailers::N;
    stream.beginHeaders(endStream);
    ZuCArray<StatusSize> status;
    status[0] = char('0' + ((value / 100) % 10));
    status[1] = char('0' + ((value / 10) % 10));
    status[2] = char('0' + (value % 10));
    field_(stream, ":status", ZuCSpan{status.data(), StatusSize});
    if (streamMode)
      headers_<Headers, false>(stream);
    else
      headers_(stream);
    stream.endHeaders(endStream);
  }

public:
  template <typename Stream>
  auto body(Stream &stream) {
    return stream.body();
  }
  template <typename Stream>
  auto body(Stream &stream, uint64_t remaining) {
    if constexpr (Streaming)
      return stream.body();
    else
      return stream.body(remaining);
  }

  template <typename Stream>
  void finish(Stream &stream) {
    if constexpr (Trailers::N) {
      stream.beginHeaders(true);
      headers_<Trailers, false>(stream);
      stream.endHeaders(true);
    } else if constexpr (HasBody)
      stream.end();
    stream.flush();
  }

  void reset() { }

  template <typename L> void operation(L &&l) { l(Method::GET, "/"); }
  template <typename L> void host(L &&l) { l("127.0.0.1"); }
  template <typename L> void protocol(L &&) { }
  unsigned status() { return 200; }
  bool streamResponse() { return false; }
  template <typename Key, typename L> void header(L &&) { }
  template <typename L> void header(L &&) { }

private:
  template <typename Stream, typename V>
  static void field_(Stream &stream, ZuCSpan name, V &&value) {
    stream.field(name, ZuFwd<V>(value));
  }

  template <
    typename KVs = Headers, bool IncludeContentLength = true,
    typename Stream>
  void headers_(Stream &stream) {
    using Keys = ZuTypeSlice<2, 0, KVs>;
    using Values = ZuTypeSlice<2, 1, KVs>;
    ZuUnroll::all<Keys>([this, &stream]<typename Key>() {
      using Value = ZuType<ZuTypeIndex<Key, Keys>{}, Values>;
      if constexpr (!ZuIsSame<Value, void>{})
	field_(stream, Key{}(), Value{}());
      else
	impl()->template header<Key>([&stream]<typename V>(V &&v) {
	  stream.field(Key{}(), ZuFwd<V>(v));
	});
    });
    runtimeHeaders_(stream);
  }

  template <typename Stream>
  void runtimeHeaders_(Stream &stream) {
    auto fn = [&stream]<typename K, typename V>(K &&k, V &&v) {
      ZtString<ZtStringHeapID<"Zhttp.H2.HeaderName">> name;
      name << ZuFwd<K>(k);
      if (name) stream.field(ZuCSpan{name}, ZuFwd<V>(v));
    };
    if constexpr (Fields::HasRuntimeBuilder<Impl, decltype(fn)>{})
      impl()->header(ZuMv(fn));
  }
};

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  typename Trailers = ZuTypeList<>,
  bool HasBody = false,
  bool Streaming = false>
class ReqBuilder :
  public Builder_<Impl, Headers, Trailers, HasBody, Streaming> {
  using Base = Builder_<Impl, Headers, Trailers, HasBody, Streaming>;

public:
  template <typename Stream>
  bool request(Stream &stream) { return Base::request_(stream); }
};

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  typename Trailers = ZuTypeList<>,
  bool HasBody = false,
  bool Streaming = false>
class ResBuilder :
  public Builder_<Impl, Headers, Trailers, HasBody, Streaming> {
  using Base = Builder_<Impl, Headers, Trailers, HasBody, Streaming>;

public:
  template <typename Stream>
  void response(Stream &stream) { Base::response_(stream); }
};

} // namespace H2

} // namespace Zhttp

#endif /* ZhttpH2Message_HH */
