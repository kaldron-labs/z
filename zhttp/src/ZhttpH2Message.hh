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

ZtEnumStruct(ParserState, int8_t,
  Initial, Body, Tunnel, RemoteClosed, Trailers, Complete, Error);

template <
  typename Impl,
  bool Request_ = false,
  typename Headers_ = ZuTypeList<>,
  uint64_t MaxBody_ = DefltMaxBody>
class Parser {
public:
  auto impl() { return static_cast<Impl *>(this); }

  enum { Request = Request_ };
  using Headers = typename Headers_::template Unshift<
    ZuStringT<"content-length">, void>;
  using State = ParserState;
  static constexpr uint64_t MaxBody = MaxBody_;

  void reset() {
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
  void tunnel() { m_state = State::Tunnel; }

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
    return m_fields.field(name, value,
      [this](ZuBSpan key, ZuBSpan value_) {
	this->header_(key, value_);
      }) && m_state != State::Error;
  }

  bool endHeaders(bool endStream) {
    if (!m_headers) return fail_();
    m_headers = false;
    auto section = m_fields.finish(
      [this](Method::T method, ZuBSpan path) {
	impl()->operation(method, path);
      },
      [this](unsigned status) { impl()->status(status); });
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
    if constexpr (Request)
      if (ZuCSpan protocol = m_fields.protocol())
	impl()->protocol(protocol);
    m_bodyAllowed = m_fields.bodyAllowed();
    impl()->headers(section, endStream);
    if (m_state == State::Tunnel) {
      if (endStream) return fail_();
      m_state = State::Tunnel;
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

  bool data(ZuBSpan value, bool endStream = false) {
    if (m_state == State::Tunnel) {
      if (value) impl()->tunnelData(value);
      if (endStream) {
	m_state = State::RemoteClosed;
	impl()->tunnelEnd();
      }
      return true;
    }
    if (m_state != State::Body || !m_bodyAllowed ||
	m_bodyLength > MaxBody || value.length() > MaxBody - m_bodyLength)
      return fail_();
    if (m_contentLength >= 0 &&
	(m_bodyLength > uint64_t(m_contentLength) ||
	 value.length() > uint64_t(m_contentLength) - m_bodyLength))
      return fail_();
    m_bodyLength += value.length();
    if (value) impl()->body(value);
    if (endStream) {
      if (!bodyComplete_()) return fail_();
      complete_();
    }
    return true;
  }

  template <typename Rx>
  State::T process(Rx &rx) {
    rx.process(*this);
    return m_state;
  }

  State::T state() const { return m_state; }
  bool cancel() {
    if (m_state == State::Tunnel || m_state == State::RemoteClosed)
      impl()->tunnelReset();
    return fail_();
  }

  void protocol(ZuBSpan) { }
  void headers(Fields::Section, bool) { }
  void tunnelData(ZuBSpan) { }
  void tunnelEnd() { }
  void tunnelReset() { }

private:
  void header_(ZuBSpan key, ZuBSpan value) {
    if (key == "content-length") {
      uint64_t length = 0;
      if (!parseUInt64Full_(value, length) || length > MaxBody) {
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
  }

  Fields::Semantics<Request> m_fields;
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
  bool = false>
class Builder {
public:
  auto impl() { return static_cast<Impl *>(this); }

  using Headers = Headers_;
  using Trailers = Trailers_;
  enum { HasBody = HasBody_ };

  template <typename Stream>
  bool request(Stream &stream) {
    bool sent = false;
    bool endStream = false;
    bool tunnel = false;
    impl()->operation(
      [this, &stream, &sent, &endStream, &tunnel]
      <typename Path, typename Query>(
	Method::T method, Path &&path, Query &&query) {
      ZuCSpan protocol;
      if (method == Method::CONNECT)
	impl()->protocol([&protocol]<typename P>(P &&value) {
	  protocol = ZuCSpan{ZuFwd<P>(value)};
	});
      if (protocol && !stream.extendedConnect()) return;
      tunnel = bool(protocol);
      endStream = !HasBody && !Trailers::N && !protocol;
      stream.beginHeaders(endStream);
      Builder::field_(stream, ":method", Method::name(method));
      if (method != Method::CONNECT || protocol) {
	Builder::field_(stream, ":scheme", "https");
	ZuCSpan path_{ZuFwd<Path>(path)};
	ZuCSpan query_{ZuFwd<Query>(query)};
	if (!query_)
	  Builder::field_(stream, ":path", path_);
	else
	  stream.field(":path", path_, '?', query_);
      }
      if (protocol) Builder::field_(stream, ":protocol", protocol);
      sent = true;
    });
    if (!sent) return false;
    impl()->host([&stream]<typename Host>(Host &&host) {
      Builder::field_(stream, ":authority", ZuCSpan{ZuFwd<Host>(host)});
    });
    if (tunnel)
      headers_<Headers, false>(stream);
    else
      headers_(stream);
    stream.endHeaders(endStream);
    return true;
  }

  template <typename Stream>
  void response(Stream &stream) {
    unsigned value = impl()->status();
    bool informational = value >= 100 && value < 200;
    bool tunnel = impl()->tunnelResponse();
    bool endStream =
      !informational && !tunnel && !HasBody && !Trailers::N;
    stream.beginHeaders(endStream);
    char status[3];
    status[0] = char('0' + ((value / 100) % 10));
    status[1] = char('0' + ((value / 10) % 10));
    status[2] = char('0' + (value % 10));
    field_(stream, ":status", ZuCSpan{status, 3});
    if (tunnel)
      headers_<Headers, false>(stream);
    else
      headers_(stream);
    stream.endHeaders(endStream);
  }

  template <typename Stream>
  auto body(Stream &stream) { return stream.body(); }

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

  template <typename L> void operation(L &&l) {
    l(Method::GET, "/", "");
  }
  template <typename L> void host(L &&l) { l("127.0.0.1"); }
  template <typename L> void protocol(L &&) { }
  unsigned status() { return 200; }
  bool tunnelResponse() { return false; }
  template <typename Key, typename L> void header(L &&) { }
  template <typename L> void header(L &&) { }
  uint64_t contentLength() { return 0; }

private:
  template <typename Stream>
  static void field_(Stream &stream, ZuCSpan name, ZuCSpan value) {
    stream.field(name, value);
  }

  template <
    typename KVs = Headers, bool IncludeContentLength = true,
    typename Stream>
  void headers_(Stream &stream) {
    if constexpr (HasBody && IncludeContentLength) {
      char value[32];
      uint64_t length = impl()->contentLength();
      unsigned offset = sizeof(value);
      do {
	value[--offset] = char('0' + (length % 10));
	length /= 10;
      } while (length);
      field_(stream, "content-length",
	ZuCSpan{value + offset, unsigned(sizeof(value) - offset)});
    }
    using Keys = ZuTypeSlice<2, 0, KVs>;
    using Values = ZuTypeSlice<2, 1, KVs>;
    ZuUnroll::all<Keys>([this, &stream]<typename Key>() {
      using Value = ZuType<ZuTypeIndex<Key, Keys>{}, Values>;
      if constexpr (!ZuIsSame<Value, void>{})
	field_(stream, Key{}(), Value{}());
      else
	impl()->template header<Key>([&stream]<typename V>(V &&v) {
	  ZuCSpan value{ZuFwd<V>(v)};
	  if (value) stream.field(Key{}(), value);
	});
    });
    runtimeHeaders_(stream);
  }

  template <typename Stream>
  void runtimeHeaders_(Stream &stream) {
    auto fn = [&stream](ZuCSpan name, ZuCSpan value) {
      if (name) stream.field(name, value);
    };
    if constexpr (Fields::HasRuntimeBuilder<Impl, decltype(fn)>{})
      impl()->header(ZuMv(fn));
  }
};

} // namespace H2

} // namespace Zhttp

#endif /* ZhttpH2Message_HH */
